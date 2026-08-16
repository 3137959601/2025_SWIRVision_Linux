#!/usr/bin/env bash
set -euo pipefail

script_dir="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
repo_dir="$(CDPATH= cd -- "$script_dir/../.." && pwd)"
env_prefix="${SWIR_ENV_PREFIX:-$HOME/.local/share/codex-envs/400w-qt-linux-v2}"
compiler_shim="${SWIR_COMPILER_SHIM:-$HOME/.local/share/codex-tools/400w-gcc15/bin}"
evidence_dir="$repo_dir/build/qt-protocol-tests"

for required in "$env_prefix/bin/qmake6" "$env_prefix/bin/make" \
                "$compiler_shim/g++"; do
    if [[ ! -e "$required" ]]; then
        echo "缺少测试依赖：$required" >&2
        exit 2
    fi
done

export PATH="$compiler_shim:$env_prefix/bin:$PATH"
export LD_LIBRARY_PATH="$env_prefix/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export PKG_CONFIG_PATH="$env_prefix/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"

run_qmake_test() {
    local name="$1"
    local project="$2"
    local build_dir="$evidence_dir/$name"
    mkdir -p "$build_dir"
    (
        cd "$build_dir"
        "$env_prefix/bin/qmake6" "$project" CONFIG+=debug CONFIG-=release \
            >qmake.log 2>&1
        "$env_prefix/bin/make" -j"${SWIR_BUILD_JOBS:-$(nproc)}" \
            >build.log 2>&1
        "./$name" >run.log 2>&1
        cat run.log
    )
}

run_qmake_test uartprotocol_test "$repo_dir/tests/uartprotocol_test.pro"
run_qmake_test linearstretchmath_test "$repo_dir/tests/linearstretchmath_test.pro"
run_qmake_test serialworker_pty_test "$repo_dir/tests/serialworker_pty_test.pro"

echo "Qt协议与伪终端串口测试全部通过。证据目录：$evidence_dir"
