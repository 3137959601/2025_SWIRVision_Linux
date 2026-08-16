#!/usr/bin/env bash
set -euo pipefail

build_type="${1:-debug}"
case "$build_type" in
    release|debug) ;;
    *)
        echo "用法：$0 [release|debug]" >&2
        exit 2
        ;;
esac

script_dir="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
repo_dir="$(CDPATH= cd -- "$script_dir/../.." && pwd)"
env_prefix="${SWIR_ENV_PREFIX:-$HOME/.local/share/codex-envs/400w-qt-linux-v2}"
binary="$repo_dir/build/x86_64-$build_type/SWIRVision"
evidence_dir="$repo_dir/build/libusb-enumeration-test"

mkdir -p "$evidence_dir"
"$repo_dir/scripts/linux/build_x86_64.sh" "$build_type" \
    >"$evidence_dir/build.log" 2>&1

export LD_LIBRARY_PATH="$env_prefix/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export QT_QPA_PLATFORM=offscreen

"$binary" --usb-list 0e0f:0006 \
    >"$evidence_dir/vmware-keyboard.log" 2>&1
grep -Eq 'USB_LIST_COUNT vid=0e0f pid=0006 count=[1-9][0-9]*' \
    "$evidence_dir/vmware-keyboard.log"

"$binary" --usb-list 706d:807c \
    >"$evidence_dir/t630-default.log" 2>&1
grep -Eq 'USB_LIST_COUNT vid=706d pid=807c count=[0-9]+' \
    "$evidence_dir/t630-default.log"

set +e
"$binary" --usb-list invalid \
    >"$evidence_dir/invalid-id.log" 2>&1
invalid_exit=$?
set -e
if [[ "$invalid_exit" -ne 2 ]]; then
    echo "非法VID:PID退出码错误：期望2，实际$invalid_exit" >&2
    exit 3
fi

ldd "$binary" >"$evidence_dir/ldd.log"
grep -q 'libusb-1.0.so' "$evidence_dir/ldd.log"

cat "$evidence_dir/vmware-keyboard.log"
cat "$evidence_dir/t630-default.log"
echo "libusb只读枚举回归测试全部通过。证据目录：$evidence_dir"
