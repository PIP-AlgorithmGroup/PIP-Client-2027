"""独立官方 CRC 向量、混合流恢复、样例负载及真实 GUI 交互烟测。"""

from pathlib import Path
import struct
import sys
import os
import time
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
from serial_protocol import FrameDecoder, crc16, decode_demo, encode_demo, encode_frame


class SerialProtocolTest(unittest.TestCase):
    """纯协议用例不依赖 pyserial 或设备；物理测试由显式工具命令驱动。"""

    def test_official_crc_vectors_and_empty_presence(self):
        self.assertEqual(
            encode_frame(bytes.fromhex("00a5ff"), 255).hex(), "a50300ff12110300a5ffcd58"
        )
        self.assertEqual(encode_frame(b"", 1).hex(), "a50000019d1103ddb2")
        self.assertEqual(crc16(b"123456789"), 0x6F91)
        self.assertEqual(crc16(b"123456789", ccitt_false=True), 0x29B1)

    def test_boundaries_fragments_crc_and_noise(self):
        parser = FrameDecoder()
        payload = bytes(range(60))
        packet = encode_frame(payload, 0)
        self.assertEqual(
            packet.hex(),
            "a53c0000a61103000102030405060708090a0b0c0d0e0f"
            "101112131415161718191a1b1c1d1e1f202122232425262728292a2b2c2d2e2f"
            "303132333435363738393a3bb21e",
        )
        self.assertEqual(parser.feed(packet[:6], 0), [])
        self.assertEqual(parser.feed(packet[6:], 0.01)[0].payload, payload)
        corrupt = bytearray(packet)
        corrupt[-1] ^= 1
        self.assertEqual(len(parser.feed(b"noise" + corrupt + packet + packet, 0.02)), 2)
        self.assertGreater(parser.errors, 0)
        with self.assertRaises(ValueError):
            encode_frame(bytes(61), 0)
        self.assertEqual(
            FrameDecoder().feed(encode_frame(bytes(300), 0, 0x0310))[0].payload, bytes(300)
        )

    def test_timeout_and_unsupported_outer_frame(self):
        parser = FrameDecoder()
        packet = encode_frame(b"x", 0)
        parser.feed(packet[:7], 0)
        self.assertEqual(parser.feed(packet[7:], 0.21), [])
        outer = bytearray(encode_frame(packet, 1))
        outer[5:7] = bytes.fromhex("1203")
        outer[-2:] = struct.pack("<H", crc16(outer[:-2]))
        self.assertEqual(parser.feed(outer, 0.22), [])
        self.assertEqual(parser.unsupported, 1)
        self.assertEqual(parser.feed(packet, 0.23)[0].payload, b"x")

    def test_mixed_remote_profiles_do_not_expose_nested_a5_payload(self):
        # 独立固定向量与MockServer黄金帧一致；遥控帧不使用A5的CRC profile。
        manual = bytes.fromhex("a95300042000011800080000000000000000003738")
        sample = bytes.fromhex("a9530004200001180008000000000000000000528c")
        custom = encode_frame(b"\x00\xa9\x53\x00", 255)
        decoder = FrameDecoder()
        result = decoder.feed(manual + custom)
        self.assertEqual([frame.kind for frame in result], ["remote", "custom"])
        self.assertEqual(result[0].record()["remote"]["channels"], [1024] * 4)
        self.assertEqual(result[1].payload, b"\x00\xa9\x53\x00")
        self.assertEqual(FrameDecoder().feed(sample), [])
        self.assertEqual(FrameDecoder(remote_crc="official_sample").feed(sample)[0].kind, "remote")

    def test_demo_roundtrip_and_validation(self):
        payload = encode_demo(42, (0.5, -1, 2, 3), 0x80000001)
        self.assertEqual(len(payload), 28)
        self.assertEqual(
            decode_demo(payload), dict(counter=42, x=0.5, y=-1, yaw=2, pitch=3, buttons=0x80000001)
        )
        with self.assertRaises(ValueError):
            encode_demo(0, (float("nan"), 0, 0, 0), 0)
        with self.assertRaises(ValueError):
            decode_demo(b"unknown")

    @unittest.skipUnless(os.environ.get("PIP_SERIAL_UI_TEST"), "显式启用Tk UI烟测")
    def test_actual_gui_worker_sends_and_decodes(self):
        import tkinter as tk
        from serial_control import ControlWindow
        import serial

        root = tk.Tk()
        root.withdraw()
        window = ControlWindow(root, "loop://")
        # 测试使用 pyserial 的实际loop设备适配；产品界面/工作线程/协议均不替换。
        import serial_control

        original = serial_control.open_port
        serial_control.open_port = lambda **kwargs: serial.serial_for_url(
            "loop://", baudrate=921600, timeout=0, write_timeout=0.1
        )
        try:
            window.connect()
            end = time.monotonic() + 2
            while window.status.get() != "已连接" and time.monotonic() < end:
                root.update()
                time.sleep(0.01)
            self.assertEqual(window.status.get(), "已连接")
            window.send("once")
            end = time.monotonic() + 2
            while window.latest_rx is None and time.monotonic() < end:
                root.update()
                time.sleep(0.01)
            self.assertEqual(window.latest_rx["hex"], "00 a5 ff")
            window.expected.set("00 a5 ff")
            window.compare()
            self.assertIn("完全一致", window.status.get())
            window.demo.set(True)
            window.show_demo.set(True)
            window.send("start")
            end = time.monotonic() + 0.3
            while time.monotonic() < end:
                root.update()
                time.sleep(0.01)
            window.send("stop")
            self.assertGreater(window.session.counters["tx"], 2)
            self.assertTrue(bytes.fromhex(window.latest_rx["hex"]).startswith(b"PIP1"))
        finally:
            serial_control.open_port = original
            window.close()

    @unittest.skipUnless(os.environ.get("PIP_SERIAL_UI_PORTS"), "显式指定一对互连硬件才打开端口")
    def test_gui_actual_physical_ports(self):
        import tkinter as tk
        from serial_control import ControlWindow

        first, second = os.environ["PIP_SERIAL_UI_PORTS"].split(",")
        root = tk.Tk()
        receiver_root = tk.Toplevel(root)
        root.withdraw()
        receiver_root.withdraw()
        sender = ControlWindow(root, first)
        receiver = ControlWindow(receiver_root, second)

        def pump_until(condition, seconds=2):
            end = time.monotonic() + seconds
            while not condition() and time.monotonic() < end:
                root.update()
                time.sleep(0.008)
            self.assertTrue(condition())

        try:
            sender.connect()
            receiver.connect()
            pump_until(lambda: sender.status.get() == receiver.status.get() == "已连接")
            sender.payload.set("00 A5 FF 00")
            sender.send("once")
            pump_until(lambda: receiver.latest_rx is not None)
            self.assertEqual(receiver.latest_rx["hex"], "00 a5 ff 00")
            sender.demo.set(True)
            receiver.show_demo.set(True)
            sender.fields["x"].set("0.5")
            sender.fields["buttons"].set("0x15")
            sender.send("start")
            pump_until(lambda: receiver.session.counters["rx"] >= 30, seconds=3)
            sender.send("stop")
            pump_until(lambda: not sender.cyclic)
            decoded = decode_demo(bytes.fromhex(receiver.latest_rx["hex"]))
            self.assertEqual(decoded["x"], 0.5)
            self.assertEqual(decoded["buttons"], 0x15)
            self.assertEqual(receiver.session.counters["errors"], 0)
        finally:
            receiver.close()
            sender.close()


if __name__ == "__main__":
    unittest.main()
