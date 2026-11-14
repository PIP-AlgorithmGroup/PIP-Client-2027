"""2027 V1.0.0 A5 自定义数据封帧；VT03/VT13 A9 53 遥控帧独立解码。

纯标准库、无 I/O。A5 长度/命令/CRC16 采用小端互操作约定，负载不重排。
PIP1 示例负载仅供调试，绝不是官方按钮/轴字段定义。时间参数使用单调秒。
"""

from dataclasses import dataclass
import math
import struct
import time


def crc8(data: bytes) -> int:
    """2027 附录 CRC8：init=FF，反射多项式 8C，无最终异或。"""
    value = 0xFF
    for byte in data:
        value ^= byte
        for _ in range(8):
            value = (value >> 1) ^ (0x8C if value & 1 else 0)
    return value


def crc16(data: bytes, *, ccitt_false: bool = False) -> int:
    """A5 使用反射 8408；独立 A9 53 帧默认按手册使用 CCITT-FALSE。"""
    value = 0xFFFF
    for byte in data:
        if ccitt_false:
            value ^= byte << 8
            for _ in range(8):
                value = ((value << 1) ^ (0x1021 if value & 0x8000 else 0)) & 0xFFFF
        else:
            value ^= byte
            for _ in range(8):
                value = (value >> 1) ^ (0x8408 if value & 1 else 0)
    return value


def encode_frame(payload: bytes, seq: int, command: int = 0x0311) -> bytes:
    """生成 A5 帧；仅支持 0311≤60B、0310≤300B，序号须为 0..255。"""
    if command not in (0x0310, 0x0311):
        raise ValueError("只支持 0x0310 / 0x0311")
    if len(payload) > (60 if command == 0x0311 else 300):
        raise ValueError("数据段超过命令容量")
    if not 0 <= seq <= 255:
        raise ValueError("seq 须为 0..255")
    header = struct.pack("<BHB", 0xA5, len(payload), seq)
    frame = header + bytes([crc8(header)]) + struct.pack("<H", command) + payload
    return frame + struct.pack("<H", crc16(frame))


def encode_demo(counter: int, axes: tuple[float, float, float, float], buttons: int) -> bytes:
    """调试样例：PIP1 + uint32 计数 + 四个 float32 + uint32 按键，共28B、小端。

    四轴命名 x/y/yaw/pitch，无物理单位；全部须有限且可由 float32 表示。
    """
    if not 0 <= counter <= 0xFFFFFFFF or not 0 <= buttons <= 0xFFFFFFFF:
        raise ValueError("计数和按键须为 uint32")
    if len(axes) != 4 or any(not math.isfinite(x) or abs(x) > 3.4028234e38 for x in axes):
        raise ValueError("四轴须为有限 float32")
    return b"PIP1" + struct.pack("<I4fI", counter, *axes, buttons)


def decode_demo(payload: bytes) -> dict:
    """显式解析本工具的 PIP1 样例；拒绝未知长度/标记和非有限轴值。"""
    if len(payload) != 28 or payload[:4] != b"PIP1":
        raise ValueError("不是 28 字节 PIP1 样例")
    counter, x, y, yaw, pitch, buttons = struct.unpack("<I4fI", payload[4:])
    if not all(math.isfinite(value) for value in (x, y, yaw, pitch)):
        raise ValueError("样例含非有限轴值")
    return dict(counter=counter, x=x, y=y, yaw=yaw, pitch=pitch, buttons=buttons)


