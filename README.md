# Mac Wake S3 原型 v0.4

基于 ESP32-S3-DevKitC-1 N16R8 的 Mac 远程睡眠／唤醒器，支持点灯 APP 外网控制、局域网页面和板载 BOOT 唤醒。目标实机为 MacBook Air M5：合盖、接电、连接外接显示器。用户已实测外网唤醒、睡眠和远程桌面恢复成功；隔夜及断网／掉电恢复尚未验收。

点灯按钮的数据键名：**唤醒 `btn-wakeup`，睡眠 `btn-sleep`**。按钮显示名称由 APP 自行设置。

仓库包含完整应用源码、依赖锁、分区表、配置工具、测试源码、操作文档与不含联网凭据的 v0.1 按钮回退镜像。ESP-IDF 工具链按下方步骤安装；组件由 IDF Component Manager 根据依赖锁下载。真实 Wi-Fi 密码、AuthKey、私有配置、联网固件和原板 Flash 备份不公开。


v0.3 新增点灯 APP 云端控制，使用官方认证接口与 MQTT over WSS。按钮及局域网唤醒已由用户实机确认成功；点灯版已完成编译、15 项指令解析边界测试及手机移动数据下唤醒和远程桌面连接实测，云端固件实机验收见 `docs/点灯版-20261022.md`。

实际 Wi-Fi 配置和联网构建产物保存在本聊天工作区的私有 work 目录，不包含在公开源代码包中。`firmware/` 中保留的 v0.1 按钮镜像仍是无 Wi-Fi 的回退版本。

目标设备：用户提供的 13 英寸 M5 MacBook Air；合盖、持续接电、连接外接显示器。用户已确认普通键盘可以稳定唤醒。截图中的系统版本为用户提供信息，尚未独立核实。

本工程实现 ESP32-S3 USB HID 键盘身份、USB Remote Wakeup、BOOT 按钮触发、Wi-Fi 局域网手机页面及状态日志。默认不发送字符，不输入密码。支持点灯云端指令；远程桌面服务与 Mac 在线确认需另行验证。

## v0.4 睡眠按钮

在点灯 APP 新增按钮，名称“睡眠”、数据键名 `btn-sleep`，点击发送 `{"btn-sleep":"tap"}`（也接受 `press`，忽略 `pressup`、`on`、`off`）。通过第二个 USB HID 媒体控制接口配合键盘发送 Option＋Command＋媒体推出键，使用 Apple 官方睡眠快捷键。原键盘接口及 `btn-wakeup` 唤醒键保留。

仅由用户主动点击触发；固件不自动测试睡眠。快捷键发出后不能据此认定 Mac 已睡眠，需观察屏幕及远程桌面断开；再测试原唤醒按钮恢复连接。Mac 已挂起时不发送睡眠快捷键，也不先唤醒它。首次升级后可能需要重新批准 USB 配件。

发送后释放媒体键和修饰键；若 USB 先挂起，则总线恢复后只补发释放报告，不重发睡眠快捷键。此时仍允许唤醒请求，避免等待释放而堵住唤醒入口。

