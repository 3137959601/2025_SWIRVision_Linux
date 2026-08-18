#!/usr/bin/env python3
"""以只读方式检查MS3020串口是否向Linux交付字节，不发送任何指令。"""

import argparse
import fcntl
import os
import select
import struct
import termios
import time
import tty


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--device", default="/dev/ttyUSB0")
    parser.add_argument("--seconds", type=float, default=5.0)
    parser.add_argument("--assert-modem-lines", action="store_true",
                        help="打开后置位DTR和RTS，但仍不发送数据")
    parser.add_argument("--output", help="可选的原始接收文件")
    args = parser.parse_args()

    fd = os.open(args.device, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    try:
        tty.setraw(fd, when=termios.TCSANOW)
        attributes = termios.tcgetattr(fd)
        attributes[4] = termios.B115200
        attributes[5] = termios.B115200
        attributes[2] |= termios.CLOCAL | termios.CREAD | termios.CS8
        attributes[2] &= ~(termios.PARENB | termios.CSTOPB)
        if hasattr(termios, "CRTSCTS"):
            attributes[2] &= ~termios.CRTSCTS
        termios.tcsetattr(fd, termios.TCSANOW, attributes)
        termios.tcflush(fd, termios.TCIFLUSH)

        if args.assert_modem_lines:
            modem_lines = termios.TIOCM_DTR | termios.TIOCM_RTS
            fcntl.ioctl(fd, termios.TIOCMBIS, struct.pack("I", modem_lines))

        received = bytearray()
        deadline = time.monotonic() + args.seconds
        while time.monotonic() < deadline:
            readable, _, _ = select.select([fd], [], [], min(0.25, deadline - time.monotonic()))
            if not readable:
                continue
            try:
                chunk = os.read(fd, 4096)
            except BlockingIOError:
                continue
            if chunk:
                received.extend(chunk)
                print(f"SERIAL_CHUNK bytes={len(chunk)} hex={chunk.hex(' ').upper()}")

        if args.output:
            with open(args.output, "wb") as output:
                output.write(received)
        print("SERIAL_PROBE_RESULT "
              f"device={args.device} seconds={args.seconds:g} "
              f"dtr_rts={'on' if args.assert_modem_lines else 'default'} "
              f"bytes={len(received)}")
        return 0
    finally:
        os.close(fd)


if __name__ == "__main__":
    raise SystemExit(main())
