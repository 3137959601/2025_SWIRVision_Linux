# Ubuntu x86_64 构建与运行复现

> 2026-09-11精简说明：自动测试源码和`test_*.sh`脚本已从当前工作树删除，避免干扰主程序阅读；本文后续出现的相关命令属于历史验证记录，可从Git提交`3331952`恢复。当前日常入口以第3节构建、第4节运行和第8节GUI/命令行回放为准。

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

在Ubuntu虚拟机的**桌面终端**中启动时，已经处于图形会话，不需要手动设置`DISPLAY`和`XAUTHORITY`；推荐先构建再直接运行Debug产物：

```bash
cd /home/d508/projects/2025_SWIRVision_Linux
bash scripts/linux/build_x86_64.sh debug
export LD_LIBRARY_PATH=/home/d508/.local/share/codex-envs/400w-qt-linux-v2/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}
export QT_OPENGL=software LIBGL_ALWAYS_SOFTWARE=1
./build/x86_64-debug/SWIRVision
```

代码与产物位置：

```text
Linux源码：/home/d508/projects/2025_SWIRVision_Linux
主窗口与Start/Stop：mainwindow.cpp
Linux USB/libusb后端：common/tih_usb_device_linux.cpp、transfer_thread_linux.cpp
跨端点行协议与组帧：common/usb_frame_pipeline.cpp
串口Qt逻辑：serialworker.cpp
Debug可执行文件：build/x86_64-debug/SWIRVision
运行日志：build/x86_64-debug/hardware-gui.log
```

如果已经构建完成，也可只执行最后三行。`run_x11_software.sh`是SSH远程启动辅助脚本，才需要显式指定X11会话，方法如下。

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

Linux USB后端已使用libusb 1.0.29实现，跨平台行包解析、四线程组帧、无模组枚举、取消与错误路径已经通过软件测试；Qt SerialPort也已通过伪终端端到端回归。尚未完成的是T630真机采集、串口真机回归和持续性能测试。离线数据是确定性合成梯度，不是T630实采数据，因此当前结论是“无模组软件移植完成”，不能写成“硬件链路验收成功”。

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

## 11. USB协议、libusb和串口软件回归

一键执行当前全部无模组软件测试：

```bash
cd /home/d508/projects/2025_SWIRVision_Linux
DISPLAY=:0 \
XAUTHORITY=/run/user/1000/gdm/Xauthority \
  bash scripts/linux/test_all_x86_64.sh
```

该脚本依次验证：

- USB行包在每个字节位置切分时仍能恢复完整行；
- 噪声、非法头、乱序、重复行、三帧窗口和16位帧号回绕；
- 四个线程并发向共享组帧器送行时的数据一致性；
- UART纯协议与线性拉伸数学；
- Qt SerialPort通过Linux伪终端写出命令、读入40字节遥测并完成解析；
- libusb枚举现有设备、T630未连接和非法参数路径；
- 2048×2048离线图像处理、显示和保存。

分别执行的关键命令：

```bash
bash scripts/linux/test_usb_frame_pipeline.sh
bash scripts/linux/test_qt_protocols.sh
bash scripts/linux/test_libusb_enumeration.sh
DISPLAY=:0 XAUTHORITY=/run/user/1000/gdm/Xauthority \
  bash scripts/linux/test_offline_replay.sh debug
```

只读枚举指定VID/PID：

```bash
envp="$HOME/.local/share/codex-envs/400w-qt-linux-v2"
export LD_LIBRARY_PATH="$envp/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export QT_QPA_PLATFORM=offscreen
build/x86_64-release/SWIRVision --usb-list 706d:807c
echo $?
```

输出`USB_LIST_COUNT ... count=0`且退出0表示查询成功但没有设备，不是程序错误；连接T630后应至少为1，并列出`libusb://BBB/DDD?...`URI。

## 12. 生成和使用x86_64运行包

```bash
cd /home/d508/projects/2025_SWIRVision_Linux
bash -n scripts/linux/package_x86_64.sh
bash scripts/linux/package_x86_64.sh release
sha256sum -c build/package/SWIRVision-linux-x86_64.tar.gz.sha256
```

产物为：

```text
build/package/SWIRVision-linux-x86_64/
build/package/SWIRVision-linux-x86_64.tar.gz
build/package/SWIRVision-linux-x86_64.tar.gz.sha256
```

直接运行：

```bash
cd build/package/SWIRVision-linux-x86_64
./运行_SWIRVision.sh
./运行_SWIRVision.sh --usb-list 706d:807c
```