依据：[Apple 官方快捷键](https://support.apple.com/en-us/102650)。v0.4 已由用户在目标 Mac 实测确认：睡眠、再次唤醒和远程桌面恢复均正常。隔夜及断网恢复仍待测试。

## 文件与工具链

- ESP-IDF **v5.4.2**；esp_tinyusb **1.7.6~1**；TinyUSB **0.18.0~4**。使用同一组版本复现，不要直接替换为最新 API。
- 芯片目标 ESP32-S3；Flash 配置 16 MB，未启用 PSRAM，任务不需要使用板载 8 MB PSRAM。
- `main/main.c`：固件；`main/index.html`：嵌入板端的手机页面。
- `docs/验收记录.md`：实机测试表；`docs/验证状态.md`：本次完成边界。

## 1. 连接

1. 用支持数据的 USB 线，把开发板的 **原生 USB / OTG** 接口连接 Mac。不要把 USB 转串口接口当作 HID 接口。第三方板的丝印及接口接线需确认。
2. 调试日志走 UART0；若板上提供独立 USB 转串口接口，可用第二根线连接到日志电脑。仅原生 USB 接口不会出现本工程的串口日志。
3. Mac 解锁时批准“Mac Wake S3”配件连接，然后再合盖并进入睡眠。
4. 首轮按普通键盘已经成功的同一连接路径测试，记录是否使用扩展坞。为了排除扩展坞影响，也可加测直连。
5. 本固件不负责电源隔离。不要未经核对将外部 5V 与 Mac USB VBUS 并接。若睡眠导致板子掉电，先按实际板原理图解决供电。Mac 已连接电源并不等于所有 USB 端口一定持续供电。

## 2. 构建

在本次聊天原工作区，已准备独立工具链，可使用 `bash tools/idf-local.sh` 代替下文的 `idf.py`。例如 `bash tools/idf-local.sh build`。复制工程到其他位置后，按下文标准方式导出 ESP-IDF。

在已导出的 ESP-IDF v5.4.2 环境中打开本目录：

```sh
idf.py set-target esp32s3
idf.py build
```

这会生成“仅按钮”测试固件。上电后正常启动，短按 BOOT 请求唤醒；不要同时按 RESET，也不要在复位期间按住 BOOT，否则会进入下载模式。

若没有环境，按官方流程安装指定版本并导出：

```sh
git clone --branch v5.4.2 --recursive https://github.com/espressif/esp-idf.git
cd esp-idf
./install.sh esp32s3
. ./export.sh
```

安装会下载编译工具及依赖。官方入口：https://docs.espressif.com/projects/esp-idf/en/v5.4.2/esp32s3/get-started/index.html

## 3. 配置 Wi-Fi 与手机口令

在工程目录运行交互式配置，密码不会作为命令参数或输出打印：

```sh
python3 tools/configure.py
idf.py -D SDKCONFIG=sdkconfig.local build
```

只支持 2.4 GHz Wi-Fi。`sdkconfig.local` 内含 Wi-Fi 密码和随机控制口令，仅本机保存。固件也包含这些配置，不要把带真实凭据的源码、构建目录或固件转发他人。

需要修改配置时：

```sh
idf.py -D SDKCONFIG=sdkconfig.local menuconfig
```

打开 `Mac Wake S3` 菜单。口令须为 32–128 字符，推荐保留随机值。`Send a Shift…` 默认关闭；如果日志显示 `host_bus_active` 或只发 USB 恢复不能达到目标，可在人工观察下启用并重新构建。Shift 会产生真实键盘输入，不是自动登录功能。

## 4. 烧录与日志

先确认实际串口，再把下方 `实际串口` 替换为设备端口：

```sh
idf.py -D SDKCONFIG=sdkconfig.local -p 实际串口 flash monitor
```

仅按钮构建时去掉 `-D SDKCONFIG=sdkconfig.local`。若使用 USB 转串口烧录，它也可用于 `monitor`；原生 USB 下载后会切换成 HID，日志需另走 UART0。烧录会替换开发板的应用及分区表；若板上有需保留的原固件，先备份。

日志示例（示例地址，不是已发现的设备）：`Phone page: http://192.168.1.123/`。手机连接可访问该板的同一局域网，打开日志中的实际地址，输入 `sdkconfig.local` 的控制口令，再点击“发送唤醒请求”。建议在路由器给板子保留 DHCP 地址。

## 5. 结果判读

| 结果 | 含义 / 下一步 |
|---|---|
| `usb_detached` | 未识别、线/口错误、未授权，或实际发生了 USB 断开 |
| `remote_wakeup_denied` | 当前挂起周期未获主机授权；保留日志，不要盲目重试 |
| `resume_signal_rejected` | USB 状态变化或协议栈未接受信号 |
| `resume_timeout` | 信号发出，但 5 秒内未观察到总线恢复 |
| `usb_resumed_unconfirmed` | 总线恢复；仍需在远程桌面客户端确认 Mac 可用 |
| `host_bus_active` | 总线已活动；不能据此认定 Mac 已醒来。可单独评估 Shift 选项 |
| `shift_sent_unconfirmed` | 按键与释放报告已提交；Mac 可用性仍待人工确认 |
| `key_release_pending` | 释放报告尚未提交成功，后台持续重试；保持连接并查看日志 |

接口：`GET /api/status` 公开板端 USB 状态；`POST /api/wake` 要求 `Authorization: Bearer <口令>`，返回 **202 已接受**，不是成功唤醒。每次只处理一个请求，最短间隔 3 秒。页面不保存口令；USB 状态也不等于系统睡眠状态。

## 6. 点灯 APP 外网控制

1. 在点灯 APP 创建“独立设备”，选择网络接入，取得该设备 AuthKey。
2. 配置工具可输入 AuthKey，也可在 `menuconfig → Mac Wake S3` 中填写。AuthKey 不要发到聊天或公开仓库。
3. 添加按钮，数据键名必须为 `btn-wakeup`。点击时应发送 `{"btn-wakeup":"tap"}`。固件也接受同键的 `on` / `press`，忽略 `off`、任意文本、其他账号、保留消息和带重复标志消息。
4. 烧录启动后，先等待 Wi-Fi、时间校准、认证和云连接。局域网页面显示“点灯云端：在线”后，再测试 APP。
5. 外网验收时关闭手机 Wi-Fi，使用移动数据；让 Mac 合盖睡眠后点击按钮，确认 Mac 恢复及远程桌面可连接。APP 在线与 USB 恢复均不等于远程桌面已可用。

开发板主动连接云端，无需在路由器开放端口。局域网 HTTP 页面不要直接映射到公网。一次只处理一个唤醒请求，最短间隔 3 秒；云端状态回传最短 1.5 秒。

v0.3 使用自定义分区表，应用分区 3 MB；升级需同时写入对应分区表。认证和 WSS 均校验证书，启动需先通过 NTP 校时。实测时官方 `mqtts` 端口证书已过期，因此选用官方认证返回且证书校验通过的 `wss` 通道。没有跳过证书验证。

验收顺序：按钮短睡眠 → 手机局域网 → 点灯上线 → 手机流量唤醒 → 隔夜与断网恢复。成功标准包括目标 Mac 恢复远程桌面连接。

## 指令解析测试

已安装并导出 ESP-IDF 后，在仓库根目录运行：

```sh
bash tools/test-cloud-command.sh
```

测试涵盖新按钮键名、账号校验、保留／重复消息、混合指令、松开动作、畸形 JSON 和大小上限，不会连接硬件或发送睡眠指令。

## 参考与边界

该工程是根据公开 USB 协议接口编写的新原型，不是所分享淘宝商品的固件复刻。无商品固件、抓包或拆机证据。

- [乐鑫 HID 示例 v5.4.2](https://github.com/espressif/esp-idf/tree/v5.4.2/examples/peripherals/usb/device/tusb_hid)
- [esp_tinyusb 固定版本](https://components.espressif.com/components/espressif/esp_tinyusb/versions/1.7.6~1)
- [相近开源项目](https://github.com/nonoo/esp-remote-wakeup)，作为架构参考，未使用其代码。
- [Apple 配件授权](https://support.apple.com/en-us/102282)，首次和长期锁定测试需考虑。

开发用 VID/PID 不能直接视为商品化身份授权；量产前另行处理 USB 身份、供电/挂起功耗及兼容性合规。

- [点灯 MQTT 官方协议](https://diandeng.tech/doc/api-mqtt)
- [点灯官方 ESP-IDF SDK](https://github.com/blinker-iot/blinker-esp-idf)，用于协议参照；本工程使用原有 ESP-IDF 5.4.2 实现接入。

联网兼容：实机发现 DHCP 分配了 IP 但未提供 DNS（0.0.0.0）。固件仅在主 DNS 为空时配置 223.5.5.5 与 1.1.1.1；保留 DHCP 已提供的解析器。启用两个 NTP 服务器，以校准 TLS 所需的系统时间。