@dataclass(frozen=True)
class Frame:
    """拥有完整帧字节的有效快照；遥控帧无 A5 命令号/序号，用 None 表示。"""

    kind: str
    command: int | None
    seq: int | None
    payload: bytes
    raw: bytes

    def record(self, *, demo: bool = False) -> dict:
        """返回可 JSON 序列化记录；默认只解释封装，保留全部原始字节与尾零。"""
        result = dict(
            kind=self.kind,
            command=None if self.command is None else f"0x{self.command:04X}",
            seq=self.seq,
            length=len(self.payload),
            hex=self.payload.hex(" "),
            frame_hex=self.raw.hex(" "),
        )
        if self.kind == "remote":
            value = int.from_bytes(self.raw, "little")

            def bits(offset, count):
                return (value >> offset) & ((1 << count) - 1)

            result["remote"] = dict(
                channels=[bits(16 + 11 * index, 11) for index in range(4)],
                mode=bits(60, 2),
                pause=bits(62, 1),
                left=bits(63, 1),
                right=bits(64, 1),
                wheel=bits(65, 11),
                trigger=bits(76, 1),
                mouse=list(struct.unpack("<hhh", self.raw[10:16])),
                mouse_buttons=[bits(offset, 2) for offset in (128, 130, 132)],
                keyboard=bits(136, 16),
            )
        elif demo:
            try:
                result["demo"] = decode_demo(self.payload)
            except ValueError as error:
                result["demo_error"] = str(error)
        return result


class FrameDecoder:
    """单线程增量解码器；逐字节推进，残帧最多309B，默认200ms到期。

    校验正确的未知 A5 命令整帧跳过，避免把其负载重新解释成控制帧。
    与监测工具职责一致，不按 seq 丢弃数据；保留序号供使用者诊断。
    A9 53 使用显式 CRC profile，不自动猜测或切换。
    """

    def __init__(self, *, timeout: float = 0.2, remote_crc: str = "ccitt_false"):
        """timeout 单位秒，正有限；remote_crc 为 ccitt_false / official_sample。"""
        if not math.isfinite(timeout) or timeout <= 0:
            raise ValueError("残帧超时须为正有限秒数")
        if remote_crc not in ("ccitt_false", "official_sample"):
            raise ValueError("未知遥控 CRC profile")
        self.timeout = timeout
        self.remote_crc = remote_crc
        self.buffer = bytearray()
        self.started = 0.0
        self.accepted = 0
        self.errors = 0
        self.noise = 0
        self.unsupported = 0

    def feed(self, data: bytes, now: float | None = None) -> list[Frame]:
        """追加分片返回有效帧；空 data 驱动超时。now 必须来自同一单调时钟。"""
        now = time.monotonic() if now is None else now
        if self.buffer and now - self.started >= self.timeout:
            self.buffer.clear()
            self.errors += 1
        frames = []
        for byte in data:
            if not self.buffer:
                self.started = now
            self.buffer.append(byte)
            while self.buffer:
                if self.buffer[0] not in (0xA5, 0xA9):
                    del self.buffer[0]
                    self.noise += 1
                    continue
                is_remote = self.buffer[0] == 0xA9
                if is_remote:
                    if len(self.buffer) < 2:
                        break
                    if self.buffer[1] != 0x53:
                        del self.buffer[0]
                        self.noise += 1
                        continue
                    size = 21
                else:
                    if len(self.buffer) < 5:
                        break
                    length = int.from_bytes(self.buffer[1:3], "little")
                    if crc8(self.buffer[:4]) != self.buffer[4] or length > 300:
                        del self.buffer[0]
                        self.errors += 1
                        continue
                    size = length + 9
                if len(self.buffer) < size:
                    break
                raw = bytes(self.buffer[:size])
                if crc16(
                    raw[:-2], ccitt_false=is_remote and self.remote_crc == "ccitt_false"
                ) != int.from_bytes(raw[-2:], "little"):
                    del self.buffer[0]
                    self.errors += 1
                    continue
                del self.buffer[:size]
                if is_remote:
                    frame = Frame("remote", None, None, raw[2:-2], raw)
                else:
                    command = int.from_bytes(raw[5:7], "little")
                    if command not in (0x0310, 0x0311) or (command == 0x0311 and length > 60):
                        self.unsupported += 1
                        continue
                    frame = Frame("custom", command, raw[3], raw[7:-2], raw)
                frames.append(frame)
                self.accepted += 1
        return frames
