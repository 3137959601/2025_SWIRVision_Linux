#!/usr/bin/env bash
set -euo pipefail

build_type="${1:-debug}"
case "$build_type" in
    release|debug) ;;
    *)
        echo "用法：DISPLAY=:0 XAUTHORITY=/path/to/Xauthority $0 [release|debug]" >&2
        exit 2
        ;;
esac

if [[ -z "${DISPLAY:-}" || -z "${XAUTHORITY:-}" || ! -r "$XAUTHORITY" ]]; then
    echo "必须显式提供有效的 DISPLAY 和可读 XAUTHORITY。" >&2
    exit 3
fi

script_dir="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
repo_dir="$(CDPATH= cd -- "$script_dir/../.." && pwd)"
test_dir="$repo_dir/build/offline-regression"
run_script="$repo_dir/scripts/linux/run_x11_software.sh"
generator="$repo_dir/scripts/linux/generate_synthetic_raw.py"

mkdir -p "$test_dir"
"$repo_dir/scripts/linux/build_x86_64.sh" "$build_type" \
    >"$test_dir/build.log" 2>&1

run_expected_exit() {
    local expected="$1"
    local log_file="$2"
    shift 2
    set +e
    timeout 20 "$run_script" "$build_type" "$@" >"$log_file" 2>&1
    local actual=$?
    set -e
    if [[ "$actual" -ne "$expected" ]]; then
        echo "退出码不符：期望$expected，实际$actual，日志：$log_file" >&2
        sed -n '1,160p' "$log_file" >&2
        exit 10
    fi
}

python3 "$generator" --width 320 --height 240 --frames 4 \
    --output "$test_dir/synthetic_320x240_4f.raw" \
    --last-frame-output "$test_dir/expected_last.raw" \
    >"$test_dir/generate-small.log" 2>&1

run_expected_exit 0 "$test_dir/replay-small.log" \
    --offline "$test_dir/synthetic_320x240_4f.raw" \
    --offline-width 320 --offline-height 240 --offline-fps 20 \
    --offline-frames 4 --offline-save "$test_dir/application_last.raw"
cmp "$test_dir/expected_last.raw" "$test_dir/application_last.raw"

truncate -s 100 "$test_dir/short.raw"
cp "$test_dir/expected_last.raw" "$test_dir/trailing.raw"
truncate -s 153601 "$test_dir/trailing.raw"
run_expected_exit 3 "$test_dir/error-missing.log" \
    --offline "$test_dir/does-not-exist.raw" \
    --offline-width 320 --offline-height 240 --offline-frames 1
run_expected_exit 3 "$test_dir/error-short.log" \
    --offline "$test_dir/short.raw" \
    --offline-width 320 --offline-height 240 --offline-frames 1
run_expected_exit 3 "$test_dir/error-trailing.log" \
    --offline "$test_dir/trailing.raw" \
    --offline-width 320 --offline-height 240 --offline-frames 1

run_expected_exit 2 "$test_dir/error-width.log" \
    --offline "$test_dir/synthetic_320x240_4f.raw" \
    --offline-width 0 --offline-height 240 --offline-frames 1
run_expected_exit 2 "$test_dir/error-fps.log" \
    --offline "$test_dir/synthetic_320x240_4f.raw" \
    --offline-width 320 --offline-height 240 --offline-fps 0 --offline-frames 1

run_expected_exit 5 "$test_dir/natural-eof.log" \
    --offline "$test_dir/synthetic_320x240_4f.raw" \
    --offline-width 320 --offline-height 240 --offline-fps 50 --offline-frames 5

python3 "$generator" --width 320 --height 240 --frames 2 \
    --output "$test_dir/two_frames.raw" \
    --last-frame-output "$test_dir/expected_loop_frame6.raw" \
    >"$test_dir/generate-loop-expected.log" 2>&1
run_expected_exit 0 "$test_dir/loop-six.log" \
    --offline "$test_dir/synthetic_320x240_4f.raw" \
    --offline-width 320 --offline-height 240 --offline-fps 50 --offline-loop \
    --offline-frames 6 --offline-save "$test_dir/application_loop_frame6.raw"
cmp "$test_dir/expected_loop_frame6.raw" "$test_dir/application_loop_frame6.raw"

if [[ "${SWIR_SKIP_400W_TEST:-0}" != "1" ]]; then
    python3 "$generator" --width 2048 --height 2048 --frames 3 \
        --output "$test_dir/synthetic_2048x2048_3f.raw" \
        --last-frame-output "$test_dir/expected_400w_last.raw" \
        >"$test_dir/generate-400w.log" 2>&1
    run_expected_exit 0 "$test_dir/replay-400w.log" \
        --offline "$test_dir/synthetic_2048x2048_3f.raw" \
        --offline-width 2048 --offline-height 2048 --offline-fps 10 \
        --offline-frames 3 --offline-save "$test_dir/application_400w_last.raw"
    cmp "$test_dir/expected_400w_last.raw" "$test_dir/application_400w_last.raw"
    run_expected_exit 0 "$test_dir/save-400w-png.log" \
        --offline "$test_dir/synthetic_2048x2048_3f.raw" \
        --offline-width 2048 --offline-height 2048 --offline-fps 5 \
        --offline-frames 1 --offline-save "$test_dir/application_400w_frame.png"
    file "$test_dir/application_400w_frame.png" \
        | grep -q '2048 x 2048, 16-bit grayscale'
fi

sha256sum "$test_dir/expected_last.raw" "$test_dir/application_last.raw"
echo "离线回放回归测试全部通过。证据目录：$test_dir"
