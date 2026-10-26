#pragma once
#if defined(BOARD_IS_WS_P4_7B) && defined(P4_CLOUD_CAMERA_EXPERIMENTAL)
#include <LovyanGFX.hpp>
#include <vector>
#include "bambu_state.h"
void initP4CameraService();
void startP4CameraService();
void tickP4CameraService();
uint32_t p4CameraGeneration();
bool drawP4Camera(lgfx::LovyanGFX& target, const PrinterSlot& printer, int x, int y);
bool p4CameraJobName(const PrinterSlot& printer, char* out, size_t capacity);
bool p4CameraBusy();
#if defined(P4_PREVIEW)
#include "p4_dashboard_layout.h"
inline std::vector<uint16_t> p4PreviewCamera(p4ui::IMAGE_W * p4ui::IMAGE_CONTENT_H, 0xe739);
inline bool p4CameraBusy() { return false; }
inline uint32_t p4CameraGeneration() { return 0; }
inline bool p4CameraJobName(const PrinterSlot&, char* out, size_t capacity) {
  if (out && capacity) out[0] = 0;
  return false;
}
inline bool drawP4Camera(lgfx::LovyanGFX& target, const PrinterSlot&, int x, int y) {
  // Synthetic 16:9 content, including all four corners; useful for proving
  // that the renderer does not crop the source or move the rest of the page.
  for (int row = 0; row < p4ui::IMAGE_CONTENT_H; ++row) {
    for (int col = 0; col < p4ui::IMAGE_W; ++col) {
      p4PreviewCamera[row * p4ui::IMAGE_W + col] = ((col / 26 + row / 18) & 1) ? 0x001f : 0x07e0;
    }
  }
  target.pushImage(x, y, p4ui::IMAGE_W, p4ui::IMAGE_CONTENT_H, p4PreviewCamera.data());
  return true;
}
#endif
#else
inline void initP4CameraService() {}
inline void startP4CameraService() {}
inline void tickP4CameraService() {}
#endif
