#!/usr/bin/env bash
set -euo pipefail
project_dir="$(cd "$(dirname "$0")/.." && pwd)"
test_dir="$(mktemp -d)"
trap 'rm -rf "$test_dir"' EXIT
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -I "$project_dir/main" \
  "$project_dir/tools/test_recovery_policy.c" -o "$test_dir/test-policy"
"$test_dir/test-policy"
