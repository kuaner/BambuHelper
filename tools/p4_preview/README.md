# Native P4 UI preview

This tool compiles the **actual firmware renderer**, LovyanGFX PNG decoder,
fonts, error text lookup and navigation hit testing against the library's SDL
platform. It does not open a window or contact a printer. Every measurement,
address and device name in the generated images is synthetic.

Install a C++17 compiler, CMake, Python 3 and SDL2; build the P4 PlatformIO
environment once to obtain its pinned LovyanGFX dependency. On macOS, SDL2 and
CMake can be supplied by Homebrew. Then run from the repository root:

```sh
cmake -S tools/p4_preview -B .cache/p4-preview-build
cmake --build .cache/p4-preview-build -j 8
TZ=Asia/Shanghai .cache/p4-preview-build/p4_preview .cache/p4-preview
```

Output is portable PPM. Convert with Pillow, ImageMagick or macOS `sips`:

```sh
sips -s format png .cache/p4-preview/dashboard-demo.ppm \
  --out .cache/p4-preview/dashboard-demo.png
```

The executable exercises home, detail tabs, AMS navigation, zero humidity,
external spool, disconnected/unknown values, long Chinese titles, overflow AMS
HT, A1 and alert scrolling. It uses assertions for navigation outcomes. Runtime
integration, physical GT911 orientation, Wi-Fi and real display timing still
require hardware verification. GT911 I2C and gesture cancellation are covered
separately by `python3 -m unittest discover -s tests -v`.

The QR renderer reads the LGFX encoder's packed module grid directly: in
LovyanGFX 1.2.24, its C getModule implementation returns a byte bit mask through
a C++ bool declaration, which produced missing modules in the desktop build.
The generated settings QR was independently decoded to `http://192.0.2.30`.
