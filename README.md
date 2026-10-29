# Mac Wake S3 v0.5.2（受限业务看门狗候选版）

默认构建的业务监督仍仅告警。v0.5.2 可选的业务看门狗默认关闭，详情见 `docs/受限业务看门狗-20261029.md`。源码及编译验证不代表实机验收；新增行为和验证步骤见 `docs/业务监督验证版-20261029.md`。

基于 ESP32-S3-DevKitC-1 N16R8 的 Mac 远程睡眠／唤醒器，网络控制仅通过点灯 APP 云端进行。v0.5 已移除局域网网页、HTTP 控制与状态接口、mDNS 广播及局域网控制口令。

目标实机为 MacBook Air M5：合盖、持续接电、连接外接显示器。v0.4 用户已实测睡眠、外网唤醒和远程桌面恢复成功；v0.5 仅云端版也已由用户确认睡眠、唤醒及远程桌面均正常，详细验收边界见 `docs/云端专用版-20261023.md`。隔夜及断网／掉电恢复仍待验证。

## 使用方式

| APP 按钮 | 数据键名 | 接收动作 |
|---|---|---|
| 唤醒 | `btn-wakeup` | `tap`、`press`、`on` |
| 睡眠 | `btn-sleep` | `tap`、`press` |

显示名称由 APP 设置，固件不会覆盖。保留板载 BOOT 实体按钮唤醒与 BOOT/RST 下载操作。Wi-Fi 仍用于开发板主动连接点灯云端；无需路由器端口映射，断网时 APP 无法控制。

云端指令校验绑定账号来源，忽略保留及带重复标志的消息，拒绝混合睡眠／唤醒指令。单请求队列、最短 3 秒间隔。HTTPS 认证及 MQTT over WSS 均校验证书，启动先校时；当 DHCP 未提供主 DNS 时使用备用解析器。

睡眠模拟 Apple 官方 Option＋Command＋媒体推出键；唤醒默认使用 USB Remote Wakeup，不输入密码或自动登录。固件回传 USB 处理结果，`mac_online` 保持 `null`；指令接受或总线恢复不代表远程桌面已可用。

## 接线与烧录

1. 用数据线将开发板原生 USB／OTG 接口连接 Mac。旁边的 USB 转串口接口用于烧录／UART0 日志，不能代替原生 USB 键盘接口。
2. Mac 首次提示配件连接时允许访问。保持接电及外接显示器，并沿已验证能用普通键盘唤醒的连接路径测试。
3. 按住 BOOT → 短按 RST → 等约 1 秒松开 BOOT，进入下载模式。写入后若未正常启动，只短按 RST。
4. 若需要日志，使用另一根线连接 USB 转串口接口。原生 USB 正常运行时为 HID，不提供串口日志。

## 构建

固定版本：ESP-IDF **v5.4.2**、esp_tinyusb **1.7.6~1**、TinyUSB **0.18.0~4**。Flash 16 MB，自定义分区表，应用分区 3 MB；不使用板载 PSRAM。依赖由 IDF Component Manager 按 `dependencies.lock` 下载。

```sh
git clone --branch v5.4.2 --recursive https://github.com/espressif/esp-idf.git
cd esp-idf
./install.sh esp32s3
. ./export.sh
```

在本仓库根目录执行：

```sh
python3 tools/configure.py
idf.py -D SDKCONFIG=sdkconfig.local build
idf.py -D SDKCONFIG=sdkconfig.local -p 实际串口 flash
```

配置工具交互输入 2.4 GHz Wi-Fi 与点灯独立设备 AuthKey，不把凭据放在命令行参数中。`sdkconfig.local` 权限为 0600；修改现有配置可使用：

```sh
idf.py -D SDKCONFIG=sdkconfig.local menuconfig
```

默认 SSID 为空时仅测试实体 BOOT 唤醒。`Send a Shift…` 默认关闭，只有经过人工测试需要时才启用。升级需使用配套分区表；首次烧录前备份原板固件。

`tools/idf-local.sh` 兼容本聊天工作区的本地工具链；其他机器请使用上方标准 ESP-IDF 环境。

## 点灯 APP

1. 创建独立设备，选择网络接入，取得 AuthKey 并填入私有配置。
2. 新增两个按钮，数据键名按上表填写。
3. 等设备在线。保存工作后点击“睡眠”，等约 30 秒点击“唤醒”，确认远程桌面恢复。
4. 外网验收时关闭手机 Wi-Fi，改用移动数据；APP 在线只代表开发板云端连接正常。

所用官方 WSS 通道曾完成证书校验与连接实测。2026-10-22 调研发现当时直接 MQTTS 端口证书过期，因此没有使用该通道，也没有关闭证书校验。

## 测试与公开文件

导出 ESP-IDF 后执行：

```sh
bash tools/test-cloud-command.sh
```

23 项测试涵盖账号来源、按钮键名、混合指令、重复／保留消息、松开动作、畸形 JSON 与长度限制；不会连接硬件或触发睡眠。

仓库包含完整源码、依赖锁、分区表、配置工具、测试、文档及无联网凭据的 v0.1 按钮回退镜像。真实 Wi-Fi、AuthKey、私有配置、联网固件、原板 Flash 备份与本地日志均不公开。`docs/` 中旧版本记录保留为历史，不代表当前版本仍提供旧接口。

## 参考与边界

- [乐鑫 HID 示例](https://github.com/espressif/esp-idf/tree/v5.4.2/examples/peripherals/usb/device/tusb_hid)
- [点灯官方 MQTT 协议](https://diandeng.tech/doc/api-mqtt)
- [点灯官方 ESP-IDF SDK](https://github.com/blinker-iot/blinker-esp-idf)
- [Apple 官方快捷键](https://support.apple.com/en-us/102650)
- [Apple 配件授权](https://support.apple.com/en-us/102282)

本工程为自行编写的原型，并非淘宝商品固件复刻。开发 VID/PID 不代表量产授权。Mac 接电不保证所有 USB 端口休眠时持续供电；供电不足时应先按实际板原理图处理，不要未经核对将外部 5V 与 Mac VBUS 并接。
