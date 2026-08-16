#!/usr/bin/env bash
set -euo pipefail

script_dir="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
repo_dir="$(CDPATH= cd -- "$script_dir/../.." && pwd)"
toolchain_dir="${SWIR_TOOLCHAIN_DIR:-$HOME/.local/share/codex-tools/400w-gcc15/bin}"
cxx="${SWIR_CXX:-$toolchain_dir/g++}"
test_dir="$repo_dir/build/usb-frame-pipeline-test"
binary="$test_dir/usb_frame_pipeline_test"

if [[ ! -x "$cxx" ]]; then
    echo "找不到可执行C++编译器：$cxx" >&2
    echo "可通过SWIR_CXX显式指定编译器。" >&2
    exit 2
fi

mkdir -p "$test_dir"
"$cxx" -std=c++17 -O2 -g -Wall -Wextra -Wpedantic -Werror -pthread \
    -I"$repo_dir" \
    "$repo_dir/common/usb_frame_pipeline.cpp" \
    "$repo_dir/tests/usb_frame_pipeline_test.cpp" \
    -o "$binary" \
    >"$test_dir/build.log" 2>&1

"$binary" | tee "$test_dir/run.log"
echo "协议层测试证据目录：$test_dir"
