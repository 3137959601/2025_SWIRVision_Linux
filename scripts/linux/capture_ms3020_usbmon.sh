#!/usr/bin/env bash
# 只读采集MacroSilicon 345f:3020所在USB总线的usbmon记录。
# 用法：sudo bash scripts/linux/capture_ms3020_usbmon.sh [秒数] [输出文件]
set -euo pipefail

seconds="${1:-15}"
output="${2:-/tmp/ms3020-usbmon-$(date +%Y%m%d-%H%M%S).log}"
bus="${MS3020_USB_BUS:-3}"

if [[ "${EUID}" -ne 0 ]]; then
    echo "本脚本仅读取内核usbmon调试接口，需要以sudo运行：" >&2
    echo "  sudo bash $0 [秒数] [输出文件]" >&2
    exit 1
fi
if ! [[ "$seconds" =~ ^[1-9][0-9]*$ ]]; then
    echo "采集秒数必须是正整数，当前值：$seconds" >&2
    exit 2
fi

modprobe usbmon
if ! mountpoint -q /sys/kernel/debug; then
    mount -t debugfs none /sys/kernel/debug
fi

monitor="/sys/kernel/debug/usb/usbmon/${bus}u"
if [[ ! -r "$monitor" ]]; then
    echo "无法读取usbmon节点：$monitor；请用 lsusb 确认总线号。" >&2
    exit 3
fi

device_path=""
for candidate in /sys/bus/usb/devices/*; do
    [[ -r "$candidate/idVendor" && -r "$candidate/idProduct" ]] || continue
    if [[ "$(cat "$candidate/idVendor")" == "345f" &&
          "$(cat "$candidate/idProduct")" == "3020" ]]; then
        device_path="$candidate"
        break
    fi
done
if [[ -z "$device_path" ]]; then
    echo "未在sysfs发现345f:3020；请确认设备已透传到Ubuntu。" >&2
    exit 4
fi
device_number="$(printf '%03d' "$(cat "$device_path/devnum")")"
bulk_in_filter="Bi:${bus}:${device_number}:3"

echo "开始只读采集USB总线${bus}，持续${seconds}秒：$output"
echo "采集期间在GUI中打开 ttyUSB0，等待遥测数据即可；不要发送业务指令。"
timeout --foreground "$seconds" cat "$monitor" > "$output" || {
    status=$?
    if [[ "$status" -ne 124 ]]; then
        exit "$status"
    fi
}
echo "采集完成，字节数：$(wc -c < "$output")"
echo "自动识别的Bulk IN 0x83筛选条件：$bulk_in_filter"
grep "$bulk_in_filter" "$output" | tail -n 80 || true
