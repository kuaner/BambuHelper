# Waveshare ESP32-P4-WIFI6-Touch-LCD-7B

本适配针对微雪 **7B / EK79007 1024×600** 开发板，32 MB Flash、32 MB PSRAM、
GT911 触摸，以及通过 SDIO 连接的 ESP32-C6 无线协处理器。
其他 P4 屏幕型号需要各自的屏幕时序和引脚配置。

## 编译与烧录

使用 Python 3.10–3.14 和 PlatformIO。此板型单独固定在 pioarduino `55.03.311`
（Arduino-ESP32 `3.3.11`），与厂商 Arduino 示例一致；其他板型仍使用原有工具链。
首次构建需要下载 P4 工具链和预编译库。

本机全局 `pio` 若使用 Python 3.15，可在项目内使用独立环境：

```sh
python3.14 -m venv .pio-core/venv
.pio-core/venv/bin/python -m pip install platformio
source .pio-core/venv/bin/activate
```

| 实际芯片版本 | 构建环境 | 说明 |
| --- | --- | --- |
| Rev3.1 及以上 | `ws_p4_lcd_7b` | 默认 production P4 库 |
| Rev1.3 / pre-v3 | `ws_p4_lcd_7b_rev1` | 使用 `esp32p4_es` 库 |

请先按实际芯片版本选择环境。厂商特别说明：菜单中的 post-v3 不代表恰好 Rev3.0
的样片能运行 production 库；编译成功不能替代芯片版本检查。

```sh
pio run -e ws_p4_lcd_7b
pio run -e ws_p4_lcd_7b -t upload --upload-port /dev/你的串口
pio device monitor -b 115200 --port /dev/你的串口
```

Rev1.3 将上述环境名称替换为 `ws_p4_lcd_7b_rev1`。串口按本机实际设备修改，
使用板载 USB-Serial/JTAG 烧录连接。Arduino 日志和 Improv 配网使用同一 USB 串口。

生成可分发的固件：

```sh
python merge_bins.py --board ws_p4_lcd_7b
# Rev1.3:
python merge_bins.py --board ws_p4_lcd_7b_rev1
```

输出位于 `firmware/<版本>/`：`Full.bin` 是从地址 `0x0` 写入的完整镜像，
其中 P4 bootloader 在 **`0x2000`**，分区表在 `0x8000`，应用在 `0x10000`。
`ota.bin` 仅用于已安装对应板型固件的手动 OTA 上传。
两个芯片版本使用不同的固件文件名和板型标识，更新时保持所选环境一致。

32 MB 分区表包含两个 13 MB OTA 槽和剩余的数据分区。首次安装或切换分区表时使用
USB 完整烧录。此板型尚未加入网页烧录器和自动发布列表，自动在线更新未开启。

## 已接入功能

- EK79007 MIPI-DSI：双 lane、1000 Mbit/s、52 MHz DPI 时钟，HSYNC 10/160/160，
  VSYNC 1/23/12。显示由原生 RGB565 PSRAM framebuffer 驱动，按更新区域回写缓存。
- 原生 1024×600 横屏仪表盘：六个仪表、大字体、配网页、打印状态、时钟与 HMS。
  面板安装方向默认补偿 180°；设置中的 0° 是补偿后的默认方向，180° 再翻转一次。
  导入配置中的 90° / 270° 会归一到横屏。
- GT911：I2C SDA=7 / SCL=8，轮询 `0x5D`，兼容 `0x14` 地址；不驱动 INT/RST。
  沿用现有点击、长按和唤醒手势。
- 背光：GPIO32，低电平有效，5 kHz PWM，支持亮度、夜间模式和灭屏。
- ESP32-C6 Wi-Fi：由 Arduino 的 ESP-Hosted 提供，保留原有热点配网、Improv、
  MQTT、云登录和网页配置流程。
- ES8311 扬声器：I2C `0x18`，MCLK=13、BCLK=12、LRCK=10、DOUT=9，功放使能=53。
  使用现有声音通知设置。
- LED / 外接按钮 / GPIO 蜂鸣器：默认不分配 GPIO，并对用户配置和已有 NVS 中的
  引脚进行检查，保护屏幕、音频、无线、USB、启动和存储接口。

