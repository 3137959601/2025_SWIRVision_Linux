#!/usr/bin/env bash
set -euo pipefail

build_type="${1:-release}"
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
compiler_shim="${SWIR_COMPILER_SHIM:-$HOME/.local/share/codex-tools/400w-gcc15/bin}"
build_dir="$repo_dir/build/x86_64-$build_type"

for required in \
    "$env_prefix/bin/qmake6" \
    "$env_prefix/bin/make" \
    "$compiler_shim/g++" \
    "$repo_dir/SWIRVision.pro"; do
    if [[ ! -e "$required" ]]; then
        echo "缺少必需文件：$required" >&2
        echo "请先按 docs/Ubuntu_x86_64_构建与运行复现.md 准备用户级环境。" >&2
        exit 3
    fi
done

mkdir -p "$build_dir"
cd "$build_dir"

export PATH="$compiler_shim:$env_prefix/bin:$PATH"
export PKG_CONFIG_PATH="$env_prefix/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"

if [[ "$build_type" == "debug" ]]; then
    qmake_config=(CONFIG+=debug CONFIG-=release)
else
    qmake_config=(CONFIG+=release CONFIG-=debug)
fi

echo "源码目录：$repo_dir"
echo "构建目录：$build_dir"
echo "环境前缀：$env_prefix"
"$env_prefix/bin/qmake6" "$repo_dir/SWIRVision.pro" "${qmake_config[@]}" 2>&1 | tee qmake.log
"$env_prefix/bin/make" -j"${SWIR_BUILD_JOBS:-$(nproc)}" 2>&1 | tee build.log

test -x "$build_dir/SWIRVision"
echo "构建成功：$build_dir/SWIRVision"
