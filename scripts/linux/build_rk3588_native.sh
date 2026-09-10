#!/usr/bin/env bash
set -euo pipefail

build_type="${1:-debug}"
case "$build_type" in
    debug|release) ;;
    *)
        echo "用法：$0 [debug|release]" >&2
        exit 2
        ;;
esac

script_dir="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
repo_dir="$(CDPATH= cd -- "$script_dir/../.." && pwd)"
build_dir="$repo_dir/build/rk3588-$build_type"

# 登录环境默认注入厂商Qt 5.15.8；本工程固定使用Debian Qt 5.15.2。
export LD_LIBRARY_PATH="/usr/lib/aarch64-linux-gnu"
export QT_PLUGIN_PATH="/usr/lib/aarch64-linux-gnu/qt5/plugins"
export QT_QPA_PLATFORM_PLUGIN_PATH="$QT_PLUGIN_PATH/platforms"
unset QT_ROOT QTDIR

if [[ "$(uname -m)" != "aarch64" ]]; then
    echo "该脚本必须在RK3588的AArch64 Linux上运行。" >&2
    exit 3
fi

qt_version="$(qmake -query QT_VERSION 2>/dev/null || true)"
if [[ "$qt_version" != 5.* ]]; then
    echo "需要Qt 5 qmake，当前版本：${qt_version:-未找到}" >&2
    exit 4
fi

for module in Qt5Widgets Qt5OpenGL Qt5SerialPort opencv4 libusb-1.0; do
    pkg-config --exists "$module" || {
        echo "缺少开发模块：$module" >&2
        exit 5
    }
done

if [[ "$build_type" == "debug" ]]; then
    qmake_config=(CONFIG+=debug CONFIG-=release)
else
    qmake_config=(CONFIG+=release CONFIG-=debug)
fi

mkdir -p "$build_dir"
cd "$build_dir"
qmake "$repo_dir/SWIRVision.pro" "${qmake_config[@]}"
make -j"${SWIR_BUILD_JOBS:-4}"

test -x "$build_dir/SWIRVision"
echo "RK3588构建成功：$build_dir/SWIRVision"
