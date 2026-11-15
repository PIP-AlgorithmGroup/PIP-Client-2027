"""真实工作线程的周期精度回归；使用pyserial loop，不占用用户实体设备。"""

from pathlib import Path
import sys
import threading
import time
import unittest
from unittest.mock import patch

import serial

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
import serial_control


class SerialSessionTest(unittest.TestCase):
    """测量实际写出时刻，不以界面填写的目标频率代替实际频率。"""

    def test_requested_cadence_and_no_catchup(self):
        # 实体write通常有耗时，不能只用近乎零耗时的loop证明长期周期不漂移。
        for rate, write_delay in ((50, 0), (75, 0), (50, 0.003), (75, 0.003)):
            with self.subTest(rate=rate, write_delay=write_delay):
                timestamps = []
                finished = threading.Event()
                original_write = serial_control.write_frame

                def record_write(port, packet):
                    started = time.perf_counter()
                    if write_delay:
                        time.sleep(write_delay)
                    original_write(port, packet)
                    timestamps.append(started)  # 测写入开始；可变write耗时不得伪造相邻短间隔。
                    if len(timestamps) >= rate * (6 if rate == 75 else 3) + 1:
                        finished.set()

                def loop_port(**configuration):
                    return serial.serial_for_url(
                        "loop://", baudrate=921600, timeout=0, write_timeout=0.1
                    )

                session = serial_control.SerialSession({}, "ccitt_false")
                settings = dict(
                    rate=rate,
                    seq=0,
                    counter=0,
                    buttons=0,
                    axes=(0, 0, 0, 0),
                    payload=bytes(range(60)),
                    demo=False,
                )
                with patch.object(serial_control, "open_port", loop_port), patch.object(
                    serial_control, "write_frame", record_write
                ):
                    session.submit(("start", settings))
                    session.start()
                    try:
                        self.assertTrue(finished.wait(8), "周期发送未在8秒内完成")
                    finally:
                        session.stopping.set()
                        session.join(timeout=1)
                self.assertFalse(session.is_alive(), "串口线程未停止")
                actual_rate = (len(timestamps) - 1) / (timestamps[-1] - timestamps[0])
                minimum_interval = min(b - a for a, b in zip(timestamps, timestamps[1:]))
                print(
                    f"target={rate}Hz write_delay={write_delay * 1000:.0f}ms "
                    f"actual={actual_rate:.2f}Hz min_gap={minimum_interval * 1000:.2f}ms"
                )
                self.assertGreaterEqual(actual_rate, rate * 0.999)
                self.assertLessEqual(actual_rate, rate * 1.001)
                self.assertGreaterEqual(minimum_interval, 1 / rate * 0.9, "不允许追赶突发")


if __name__ == "__main__":
    unittest.main()
