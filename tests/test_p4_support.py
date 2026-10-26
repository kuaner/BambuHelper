"""Host checks: P4 peripheral guards, partition bounds and ROM flash offsets.

Run: python -m unittest discover -s tests
"""
import csv
import importlib.util
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("merge_bins", ROOT / "merge_bins.py")
merge_bins = importlib.util.module_from_spec(spec)
spec.loader.exec_module(merge_bins)


class P4SupportTests(unittest.TestCase):
    @unittest.skipUnless(shutil.which("c++"), "A host C++ compiler is required")
    def test_gt911_press_hold_release_and_bus_failure(self):
        # Run the real backend against an I2C register stub: a missing frame is
        # not a release, and an I2C error must not create a phantom touch.
        arduino = r'''
        #pragma once
        #include <cstdint>
        #include <cstddef>
        #include <cstdio>
        extern uint32_t nowMs;
        inline uint32_t millis() { return nowMs; }
        constexpr int INPUT = 0;
        inline void pinMode(int, int) {}
        struct FakeSerial {
          template<class... T> void printf(const char*, T...) {}
          template<class T> void print(T) {}
          void println() {}
          template<class T> void println(T) {}
        };
        extern FakeSerial Serial;
        '''
        wire = r'''
        #pragma once
        #include <vector>
        #include "Arduino.h"
        struct FakeWire {
          uint8_t address = 0, status = 0;
          bool fail = false, xyFail = false;
          uint16_t x = 123, y = 456;
          uint16_t reg = 0;
          std::vector<uint8_t> tx, rx;
          size_t pos = 0;
          void begin(int, int) {}
          void setClock(int) {}
          void beginTransmission(uint8_t a) { address = a; tx.clear(); }
          void write(uint8_t b) { tx.push_back(b); }
          int endTransmission(bool = true) {
            if (fail || address != 0x5D) return 2;
            if (tx.size() >= 2) reg = (tx[0] << 8) | tx[1];
            if (tx.size() == 3 && reg == 0x814E) status = tx[2];
            return 0;
          }
          size_t requestFrom(uint8_t, uint8_t n) {
            rx.assign(n, 0); pos = 0;
            if (reg == 0x814E) rx[0] = status;
            if (reg == 0x8140 && n == 4) rx = {'9', '1', '1', 0};
            if (reg == 0x8150 && n == 4) {
              if (xyFail) return 0;
              rx = {uint8_t(x), uint8_t(x >> 8), uint8_t(y), uint8_t(y >> 8)};
            }
            return n;
          }
          int read() { return rx[pos++]; }
        };
        extern FakeWire Wire;
        '''
        probe = r'''
        #include <cassert>
        #include "Wire.h"
        #include "button_touch_backend.h"
        uint32_t nowMs = 1;
        FakeSerial Serial;
        FakeWire Wire;
        int main() {
          touchInit();
          assert(touchDiagnostics().ready);
          Wire.status = 0x81;
          assert(touchPoll().isDown);
          assert(Wire.status == 0);                  // acknowledged
#ifdef BOARD_IS_WS_P4_7B
          assert(touchContact().valid && touchContact().x == 123 && touchContact().y == 456);
#endif
          nowMs = 100;
          assert(touchPoll().isDown);               // no new frame: held
          assert(touchDiagnostics().presses == 1);
          Wire.status = 0x80;
          assert(!touchPoll().isDown);              // explicit release
          Wire.status = 0x81;
          assert(touchPoll().isDown);
          nowMs = 1200;
          assert(!touchPoll().isDown);              // stale hold expires
          Wire.fail = true;
          assert(touchPoll().ev == TouchEvent::Unavailable);
          const auto d = touchDiagnostics();
          assert(d.presses == 2 && d.frames == 3 && d.readErrors == 1);
#ifdef BOARD_IS_WS_P4_7B
          assert(!touchContact().valid);             // no navigation after I2C failure
          Wire.fail = false; Wire.status = 0x81;
          touchPoll(); assert(touchContact().valid);
          Wire.status = 0x82; touchPoll();
          assert(!touchContact().valid && Wire.status == 0); // multitouch acknowledged
          Wire.status = 0x81; Wire.xyFail = true; touchPoll();
          assert(!touchContact().valid && Wire.status == 0); // coordinate read failed
          Wire.xyFail = false; Wire.x = 1024; Wire.status = 0x81; touchPoll();
          assert(!touchContact().valid);             // invalid edge coordinate
          Wire.x = 1023; Wire.y = 599; Wire.status = 0x81; touchPoll();
          assert(touchContact().valid);
          nowMs += 1001; touchPoll();
          assert(!touchContact().down && !touchContact().valid); // stale hold cancels
#endif
        }
        '''
        with tempfile.TemporaryDirectory() as tmp:
            folder = Path(tmp)
            (folder / "Arduino.h").write_text(arduino)
            (folder / "Wire.h").write_text(wire)
            (folder / "probe.cpp").write_text(probe)
            exe = folder / "probe"
            for extra in ([], ["-DBOARD_IS_WS_P4_7B=1"]):
                subprocess.run(["c++", "-std=c++11", "-DUSE_GT911=1", "-DGT911_SDA=7",
                                "-DGT911_SCL=8", "-DGT911_INT=-1", *extra, "-I", tmp,
                                "-I", str(ROOT / "src"), str(folder / "probe.cpp"),
                                str(ROOT / "src/button_touch_gt911.cpp"), "-o", str(exe)], check=True)
                subprocess.run([str(exe)], check=True)

    def test_partition_bounds_and_ota_slots(self):
        with (ROOT / "partitions_32mb.csv").open() as src:
            rows = [row for row in csv.reader(src) if row and not row[0].startswith("#")]
        end = 0x9000
        apps = []
        for row in rows:
            name, kind, subtype, offset, size = (cell.strip() for cell in row[:5])
            offset, size = int(offset, 0), int(size, 0)
            self.assertGreaterEqual(offset, end, name)
            self.assertGreater(size, 0, name)
            end = offset + size
            if kind == "app":
                self.assertEqual(offset % 0x10000, 0, name)
                apps.append((subtype, size))
        self.assertEqual(apps, [("ota_0", 13 * 1024**2), ("ota_1", 13 * 1024**2)])
        self.assertEqual(end, 32 * 1024**2)

    def test_full_images_follow_p4_rom_offsets(self):
        # Use tagged segments to detect a misplaced bootloader, including a
        # regression to the S3/C3 address 0 or the classic ESP32 address 0x1000.
        segments = {
            "bootloader.bin": (0x2000, b"P4-BOOT" * 64),
            "partitions.bin": (0x8000, b"P4-PART" * 32),
            "boot_app0.bin": (0xE000, b"OTA-DATA" * 1024),
            "firmware.bin": (0x10000, b"P4-APP" * 512),
        }
        for board in ("ws_p4_lcd_7b", "ws_p4_lcd_7b_rev1"):
            with self.subTest(board=board), tempfile.TemporaryDirectory() as tmp:
                folder = Path(tmp)
                for name, (_, data) in segments.items():
                    (folder / name).write_bytes(data)
                image = folder / "Full.bin"
                with patch.dict(merge_bins.BOARDS[board], {"build_dir": tmp}):
                    self.assertTrue(merge_bins.merge_binaries(tmp, image, board))
                data = image.read_bytes()
                self.assertEqual(data[:0x2000], b"\xff" * 0x2000)
                for offset, expected in segments.values():
                    self.assertEqual(data[offset:offset + len(expected)], expected)
                self.assertEqual(merge_bins.BOARDS[board]["board_id"], board)

    @unittest.skipUnless(shutil.which("c++"), "A host C++ compiler is required")
    def test_gpio_guards_and_display_geometry(self):
        source = r'''
        #include "ws_p4_7b_config.h"
        #include "layout_1024x600.h"
        #include "layout_card.h"
        #ifdef BOARD_IS_WS_P4_7B
        static_assert(normalizeDisplayRotation(0) == 0, "native landscape");
        static_assert(normalizeDisplayRotation(1) == 0, "no portrait layout");
        static_assert(normalizeDisplayRotation(2) == 2, "upside down landscape");
        static_assert(normalizeDisplayRotation(3) == 2, "no portrait layout");
        int main() {
          const int reserved[] = {0, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17,
            18, 19, 21, 22, 26, 27, 32, 33, 35, 36, 37, 38, 39, 40, 41,
            42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 255};
          for (int pin : reserved) if (!isP4SevenBReservedPin(pin)) return 1;
          if (isP4SevenBReservedPin(4)) return 2;
          if (cardBottomBaseline(CARD_GEO_P4) != 576) return 3;
        }
        #else
        static_assert(normalizeDisplayRotation(1) == 1, "existing portrait");
        static_assert(normalizeDisplayRotation(3) == 3, "existing portrait");
        int main() {}
        #endif
        static_assert(LY_COL1 - LY_GAUGE_R >= 0, "left gauge clipped");
        static_assert(LY_COL3 + LY_GAUGE_R < LY_W, "right gauge clipped");
        static_assert(LY_ROW1 - LY_GAUGE_R >= LY_HDR_Y + LY_HDR_H, "header overlap");
        static_assert(LY_ROW1 + LY_GAUGE_R + 32 < LY_ROW2 - LY_GAUGE_R, "row overlap");
        static_assert(LY_ROW2 + LY_GAUGE_R + 20 < LY_ETA_Y, "ETA overlap");
        static_assert(LY_BOT_Y + LY_BOT_H <= LY_H, "bottom bar clipped");
        // Icons are 2x on this panel. Test the actual footprint at both edges,
        // which catches the original lock positioned using a 16px footprint.
        static_assert(LY_BOT_CY - LY_SC(16) / 2 >= LY_BOT_Y, "lock top clipped");
        static_assert(LY_BOT_CY + LY_SC(16) / 2 <= LY_H, "lock bottom clipped");
        static_assert(LY_AMS_Y > LY_ROW1 + LY_GAUGE_R + 32, "AMS overlaps row 1");
        static_assert(LY_AMS_Y + LY_AMS_H + 10 < LY_ETA_Y, "AMS overlaps ETA");
        static_assert(LY_SPLIT_L_COL1 - LY_SPLIT_L_GAUGE_R >= 0, "split left clipped");
        static_assert(LY_SPLIT_L_COL2 + LY_SPLIT_L_GAUGE_R < LY_W / 2, "split right clipped");
        static_assert(LY_SPLIT_L_ROW2 + LY_SPLIT_L_GAUGE_R + 28 < LY_H - 80,
                      "split labels overlap ETA");
        '''
        with tempfile.TemporaryDirectory() as tmp:
            probe = Path(tmp) / "probe.cpp"
            probe.write_text(source)
            for flags in ([], ["-DBOARD_IS_WS_P4_7B=1"]):
                exe = Path(tmp) / "probe"
                subprocess.run(["c++", "-std=c++11", "-I", str(ROOT / "include"),
                                *flags, str(probe), "-o", str(exe)], check=True)
                subprocess.run([str(exe)], check=True)


if __name__ == "__main__":
    unittest.main()
