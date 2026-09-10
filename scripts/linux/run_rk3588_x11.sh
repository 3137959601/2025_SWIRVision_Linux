#!/usr/bin/env bash
set -euo pipefail

build_type="${1:-debug}"
shift || true

script_dir="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
repo_dir="$(CDPATH= cd -- "$script_dir/../.." && pwd)"
binary="$repo_dir/build/rk3588-$build_type/SWIRVision"

export DISPLAY="${DISPLAY:-:0}"
export XAUTHORITY="${XAUTHORITY:-/var/run/lightdm/root/:0}"
export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}"
export LD_LIBRARY_PATH="/usr/lib/aarch64-linux-gnu"
export QT_PLUGIN_PATH="/usr/lib/aarch64-linux-gnu/qt5/plugins"
export QT_QPA_PLATFORM_PLUGIN_PATH="$QT_PLUGIN_PATH/platforms"
export QT_OPENGL="${QT_OPENGL:-software}"
export LIBGL_ALWAYS_SOFTWARE="${LIBGL_ALWAYS_SOFTWARE:-1}"
unset QT_ROOT QTDIR

test -x "$binary" || {
    echo "可执行文件不存在，请先运行build_rk3588_native.sh。" >&2
    exit 3
}
test -r "$XAUTHORITY" || {
    echo "X11认证文件不可读：$XAUTHORITY" >&2
    exit 4
}

exec "$binary" "$@"
