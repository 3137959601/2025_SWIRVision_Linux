#!/usr/bin/env bash
set -euo pipefail

build_type="${1:-release}"
case "$build_type" in
    release|debug) ;;
    *)
        echo "用法：DISPLAY=:0 XAUTHORITY=/path/to/Xauthority $0 [release|debug]" >&2
        exit 2
        ;;
esac

if [[ -z "${DISPLAY:-}" || -z "${XAUTHORITY:-}" ]]; then
    echo "必须显式设置 DISPLAY 和 XAUTHORITY，脚本不会猜测桌面会话。" >&2
    echo "示例：DISPLAY=:0 XAUTHORITY=/run/user/$(id -u)/gdm/Xauthority $0 $build_type" >&2
    exit 3
fi
if [[ ! -r "$XAUTHORITY" ]]; then
    echo "X11授权文件不可读：$XAUTHORITY" >&2
    exit 4
fi

script_dir="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
repo_dir="$(CDPATH= cd -- "$script_dir/../.." && pwd)"
env_prefix="${SWIR_ENV_PREFIX:-$HOME/.local/share/codex-envs/400w-qt-linux-v2}"
binary="$repo_dir/build/x86_64-$build_type/SWIRVision"

if [[ ! -x "$binary" ]]; then
    echo "可执行文件不存在：$binary" >&2
    echo "请先运行 scripts/linux/build_x86_64.sh $build_type" >&2
    exit 5
fi

export LD_LIBRARY_PATH="$env_prefix/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export QT_OPENGL=software
export LIBGL_ALWAYS_SOFTWARE=1

echo "显示会话：$DISPLAY"
echo "启动程序：$binary"
exec "$binary"
