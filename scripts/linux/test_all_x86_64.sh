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

script_dir="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
repo_dir="$(CDPATH= cd -- "$script_dir/../.." && pwd)"

"$repo_dir/scripts/linux/build_x86_64.sh" "$build_type"
"$repo_dir/scripts/linux/test_usb_frame_pipeline.sh"
"$repo_dir/scripts/linux/test_qt_protocols.sh"
"$repo_dir/scripts/linux/test_libusb_enumeration.sh" "$build_type"
"$repo_dir/scripts/linux/test_offline_replay.sh" "$build_type"

echo "Ubuntu x86_64无模组软件回归全部通过。"
