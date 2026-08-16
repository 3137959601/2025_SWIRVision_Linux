#!/usr/bin/env python3
"""生成确定性的16位小端合成RAW帧，仅用于离线链路测试。"""

import argparse
import struct
from pathlib import Path


def frame_pixels(width: int, height: int, frame_index: int):
    for y in range(height):
        for x in range(width):
            yield (x * 257 + y * 131 + frame_index * 4096) & 0xFFFF


def write_frame(handle, width: int, height: int, frame_index: int) -> None:
    row_format = "<" + "H" * width
    for y in range(height):
        row = [
            (x * 257 + y * 131 + frame_index * 4096) & 0xFFFF
            for x in range(width)
        ]
        handle.write(struct.pack(row_format, *row))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--width", type=int, required=True)
    parser.add_argument("--height", type=int, required=True)
    parser.add_argument("--frames", type=int, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--last-frame-output", type=Path)
    args = parser.parse_args()

    if args.width <= 0 or args.height <= 0 or args.frames <= 0:
        parser.error("width、height和frames都必须大于0")

    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("wb") as output:
        for frame_index in range(args.frames):
            write_frame(output, args.width, args.height, frame_index)

    if args.last_frame_output:
        args.last_frame_output.parent.mkdir(parents=True, exist_ok=True)
        with args.last_frame_output.open("wb") as last_frame:
            write_frame(last_frame, args.width, args.height, args.frames - 1)

    frame_bytes = args.width * args.height * 2
    print(f"生成完成：{args.output}")
    print(f"尺寸：{args.width}x{args.height}，帧数：{args.frames}，单帧字节：{frame_bytes}")
    print(f"总字节：{frame_bytes * args.frames}")
    if args.last_frame_output:
        print(f"末帧期望文件：{args.last_frame_output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
