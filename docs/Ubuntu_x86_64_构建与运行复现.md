# Ubuntu x86_64 构建与运行复现

本文只描述已经在 Ubuntu 20.04.6 虚拟机验证过的路径。完整下载、失败和修复证据见《迁移执行日志》。所有依赖位于用户目录，不替换系统 Qt、GCC 或 OpenCV，也不需要 `sudo`。

## 1. 目录和版本

```text
源码：/home/d508/projects/2025_SWIRVision_Linux
环境：/home/d508/.local/share/codex-envs/400w-qt-linux-v2
编译器shim：/home/d508/.local/share/codex-tools/400w-gcc15/bin
Release：源码目录/build/x86_64-release
Debug：源码目录/build/x86_64-debug
```

主要版本为 Qt 6.7.3、Qt SerialPort 6.7.3、OpenCV 4.10.0、GCC/G++ 15.3.0。Qt SerialPort来自Qt官方同版本源码，因为conda-forge没有匹配的6.7.3模块包；不能混装6.11.1，否则Qt私有ABI和部署内容可能不一致。

## 2. 为什么需要编译器shim

隔离环境中的真实编译器名是：

```bash
x86_64-conda-linux-gnu-gcc
x86_64-conda-linux-gnu-g++
```

qmake早期探测固定调用`g++`，所以用户目录中建立两个符号链接：

```bash
mkdir -p "$HOME/.local/share/codex-tools/400w-gcc15/bin"
ln -s "$HOME/.local/share/codex-envs/400w-qt-linux-v2/bin/x86_64-conda-linux-gnu-gcc" \
  "$HOME/.local/share/codex-tools/400w-gcc15/bin/gcc"
ln -s "$HOME/.local/share/codex-envs/400w-qt-linux-v2/bin/x86_64-conda-linux-gnu-g++" \
  "$HOME/.local/share/codex-tools/400w-gcc15/bin/g++"
```

这样只满足qmake的命名约定，不修改`/usr/bin`，也不影响系统编译器。

## 3. 构建

```bash
cd /home/d508/projects/2025_SWIRVision_Linux
bash scripts/linux/build_x86_64.sh release
bash scripts/linux/build_x86_64.sh debug
```

脚本会显式设置：

- `PATH`：让qmake找到用户级`g++` shim和Qt工具；
- `PKG_CONFIG_PATH`：让`.pro`中的`opencv4`和`gl`解析到同一隔离环境；
- 独立构建目录：避免Debug与Release对象文件互相覆盖；
- `tee`日志：`qmake.log`和`build.log`可用于复盘失败。

验证产物和动态库：

```bash
file build/x86_64-release/SWIRVision
LD_LIBRARY_PATH="$HOME/.local/share/codex-envs/400w-qt-linux-v2/lib" \
  ldd build/x86_64-release/SWIRVision | tee build/x86_64-release/ldd.log
grep 'not found' build/x86_64-release/ldd.log
```

最后一个命令没有输出才表示没有缺失动态库。

## 4. 在虚拟机桌面启动

SSH中的`DISPLAY`为空是正常现象，不能直接假设显示号。先查询：

```bash
ps -eo user,pid,comm,args | grep -E 'Xorg|Xwayland' | grep -v grep
ls -la /tmp/.X11-unix
```

本次实测Xorg参数给出显示`:0`和授权文件`/run/user/1000/gdm/Xauthority`。启动Debug版本：

```bash
cd /home/d508/projects/2025_SWIRVision_Linux
DISPLAY=:0 \
XAUTHORITY=/run/user/1000/gdm/Xauthority \
  bash scripts/linux/run_x11_software.sh debug
```

受控5秒冒烟测试：

```bash
DISPLAY=:0 \
XAUTHORITY=/run/user/1000/gdm/Xauthority \
  timeout 5 bash scripts/linux/run_x11_software.sh debug
echo $?
```

退出码124表示程序运行满5秒后由`timeout`终止；它不是崩溃。退出码139表示SIGSEGV，必须用gdb定位，不能记成“启动成功”。

## 5. 已遇到的典型失败

| 现象 | 根因 | 解决方法 |
| --- | --- | --- |
| `g++: command not found` | qmake调用通用名，隔离环境只有带目标三元组的编译器名 | 使用用户级shim并把它放到`PATH`最前 |
| `GL/gl.h: No such file or directory` | 头文件已安装，但qmake生成的include路径没有环境根目录 | Unix分支声明`PKGCONFIG += opencv4 gl` |
| X11启动退出139 | 3.3 Core函数表初始化失败后仍调用`glGenVertexArrays` | 控件显式请求3.3 Core并检查`initializeOpenGLFunctions()` |
| `git: command not found` | 非交互SSH未激活隔离环境 | 显式调用`$SWIR_ENV_PREFIX/bin/git` |
| offscreen提示不支持`QOpenGLWidget` | Qt offscreen平台不提供该窗口上下文 | 只把offscreen用于事件循环验证，真实显示使用X11 |

## 6. 当前能力边界

目前证明的是Qt GUI可以在X11和Mesa软件OpenGL下运行，不要求硬件GPU加速。当前图像控件仍调用OpenGL API；“软件OpenGL”不等于“完全不使用OpenGL”。Linux USB后端和原始图像离线回放尚未完成，因此还不能宣称400W Linux功能闭环完成。
