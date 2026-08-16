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

目前已经证明Qt GUI可以在X11和Mesa软件OpenGL下运行，不要求硬件GPU加速；也已经用合成RAW完成“文件读取—共享原始帧—`ImageProcessor`—OpenGL显示—RAW/PNG保存—停止”的离线闭环。当前图像控件仍调用OpenGL API，因此“软件OpenGL”不等于“完全不使用OpenGL”。

尚未完成的是Linux USB后端、T630真机采集、串口真机回归和持续性能测试。离线数据是确定性合成梯度，不是T630实采数据，不能把本节结果表述为USB迁移成功。

## 7. 离线RAW格式与为什么采用该格式

离线输入与程序现有RAW保存格式互逆：

- 无文件头；
- 每个像素为16位无符号整数；
- 小端字节序；
- 每行只含`宽度 × 2`字节，不含QImage行对齐填充；
- 多帧文件就是完整帧连续拼接，文件大小必须是`宽度 × 高度 × 2`的整数倍。

这样做的原因是复用真实处理入口和程序已有保存语义，不另造只能显示测试图片的旁路。它验证的是完整帧进入图像处理后的软件链路；USB的16字节行包头、四通道收包和按行组帧属于传输层，后续应抽成Windows WinUSB与Linux libusb共用的解析模块，不能复制进离线回放器。

生成确定性测试文件：

```bash
cd /home/d508/projects/2025_SWIRVision_Linux
python3 scripts/linux/generate_synthetic_raw.py \
  --width 2048 --height 2048 --frames 3 \
  --output build/synthetic_2048x2048_3f.raw \
  --last-frame-output build/expected_last.raw
```

## 8. GUI操作与命令行复现

GUI工具栏提供“选择离线RAW、开始离线回放、停止离线回放、循环回放”。该入口与USB按钮分离，状态栏会明确显示离线来源。开始前必须把界面的宽、高设为与文件一致；停止动作会唤醒可能正在等待处理确认的工作线程并等待其正常结束。

自动处理3帧并保存末帧：

```bash
cd /home/d508/projects/2025_SWIRVision_Linux
DISPLAY=:0 \
XAUTHORITY=/run/user/1000/gdm/Xauthority \
  bash scripts/linux/run_x11_software.sh debug \
    --offline build/synthetic_2048x2048_3f.raw \
    --offline-width 2048 --offline-height 2048 \
    --offline-fps 10 --offline-frames 3 \
    --offline-save build/application_last.raw
echo $?
cmp build/expected_last.raw build/application_last.raw
```

退出码含义：

| 退出码 | 含义 |
| --- | --- |
| 0 | 达到目标帧数，处理和可选保存成功 |
| 2 | 命令行或尺寸/FPS参数非法 |
| 3 | 文件不存在、读失败、不足一帧或尾部不是完整帧 |
| 4 | 目标帧保存失败 |
| 5 | 文件自然结束，但没有达到`--offline-frames`要求 |

## 9. 一键离线回归

```bash
cd /home/d508/projects/2025_SWIRVision_Linux
bash -n scripts/linux/test_offline_replay.sh
DISPLAY=:0 \
XAUTHORITY=/run/user/1000/gdm/Xauthority \
  scripts/linux/test_offline_replay.sh debug
```

脚本会重新构建，随后覆盖以下场景：320×240正常回放与逐字节比较、缺文件、短文件、尾部残字节、非法宽度/FPS、自然结束未达到目标、循环跨文件尾、2048×2048回放、RAW逐字节比较和16位灰度PNG属性检查。证据保存在`build/offline-regression/`。

若只想快速验证小尺寸逻辑，可显式跳过400W尺寸测试：

```bash
SWIR_SKIP_400W_TEST=1 DISPLAY=:0 \
XAUTHORITY=/run/user/1000/gdm/Xauthority \
  scripts/linux/test_offline_replay.sh debug
```

跳过只适合开发中快速检查，不能代替交付前的2048×2048验证。

## 10. 离线验证的已知边界

- 当前合成帧没有T630行包头，也不验证四通道Bulk IN并发、行号/帧号重组和丢包恢复。
- 短时2048×2048测试只能证明功能正确，不能代表持续帧率和内存上限。
- 未加载真实NUC、坏点等标定数据时，只能验证处理线程与默认处理路径，不代表所有算法参数已完成回归。
- Mesa软件OpenGL已能显示；是否需要硬件加速必须用持续帧率、CPU和显示刷新数据判断。