无线 SDIO 引脚为 CLK=18、CMD=19、D0=14、D1=15、D2=16、D3=17、C6 reset=54。
正常安装只烧录 P4，C6 保留现有出厂固件，无需预先修改或重刷。此次适配没有修改
C6 固件，普通 P4 应用烧录也不会更新 C6。当前尚无实板日志表明需要更新 C6；
如果实际出现无线初始化失败，再根据 ESP-Hosted 连接/版本日志排查兼容性。

## 显示与触摸操作

- 默认使用 Cards，显示当前打印任务名、剩余时间、层数、温度与正在进料的 AMS。
  Gauges 与 Cards 均提供原生 1024×600 布局；网页 Print screen 可切换样式，
  Gauges 可选择六仪表或八仪表网格，两种样式均显示任务名，支持中文打印机名和任务名。
- 打印中、待机或打印完成时，默认轻点屏幕打开 AMS 总览，再点返回；30 秒自动返回。
  多页 AMS 每 8 秒轮换。总览显示料槽颜色、材料、当前进料槽、已知剩余量、温度与湿度，
  同时兼容 AMS HT 和外置料架。未报告的剩余量不会被伪造为百分比。
- AMS 湿度同时接受数字与字符串字段，保留增量更新的已知读数；有效的 0% 正常显示，
  不会回退成湿度等级。只有没有百分比字段的旧 AMS 才显示 L1–L5。
- Gauge Layout 可选择各 AMS 的温湿度、耗材与料槽仪表；勾选 AMS view 可用料槽条
  替换仪表盘第二行。网页也可点击 Switch device status / AMS page 打开总览。
- Screen 中可自定义单击、双击和长按动作；Hardware 中 Button type 选择 Touchscreen。
  触摸是整屏手势，料槽显示用于查看状态。
- 双打印机分屏为左右两个面板，各显示四个仪表；保留自动轮换与 PSRAM 板型的
  可选四打印机模式。
- Cards 保留云端打印盘缩略图；局域网摄像头沿用项目原有 P1/A1 协议，需 LAN 模式、
  正确访问码并配置 Camera 仪表。当前连接的云端 X2D 不属于该摄像头协议的支持范围。
- 截图接口支持完整 1024×600；`/debug` 提供 AMS 数量与触摸帧数、按压次数和读错误数。

microSD、板载摄像头采集、麦克风、RS485 和 CAN 没有对应的 BambuHelper 业务功能。

## 中国区云配置

实板已使用 `bambulab.cn` 登录 Cookie 中的 `token` 值连接中国区云端；仅填写该字段的值，
不要粘贴整段 Cookie。区域选择 China/CN。使用 My printers 返回的序列号，避免手工漏字：
MQTT 连接成功仅证明已登录云端，不代表所订阅的序列号正确。当前 API 使用中国区域名，
MQTT 为 `cn.mqtt.bambulab.com:8883`。

## 验证边界

已在 Rev1.3 实板验证屏幕、180° 方向、Wi-Fi、中国区 MQTT 实时打印数据与 GT911 按压事件，以及两台 AMS 2 的准确湿度（包含 0%）。
Cards、AMS 总览、料槽条和截图按实板渲染结果检查。生产版芯片环境另做编译验证。
当前只有一台云端 X2D 可供测试；双打印机分屏、LAN 摄像头、通知音与 OTA 的完整运行流程
仍需对应设备或场景验证，不能把编译通过等同于这些场景全部实测。
若点屏初始化失败，固件会关闭背光并在串口报告失败步骤，避免继续访问空 framebuffer。

配置测试使用 `python -m unittest discover -s tests`，覆盖 OTA 分区边界、P4 ROM
bootloader 地址、保留 GPIO、屏幕旋转及仪表区域边界。

引脚和时序来自用户提供的厂商资料库：
`examples/arduino/libraries/displays/displays_config.h`、
`examples/arduino/libraries/GFX_Library_for_Arduino/src/databus/Arduino_ESP32DSIPanel.cpp`、
`examples/arduino/examples/09_Audio_Playback/` 和 `examples/arduino/README_ZH.md`。
另见[微雪官方文档](https://docs.waveshare.net/ESP32-P4-WIFI6-Touch-LCD-7B/)
和 [pioarduino 55.03.311](https://github.com/pioarduino/platform-espressif32/releases/tag/55.03.311)。
