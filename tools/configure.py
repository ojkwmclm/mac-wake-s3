#!/usr/bin/env python3
"""Write local credentials without exposing them as shell arguments or logs."""
import getpass
import json
import os
import re
from pathlib import Path

root = Path(__file__).resolve().parent.parent
path = root / "sdkconfig.local"
if path.exists():
    raise SystemExit("sdkconfig.local 已存在；保留原文件。需要更改时请自行编辑。")
ssid = input("2.4 GHz Wi-Fi 名称（留空只测试板载按钮）：").strip()
password = getpass.getpass("Wi-Fi 密码：") if ssid else ""
if len(ssid.encode()) > 32 or len(password.encode()) > 64:
    raise SystemExit("Wi-Fi 名称或密码超过长度限制。")
auth_key = getpass.getpass("点灯独立设备 AuthKey（留空禁用云端）：").strip() if ssid else ""
if auth_key and not re.fullmatch(r"[A-Za-z0-9_-]{1,128}", auth_key):
    raise SystemExit("AuthKey 格式无效。")
content = "\n".join([
    "# Local credentials. Do not share this file or firmware built with it.",
    'CONFIG_IDF_TARGET="esp32s3"',
    "CONFIG_WAKE_WIFI_SSID=" + json.dumps(ssid, ensure_ascii=False),
    "CONFIG_WAKE_WIFI_PASSWORD=" + json.dumps(password, ensure_ascii=False),
    "CONFIG_WAKE_BLINKER_KEY=" + json.dumps(auth_key),
    "",
])
fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
with os.fdopen(fd, "w") as f:
    f.write(content)
print("已写入 sdkconfig.local（仅当前用户可读写）。")
print("构建时使用：idf.py -D SDKCONFIG=sdkconfig.local build")
