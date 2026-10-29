#!/usr/bin/env bash
set -euo pipefail
project_dir="$(cd "$(dirname "$0")/.." && pwd)"
test_dir="$(mktemp -d)"
trap 'rm -rf "$test_dir"' EXIT
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
  -I "$project_dir/tools/health-host-stubs" -I "$project_dir/main" \
  "$project_dir/tools/test_health_supervisor.c" "$project_dir/main/health.c" \
  -o "$test_dir/test-health"
"$test_dir/test-health"
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -DCONFIG_WAKE_HEALTH_FAULT_TEST=1 \
  -I "$project_dir/tools/health-host-stubs" -I "$project_dir/main" \
  "$project_dir/tools/test_health_fault.c" "$project_dir/main/health.c" \
  -o "$test_dir/test-fault"
"$test_dir/test-fault"
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -DCONFIG_WAKE_HEALTH_RESET=1 \
  -I "$project_dir/tools/health-host-stubs" -I "$project_dir/main" \
  "$project_dir/tools/test_health_watchdog.c" "$project_dir/main/health.c" \
  -o "$test_dir/test-watchdog"
"$test_dir/test-watchdog"
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -I "$project_dir/main" \
  "$project_dir/tools/test_health_reset_policy.c" -o "$test_dir/test-reset-policy"
"$test_dir/test-reset-policy"
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
  -DCONFIG_WAKE_HEALTH_RESET=1 -DCONFIG_WAKE_HEALTH_RESET_TEST=1 \
  -I "$project_dir/tools/health-host-stubs" -I "$project_dir/main" \
  "$project_dir/tools/test_health_reset_once.c" "$project_dir/main/health.c" \
  -o "$test_dir/test-once"
for mode in 0 1 2 3; do "$test_dir/test-once" "$mode"; done
