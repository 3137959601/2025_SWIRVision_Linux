#!/usr/bin/env bash
set -euo pipefail

build_type="${1:-debug}"
script_dir="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
repo_dir="$(CDPATH= cd -- "$script_dir/../.." && pwd)"
binary="$repo_dir/build/rk3588-$build_type/SWIRVision"
generator="$script_dir/generate_synthetic_raw.py"
runner="$script_dir/run_rk3588_x11.sh"
test_dir="$repo_dir/build/rk3588-offline"

test -x "$binary" || {
    echo "请先运行：bash scripts/linux/build_rk3588_native.sh $build_type" >&2
    exit 3
}

mkdir -p "$test_dir"

export LD_LIBRARY_PATH="/usr/lib/aarch64-linux-gnu"
export QT_PLUGIN_PATH="/usr/lib/aarch64-linux-gnu/qt5/plugins"
export QT_QPA_PLATFORM_PLUGIN_PATH="$QT_PLUGIN_PATH/platforms"
unset QT_ROOT QTDIR

QT_QPA_PLATFORM=offscreen "$binary" --usb-list 706d:807c \
    >"$test_dir/usb-list.log" 2>&1

python3 "$generator" --width 320 --height 240 --frames 4 \
    --output "$test_dir/synthetic_320x240_4f.raw" \
    --last-frame-output "$test_dir/expected_small_last.raw"
timeout 60 "$runner" "$build_type" \
    --offline "$test_dir/synthetic_320x240_4f.raw" \
    --offline-width 320 --offline-height 240 --offline-fps 20 \
    --offline-frames 4 --offline-save "$test_dir/application_small_last.raw" \
    >"$test_dir/replay-small.log" 2>&1
cmp "$test_dir/expected_small_last.raw" "$test_dir/application_small_last.raw"

python3 "$generator" --width 2048 --height 2048 --frames 3 \
    --output "$test_dir/synthetic_2048x2048_3f.raw" \
    --last-frame-output "$test_dir/expected_400w_last.raw"
timeout 60 "$runner" "$build_type" \
    --offline "$test_dir/synthetic_2048x2048_3f.raw" \
    --offline-width 2048 --offline-height 2048 --offline-fps 5 \
    --offline-frames 3 --offline-save "$test_dir/application_400w_last.raw" \
    >"$test_dir/replay-400w.log" 2>&1
cmp "$test_dir/expected_400w_last.raw" "$test_dir/application_400w_last.raw"

timeout 60 "$runner" "$build_type" \
    --offline "$test_dir/synthetic_2048x2048_3f.raw" \
    --offline-width 2048 --offline-height 2048 --offline-fps 5 \
    --offline-frames 1 --offline-save "$test_dir/application_400w_frame.png" \
    >"$test_dir/save-400w-png.log" 2>&1
test "$(grep -c OFFLINE_TEST_FRAME "$test_dir/save-400w-png.log")" -eq 1
file "$test_dir/application_400w_frame.png" \
    | grep -q "2048 x 2048, 16-bit grayscale"

echo "RK3588离线回放测试全部通过：$test_dir"
