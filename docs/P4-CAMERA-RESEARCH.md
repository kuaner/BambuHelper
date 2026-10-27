# P4 云端打印实景截图

## 已验证的直接取图路径

2026-10-26：Mac 已通过自行编译的 C++ 客户端，跨局域网取得 X2D
`01.02.00.00` 的真实腔体 JPEG（1920×1080，约 83–90 KB）。OpenSSL 后端
连续三次成功；与 P4 相同的 Mbed TLS 3.6.6 后端连续两次成功。
运行时不加载 Bambu Studio 网络插件，不需要电脑或 NAS 转发画面。
P4 的实板结果见后续验证记录；Mac 成功不代表所有打印机/网络均已验证。

1. 使用现有账号 token、应用证书的签名证明和已安装 Studio 的签名元数据，
   请求区域 API `POST /v1/iot-service/api/user/ttcode`。
2. 请求限定 `protocols:["tutk"]`。同时允许 Agora 时，当前服务器会选择 Agora。
3. 取得短期 `ttcode/authkey/passwd`，通过 TUTK rendezvous、UDP 打洞和 DTLS
   建立文件控制通道，不发送视频启动命令。
4. 发送 `cmdtype:4`，下载 `mem:/26`；参数为 `get_project_file`、
   `peer_host:"studio"`、空 `file_rel_path`。
5. 去除首包附带的 JSON 元数据，按 offset 拼接，验证长度、MD5 和 JPEG 边界。
   成功或失败都关闭会话。

普通 Bearer token 或缺少 executable-info 的签名请求会返回 403。
应用私钥、账号 token 和短期连接凭证都没有放进源码或固件。实验设备通过独立
配置导入应用凭据，账号 token 继续使用原有配置。HTTP 信任链仍由 CA bundle 校验。

## P4 集成与配置

- Rev1.3 使用 `ws_p4_lcd_7b_rev1_camera`；Rev3.1+ 使用 `ws_p4_lcd_7b_camera`。
  模块保留 `P4_CLOUD_CAMERA_EXPERIMENTAL` 编译标识，旧的 `_rev1` / 无后缀
  环境仍可用于不带实景的诊断。
- 单次图片最大 512 KiB；JPEG、分片缓冲和两个工作线程栈使用 PSRAM。
- 短期 HTTPS 完全释放后才建立 TUTK/DTLS，主循环继续处理 MQTT、触摸和界面。
- 不启用云端模型缩略图下载、视频流或模型轮播。
- 开机没有当前任务实景时显示机型图，首张有效图片到达后自动切换；点击图片区手动切换。
  单独的“查看详情”按钮进入详情。
- 成功后每 5 分钟刷新，失败指数退避，保留最后一张通过验证的图片。
- Mac 自动路径已验证跨网 P2P；强制中继测试仅完成握手和 AV 登录，文件回复超时，
  尚不能声称所有中继路径已通过。

## 来源与许可

- [Bambu Studio MediaPlayCtrl.cpp](https://github.com/bambulab/BambuStudio/blob/79df7ae309be398ed30c9f397c13c9911c2edc57/src/slic3r/GUI/MediaPlayCtrl.cpp)：
  `start_device_image_flow()` / `DownloadMemFile("mem:/26", ...)`。
- [FileTransferObject.cpp](https://github.com/bambulab/BambuStudio/blob/79df7ae309be398ed30c9f397c13c9911c2edc57/src/slic3r/Utils/FileTransferObject.cpp)：
  内存文件单次传输、cmd_type 4。
- [open-bamboo-networking](https://github.com/ClusterM/open-bamboo-networking/tree/2c17a64b5075dc98af977b098d12dd3c39a9c670)：
  TUTK/DTLS/AV/FEC 传输和协议研究。本项目的 `lib/BambuTUTK` 保留 AGPL-3.0
  许可，链接该模块的实验固件须遵守该许可；其他文件保留各自已有许可。
- [bambu-bar camera-feed](https://github.com/ptrl-design/bambu-bar/tree/camera-feed)：
  此前检查的版本 `a6fe610bbd8153658d21e8bd30a7a3faf174bf55` 使用局域网
  RTSPS:322 / JPEG:6000，与这里的云端内存文件截图是不同路径。

应用凭据通过设备本地的 `POST /camera/credentials` 导入，字段为 `certificate`、
`private_key`、`platform`、`device_id`、`firmware` 和 `executable_info`。账号 token
沿用云打印配置。`/camera/status` 只返回阶段和内存诊断；`POST /camera/capture`
请求后台抓一张，`POST /camera/automatic` 的表单字段 `enabled=1/0` 控制自动刷新。
这些路由和配置网页一样没有额外的身份认证，只在可信的设备局域网中使用。
凭据与图片不能提交到 Git。主机工具见 [tools/p4_cloud_camera](../tools/p4_cloud_camera/README.md)。

本分支不维护其他板型的 LAN 摄像头或模型缩略图功能。
