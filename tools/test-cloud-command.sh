#!/usr/bin/env bash
set -euo pipefail
: "${IDF_PATH:?请先安装并导出 ESP-IDF v5.4.2}"
project_dir="$(cd "$(dirname "$0")/.." && pwd)"
test_dir="$(mktemp -d)"
trap 'rm -rf "$test_dir"' EXIT
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
  -I "$project_dir/main" -I "$IDF_PATH/components/json/cJSON" \
  "$project_dir/tools/test_cloud_command.c" "$project_dir/main/cloud_command.c" \
  "$IDF_PATH/components/json/cJSON/cJSON.c" -lm -o "$test_dir/test-cloud-command"
"$test_dir/test-cloud-command"