默认启用软件OpenGL，原因是Ubuntu虚拟机不保证有可用GPU直通。若目标机已经有正常桌面硬件OpenGL，可显式尝试：

```bash
SWIR_OPENGL_MODE=desktop ./运行_SWIRVision.sh
```

打包脚本递归收集可执行文件和所选Qt插件在隔离环境中的动态库，不复制glibc和ELF动态加载器。这样可以避免替换目标系统最基础的ABI组件，但也意味着目标机应是兼容的x86_64 Linux；当前只在Ubuntu 20.04.6虚拟机验证，跨发行版兼容不能自动推定。

## 13. T630连接后的验收顺序

先确认虚拟机本身提供USB 3.x控制器：

```bash
lspci -nnk | grep -A4 -i 'USB controller'
lsusb -t
for f in /sys/bus/usb/devices/usb*/speed; do printf '%s=' "$f"; cat "$f"; done
```

当前实测只有VMware USB1.1 UHCI和USB2 EHCI，最高根集线器速率480 Mbit/s，没有xHCI。用户需要先关闭虚拟机，在VMware虚拟机设置的“USB控制器”中把兼容性改为USB 3.0或USB 3.1，再启动Ubuntu并把T630连接到虚拟机。复查时应看到xHCI控制器和`5000M`根集线器；否则只能做低速枚举/功能诊断，不能验收USB 3.x性能。

先执行只读检查，不要一上来修改权限：

```bash
lsusb -d 706d:807c
lsusb -t
lsusb -d 706d:807c -v | tee build/t630-lsusb-descriptor.txt
QT_QPA_PLATFORM=offscreen \
  build/package/SWIRVision-linux-x86_64/运行_SWIRVision.sh \
  --usb-list 706d:807c | tee build/t630-usb-list.txt
```

重点确认：

- VID/PID是否确实为`706d:807c`；如不同，以`lsusb`实测值为准，不能硬改设备假装识别。
- `lsusb -t`是否显示`5000M`或更高，而不是`480M`；USB 2.0链路无法代表Windows USB 3.0性能基线。
- 接口0/altsetting 0是否存在四个Bulk IN端点；若描述符不同，应修改端点发现策略并记录实际描述符。
- 普通用户是否能打开`/dev/bus/usb/BBB/DDD`。

然后在GUI中按“检索设备 → 连接 → 开始传输 → 停止传输”的顺序短测，观察完整帧、丢行/丢帧和错误日志。不要把“设备能枚举”写成“采集成功”；至少需要看到完整帧并能安全停止。

如果日志明确出现`LIBUSB_ERROR_ACCESS`，才需要用户在Ubuntu终端执行以下`sudo`命令。先确认系统存在`plugdev`组：

```bash
getent group plugdev
```

存在时创建最小范围udev规则：

```bash
printf '%s\n' 'SUBSYSTEM=="usb", ATTR{idVendor}=="706d", ATTR{idProduct}=="807c", MODE="0660", GROUP="plugdev", TAG+="uaccess"' | \
  sudo tee /etc/udev/rules.d/70-swir-t630.rules >/dev/null
sudo udevadm control --reload-rules
sudo udevadm trigger
```

随后拔下并重新插入T630，再检查权限和枚举：

```bash
ls -l /dev/bus/usb/BBB/DDD
QT_QPA_PLATFORM=offscreen \
  build/package/SWIRVision-linux-x86_64/运行_SWIRVision.sh --usb-list 706d:807c
```

其中`BBB/DDD`必须替换为程序URI或`lsusb`给出的实际三位总线号和设备号。规则使用`0660`而不是`0666`，只授权桌面活跃用户/plugdev组，避免向所有用户开放设备。若`plugdev`组不存在，先停止，不要照抄规则；应根据该虚拟机的实际用户组生成规则。

串口验收前先确认模组接口是3.3 V TTL、RS-232还是RS-485，并使用匹配的转换器；电气制式不明确时禁止直接连接。确认后记录：

```bash
ls -l /dev/ttyUSB* /dev/ttyACM* 2>/dev/null
dmesg --ctime | tail -100
```

普通用户若出现串口权限错误，再执行一次：

```bash
sudo usermod -aG dialout "$USER"
```

该组变更需要注销并重新登录后生效。不要用`sudo`直接启动GUI作为长期方案，否则会造成配置文件属主和显示授权问题。

## 14. 当前阶段门槛

