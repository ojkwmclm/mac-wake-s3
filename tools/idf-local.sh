#!/bin/bash
# Use the toolchain prepared in this chat, without changing global shell settings.
set -euo pipefail
project_dir="$(cd "$(dirname "$0")/.." && pwd)"
workspace_dir="$(cd "$project_dir/../.." && pwd)"
local_sdk="$workspace_dir/work/esp-idf"
local_tools="$workspace_dir/work/idf-tools"
if [[ -d "$local_sdk" && -d "$local_tools/python_env/idf5.4_py3.12_env" ]]; then
    export IDF_PATH="$local_sdk"
    export IDF_TOOLS_PATH="$local_tools"
    export IDF_PYTHON_ENV_PATH="$local_tools/python_env/idf5.4_py3.12_env"
    export IDF_COMPONENT_CACHE_PATH="$workspace_dir/work/component-cache"
    # This local SDK contains the submodules needed by this minimal project.
    export IDF_SKIP_CHECK_SUBMODULES=1
    export PATH="$IDF_PYTHON_ENV_PATH/bin:$local_tools/tools/cmake/3.30.2/CMake.app/Contents/bin:$local_tools/tools/ninja/1.12.1:$local_tools/tools/xtensa-esp-elf/esp-14.2.0_20241119/xtensa-esp-elf/bin:$local_tools/tools/esp32ulp-elf/2.38_20240113/esp32ulp-elf/bin:$PATH"
elif [[ -z "${IDF_PATH:-}" ]]; then
    echo '未找到本地工具链。请先按 README 安装并导出 ESP-IDF v5.4.2。' >&2
    exit 1
fi
cd "$project_dir"
exec python "$IDF_PATH/tools/idf.py" "$@"
