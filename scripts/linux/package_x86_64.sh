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
build_dir="$repo_dir/build/x86_64-$build_type"
package_root="$repo_dir/build/package"
package_name="SWIRVision-linux-x86_64"
package_dir="$package_root/$package_name"
archive="$package_root/$package_name.tar.gz"
qt_plugin_root="$env_prefix/lib/qt6/plugins"

"$script_dir/build_x86_64.sh" "$build_type"

for required in "$build_dir/SWIRVision" "$qt_plugin_root/platforms/libqxcb.so"; do
    if [[ ! -e "$required" ]]; then
        echo "打包所需文件不存在：$required" >&2
        exit 3
    fi
done

# 删除范围严格限定为仓库 build/package 下的固定产物；源码和已有环境均不触碰。
case "$package_dir" in
    "$repo_dir"/build/package/"$package_name") ;;
    *) echo "拒绝清理非预期目录：$package_dir" >&2; exit 4 ;;
esac
rm -rf -- "$package_dir"
rm -f -- "$archive"
mkdir -p "$package_dir/bin" "$package_dir/lib" "$package_dir/plugins"
cp -L -- "$build_dir/SWIRVision" "$package_dir/bin/SWIRVision"

# X11显示、无人值守探针、OpenGL上下文和图像保存需要这些插件组。
plugin_groups=(platforms xcbglintegrations imageformats platforminputcontexts)
for group in "${plugin_groups[@]}"; do
    if [[ -d "$qt_plugin_root/$group" ]]; then
        mkdir -p "$package_dir/plugins/$group"
        find "$qt_plugin_root/$group" -maxdepth 1 -type f -name '*.so' -print0 |
            while IFS= read -r -d '' plugin; do
                cp -L -- "$plugin" "$package_dir/plugins/$group/$(basename -- "$plugin")"
            done
    fi
done

declare -a queue=("$package_dir/bin/SWIRVision")
while IFS= read -r -d '' plugin; do
    queue+=("$plugin")
done < <(find "$package_dir/plugins" -type f -name '*.so' -print0)

declare -A scanned=()
index=0
while (( index < ${#queue[@]} )); do
    target="${queue[$index]}"
    ((index += 1))
    [[ -n "${scanned[$target]:-}" ]] && continue
    scanned["$target"]=1

    while IFS= read -r dependency; do
        [[ "$dependency" == "$env_prefix"/* ]] || continue
        soname="$(basename -- "$dependency")"
        destination="$package_dir/lib/$soname"
        if [[ ! -e "$destination" ]]; then
            cp -L -- "$dependency" "$destination"
            queue+=("$destination")
        fi
    done < <(
        LD_LIBRARY_PATH="$package_dir/lib:$env_prefix/lib" ldd "$target" 2>/dev/null |
            awk '/=> \/[^ ]+/ { print $3 }'
    )
done

cat > "$package_dir/qt.conf" <<'EOF'
[Paths]
Plugins = plugins
EOF

cat > "$package_dir/运行_SWIRVision.sh" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
package_dir="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
export LD_LIBRARY_PATH="$package_dir/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export QT_PLUGIN_PATH="$package_dir/plugins"
export QT_QPA_PLATFORM_PLUGIN_PATH="$package_dir/plugins/platforms"
# 默认使用软件OpenGL以适配虚拟机；真机可在命令前设置 SWIR_OPENGL_MODE=desktop。
case "${SWIR_OPENGL_MODE:-software}" in
    software)
        export QT_OPENGL=software
        export LIBGL_ALWAYS_SOFTWARE=1
        ;;
    desktop)
        export QT_OPENGL=desktop
        unset LIBGL_ALWAYS_SOFTWARE || true
        ;;
    *)
        echo "SWIR_OPENGL_MODE只能是 software 或 desktop。" >&2
        exit 2
        ;;
esac
cd "$package_dir"
exec "$package_dir/bin/SWIRVision" "$@"
EOF
chmod +x "$package_dir/运行_SWIRVision.sh"

cat > "$package_dir/README-运行说明.txt" <<'EOF'
400W SWIRVision Ubuntu x86_64运行包

1. 在Ubuntu图形桌面终端执行：
   ./运行_SWIRVision.sh
2. 只读枚举T630（默认VID:PID）：
   ./运行_SWIRVision.sh --usb-list 706d:807c
3. 默认使用Mesa软件OpenGL。需要尝试桌面硬件OpenGL时：
   SWIR_OPENGL_MODE=desktop ./运行_SWIRVision.sh
4. 本包不代表T630真机已经验收；USB实机、串口实机和持续吞吐仍需连接模组测试。
EOF

LD_LIBRARY_PATH="$package_dir/lib" ldd "$package_dir/bin/SWIRVision" > "$package_dir/ldd.txt"
if grep -q 'not found' "$package_dir/ldd.txt"; then
    echo "运行包仍有缺失动态库：" >&2
    grep 'not found' "$package_dir/ldd.txt" >&2
    exit 5
fi

(
    cd "$package_root"
    tar -czf "$archive" "$package_name"
)
sha256sum "$archive" | tee "$archive.sha256"
echo "运行目录：$package_dir"
echo "压缩包：$archive"
echo "包内动态库数量：$(find "$package_dir/lib" -maxdepth 1 -type f | wc -l)"