Ubuntu x86_64的无模组软件实现、自动回归和运行包已经完成。当前必须等待T630硬件连接；只有USB真实采集、停止/重连和串口控制通过后，才进入RK3588 ARM64移植。RK3588和EVS在此之前不继续修改。

## 15. 固定缺8行时的Windows兼容显示

现场严格解析结果为每帧稳定`2040/2048`行。当前Linux独立项目保留2048×2048协议，仅对该规格允许最多缺8行：检测到下一帧后发布上一帧，缺失行沿用上一张已发布图像，首帧缺行补零。这复现了原Windows缓冲区不清零并在帧号切换时发布的效果，但统计和警告仍保留实际缺行，不应据此写成“模组零丢行”。

关键复测命令：

```bash
cd /home/d508/projects/2025_SWIRVision_Linux
bash scripts/linux/test_usb_frame_pipeline.sh
bash scripts/linux/test_all_x86_64.sh debug
```

真机GUI按以下顺序操作：检索设备、连接、打开串口、开始传输约10秒、停止传输。随后保存日志：

```bash
grep -E 'USB兼容发布|已发布帧|其中兼容帧|最完整帧行数' \
  build/x86_64-debug/hardware-gui.log | tail -n 80
tail -n 30 build/x86_64-debug/speed_log.txt
```

预期是出现`USB兼容发布不完整帧`并且GUI产生图像。若仍无图像，检查是否出现`已发布帧`增长：增长但无图说明问题在显示链路；不增长则继续检查实际最完整行数是否低于2040。以上操作不需要sudo。

## 16. MacroSilicon串口无数据时的修复与诊断

当前设备为`345f:3020`，Linux节点为`/dev/ttyUSB0`，由`pl2303`驱动绑定。GUI应先点击“刷新串口”，选择`ttyUSB0`，再点击“打开串口”；新版本会固定使用115200 8N1、无流控，并显式置位DTR/RTS。打开“调试 → 串口调试状态”，应能看到实际线路置位结果。

若GUI报`Permission denied`，先检查应用进程是否继承了`dialout`组。设备权限应类似`crw-rw---- root dialout /dev/ttyUSB0`。即使`id`已经显示当前用户属于`dialout`，**在加入该组之前启动的桌面会话和GUI仍不会自动获得新组权限**。不必更换串口设备，也不用sudo；关闭旧GUI后，在Ubuntu桌面终端执行以下命令，随后在新shell中启动程序：

```bash
newgrp dialout
id
cd /home/d508/projects/2025_SWIRVision_Linux
export LD_LIBRARY_PATH=/home/d508/.local/share/codex-envs/400w-qt-linux-v2/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}
export QT_OPENGL=software LIBGL_ALWAYS_SOFTWARE=1
./build/x86_64-debug/SWIRVision
```

`id`输出必须含有`dialout`。要永久让桌面启动器也带上该组，注销Ubuntu桌面账户后重新登录即可；无需重装、重启或改设备权限。

普通用户可先执行只读探针；开始前请关闭GUI中的串口，避免两个进程同时打开设备：

```bash
cd /home/d508/projects/2025_SWIRVision_Linux
python3 scripts/linux/probe_ms3020_serial.py --device /dev/ttyUSB0 --seconds 10
python3 scripts/linux/probe_ms3020_serial.py --device /dev/ttyUSB0 --seconds 10 --assert-modem-lines
```

两次探针均不发送任何业务命令。若仍为0字节，使用下面的`sudo`只读采集USB总线。**先启动命令，再在15秒窗口内于GUI执行“关闭串口→打开串口”，并等待约10秒**；这样初始化控制传输和Bulk IN请求才会被记录。采集完成后把终端输出交给排查者：

```bash
cd /home/d508/projects/2025_SWIRVision_Linux
sudo bash scripts/linux/capture_ms3020_usbmon.sh 15 /tmp/ms3020-usbmon.log
```

脚本会从sysfs自动识别当前`345f:3020`设备号，并在结束时直接输出Bulk IN `0x83`的最近80条记录；设备重新插拔后不需要手动修改筛选号。`usbmon`只读取内核调试记录，不会发送串口数据、重置设备或替换驱动。

判断规则：若`Bi`完成记录中的长度持续为0，数据没有从设备到达Linux USB层，继续检查模组输出、VMware透传或驱动初始化；若存在非零`Bi`数据而`/dev/ttyUSB0`仍读不到，问题就锁定在`pl2303` tty转换层，下一步再针对性测试新内核/新驱动，不能靠增加轮询指令掩盖。
