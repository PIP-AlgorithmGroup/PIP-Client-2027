"""Linux/WSL连续链路验收源：PTY→真实产品probe→外部MQTT；不需要pyserial。

仅测试输入段被PTY替代。所有文件/网络端点由参数指定，子进程最多运行8秒。
"""

import argparse
import json
import os
from pathlib import Path
import pty
import selectors
import socket
import struct
import subprocess
import time
import tty

from serial_protocol import encode_frame


def windows_gateway():
    """WSL NAT默认网关；只读取路由，不改变系统网络。不适用于非NAT时须显式host。"""
    for line in Path("/proc/net/route").read_text().splitlines()[1:]:
        fields = line.split()
        if fields[1] == "00000000":
            return socket.inet_ntoa(struct.pack("<I", int(fields[2], 16)))
    raise ValueError("没有默认网关，请提供 --host")


def run(arguments):
    """单线程拥有PTY；先等产品ready再发新控制，退出关闭两端并等待产品析构。"""
    payloads = [bytes.fromhex(value) for value in json.loads(arguments.payloads.read_text())]
    master, slave = pty.openpty()
    process = None
    try:
        tty.setraw(slave)
        process = subprocess.Popen(
            [
                str(arguments.probe),
                "--port",
                os.ttyname(slave),
                "--host",
                arguments.host or windows_gateway(),
                "--mqtt-port",
                str(arguments.mqtt_port),
                "--seconds",
                "8",
            ],
            stdout=subprocess.PIPE,
        )
        # 按原始字节读pipe，避免TextIO预读与select状态不一致；每行有明确上限。
        pending = bytearray()
        with selectors.DefaultSelector() as selector:
            selector.register(process.stdout, selectors.EVENT_READ)
            deadline = time.monotonic() + 6
            is_ready = False
            while time.monotonic() < deadline and not is_ready:
                if process.poll() is not None:
                    raise RuntimeError(f"产品probe启动退出 {process.returncode}")
                if not selector.select(0.1):
                    continue
                pending.extend(os.read(process.stdout.fileno(), 4096))
                if len(pending) > 16384:
                    raise RuntimeError("probe输出超限")
                is_ready = b"ready\n" in pending
            if not is_ready:
                raise TimeoutError("PTY产品probe未ready")
        for seq, payload in enumerate(payloads):
            packet = encode_frame(payload, seq & 255)
            if os.write(master, packet) != len(packet):
                raise OSError("PTY部分写入")
            time.sleep(0.04)
        if process.wait(timeout=10) != 0:
            raise RuntimeError("PTY产品probe异常退出")
        print(f"PASS PTY输入已发送 {len(payloads)} 帧；输出由末端独立验证", flush=True)
    finally:
        if process is not None and process.poll() is None:
            process.terminate()
            process.wait(timeout=3)
        os.close(master)
        os.close(slave)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--probe", type=Path, required=True)
    parser.add_argument("--payloads", type=Path, required=True)
    parser.add_argument("--host", default="")
    parser.add_argument("--mqtt-port", type=int, required=True)
    run(parser.parse_args())
