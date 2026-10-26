# 摄像头与云端模型预览

已按用户提供的地址 clone `ptrl-design/bambu-bar` 的 `camera-feed` 分支，
本地位于 `.cache/reference/bambu-bar-camera-feed/`（忽略目录）。
检查版本：`a6fe610bbd8153658d21e8bd30a7a3faf174bf55`。

- `Sources/BambuBar/App/CameraFeed.swift:162` 将 `session.config.host` 传给摄像头客户端。
- `Sources/BambuBar/Printer/CameraClient.swift` 区分 RTSPS:322 与 JPEG:6000，
  `CameraSocket.open()` 对该主机发起直接 TLS 连接。
- `Sources/BambuBar/Printer/RTSPCameraClient.swift` 支持 X2D 的 RTSPS 握手、
  interleaved RTP 与 H.264/H.265；视频通过 macOS CoreMedia/AVSampleBufferDisplayLayer 解码。
- 该分支是局域网摄像头实现，没有跨网络的云端 P2P 取流；不能将它当成 HTTPS 截图接口移植到 P4。

源码：https://github.com/ptrl-design/bambu-bar/tree/camera-feed

试验阶段曾复用 `thumb_fetch.cpp` / `bambu_cloud.cpp` 获取云端模型图，并验证同区域
服务代理与严格匹配任务 id 的封面回退。实板发现并行取图时网络稳定性不符合要求，
当前 P4 按用户决定完全关闭该路径：固定显示内置机型图，只保留 MQTT，不轮播，也不创建
第二条 TLS 会话。通用 PSRAM 板型的云端预览代码保留，不受 P4 的关闭开关影响。

远程视频需要另一套协议与解码链路，本次未接入；原有 P1/A1 LAN 摄像头功能仍保留。
