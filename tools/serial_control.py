"""控制器模拟发送 / 图传串口解码。默认打开 Tk UI，--help 查看命令行模式。"""

import argparse
from contextlib import contextmanager
import json
import math
import queue
import sys
import threading
import time

from serial_protocol import FrameDecoder, encode_demo, encode_frame

try:
    import serial
    from serial.tools import list_ports
except ImportError as error:
    raise SystemExit(
        "缺少 pyserial：python -m pip install -r tools/requirements_serial.txt"
    ) from error


def open_port(port, baud=921600, bits=8, parity="N", stop="1", flow="none"):
    """调用者拥有串口，须 close；读非阻塞、写最多等待100ms，不更改系统权限。"""
    if not port.strip() or port != port.strip():
        raise ValueError("请选择或填写设备路径")
    if not 1 <= int(baud) <= 4000000 or bits not in (5, 6, 7, 8):
        raise ValueError("波特率须为1..4000000，数据位须为5..8")
    if parity not in ("N", "E", "O", "S", "M") or stop not in ("1", "1.5", "2"):
        raise ValueError("校验或停止位无效")
    if (stop == "1.5" and bits != 5) or (stop == "2" and bits == 5):
        raise ValueError("1.5停止位须配5数据位；5数据位不配2停止位")
    if flow not in ("none", "hardware", "software"):
        raise ValueError("流控无效")
    return serial.Serial(
        port,
        int(baud),
        bytesize=bits,
        parity=parity,
        stopbits=float(stop),
        timeout=0,
        write_timeout=0.1,
        rtscts=flow == "hardware",
        xonxoff=flow == "software",
    )


def write_frame(port, packet):
    """单 writer 写完整帧；超时/部分写报错，禁止自动重放不确定的控制。"""
    if port.write(packet) != len(packet):
        raise OSError("部分帧写出，状态不确定；请重新连接后发送新数据")


@contextmanager
def _serial_clock():
    """串口线程的计时生命周期；仅旧版Windows Python临时请求1ms时钟粒度。

    Python 3.11+ 的Windows sleep使用高分辨率等待定时器，POSIX sleep也支持细粒度等待。
    3.10及更早的Windows sleep仍受系统tick限制，请求必须与释放配对，不永久修改时钟。
    """
    timer = None
    if sys.platform == "win32" and sys.version_info < (3, 11):
        import ctypes

        timer = ctypes.WinDLL("winmm")
        if timer.timeBeginPeriod(1) != 0:
            raise OSError("无法请求Windows 1ms计时精度，请使用Python 3.11或更新版本")
    try:
        yield
    finally:
        if timer is not None and timer.timeEndPeriod(1) != 0:
            raise OSError("释放Windows 1ms计时请求失败")


def pair_test(first_name, second_name):
    """显式独占一对已互连的物理端口；默认921600/8N1，各方向验证30个有效帧。"""
    with open_port(first_name) as first, open_port(second_name) as second:
        for source, destination in ((first, second), (second, first)):
            source.reset_input_buffer()
            destination.reset_input_buffer()
            decoder = FrameDecoder()
            for seq in range(30):
                payload = (b"", bytes.fromhex("00a5ff"), bytes(range(60)))[seq % 3]
                write_frame(source, encode_frame(payload, seq))
                end = time.monotonic() + 1
                frames = []
                while not frames and time.monotonic() < end:
                    frames.extend(decoder.feed(destination.read(4096)))
                    time.sleep(0.002)
                if len(frames) != 1 or frames[0].payload != payload or frames[0].seq != seq:
                    raise RuntimeError(f"{source.port}→{destination.port} 第{seq}帧不一致或超时")
            print(f"PASS {source.port} -> {destination.port}: 30帧, 0/3/60B, CRC/序号/负载一致")


class SerialSession(threading.Thread):
    """工作线程独占串口及解析器；命令槽1个、显示事件最多200个，UI不执行I/O。

    每次主动等待不超过4ms，随后检查停止标志；写操作另有100ms超时，UI随后join。
    断线终止且不自动重连/补发。
    显示队列拥塞时丢弃显示事件但保留累计计数，不影响串口线程的收发。
    """

    def __init__(self, configuration, remote_crc):
        super().__init__(daemon=False)
        self.configuration = configuration
        self.remote_crc = remote_crc
        self.commands = queue.Queue(maxsize=1)
        self.events = queue.Queue(maxsize=200)
        self.stopping = threading.Event()
        self.counters = dict(tx=0, rx=0, remote=0, errors=0, display_drops=0)

    def submit(self, command):
        """UI非阻塞提交不可变发送快照；忙时显式拒绝，不积压控制命令。"""
        self.commands.put_nowait(command)

    def _event(self, kind, value):
        try:
            self.events.put_nowait((kind, value, self.counters.copy()))
        except queue.Full:
            self.counters["display_drops"] += 1

    def run(self):
        """串口仅在此线程打开/关闭；按高精度单调deadline等待，不追赶补发。

        旧Windows Python的monotonic和Event.wait可能量化为15.6ms，使50Hz约为31Hz。
        perf_counter使用QPC；周期沿原截止时间推进，不把编码、写入和显示事件耗时累积进周期。
        允许有限相位校正，但两次写入开始至少相隔目标周期的90%且不小于13334µs；
        写入阻塞越过下一周期时重新定相，不追赶已错过的控制。系统调度仍不提供硬实时保证。
        """
        decoder = FrameDecoder(remote_crc=self.remote_crc)
        cyclic = None
        seq = 0
        counter = 0
        deadline = 0.0
        try:
            with _serial_clock(), open_port(**self.configuration) as port:
                port.reset_input_buffer()
                self._event("state", "已连接")
                while not self.stopping.is_set():
                    now = time.perf_counter()
                    try:
                        command, settings = self.commands.get_nowait()
                    except queue.Empty:
                        command, settings = None, None
                    send = None
                    if command == "stop":
                        cyclic = None
                        self._event("state", "已停止周期发送")
                    elif command in ("once", "start"):
                        seq = settings["seq"]
                        counter = settings["counter"]
                        cyclic = settings if command == "start" else cyclic
                        send = settings
                        deadline = now
                    if send is None and cyclic is not None and now >= deadline:
                        send = cyclic
                    if send is not None:
                        payload = (
                            encode_demo(counter, send["axes"], send["buttons"])
                            if send["demo"]
                            else send["payload"]
                        )
                        packet = encode_frame(payload, seq)
                        write_started = time.perf_counter()
                        write_frame(port, packet)
                        write_completed = time.perf_counter()
                        self.counters["tx"] += 1
                        self._event(
                            "tx", dict(seq=seq, hex=payload.hex(" "), frame_hex=packet.hex(" "))
                        )
                        seq = (seq + 1) & 255
                        counter = (counter + 1) & 0xFFFFFFFF
                        period = 1 / send["rate"]
                        next_phase = deadline + period
                        # 保持周期相位以补偿正常I/O耗时；长写入只跳过旧周期，不连续补发。
                        if next_phase <= write_completed:
                            next_phase = write_completed + period
                        deadline = max(next_phase, write_started + max(period * 0.9, 0.013334))
                    previous_errors = decoder.errors
                    frames = decoder.feed(port.read(4096))
                    self.counters["errors"] = decoder.errors
                    if decoder.errors != previous_errors:
                        self._event("warning", "已丢弃CRC错误或超时残帧")
                    for frame in frames:
                        self.counters["remote" if frame.kind == "remote" else "rx"] += 1
                        self._event("rx", frame.record())
                    delay = 0.004
                    if cyclic is not None:
                        delay = min(delay, max(0.0, deadline - time.perf_counter()))
                    if delay > 0:
                        time.sleep(delay)
        except (OSError, ValueError, serial.SerialException) as error:
            self._event("error", str(error))
        finally:
            self._event("closed", "已断开")


class ControlWindow:
    """浅色 Tk 前端；全部控件仅主线程访问，每50ms取有界日志/状态快照。"""

    def __init__(self, root, initial_port=""):
        import tkinter as tk
        from tkinter import ttk
        from tkinter.scrolledtext import ScrolledText

        self.root = root
        self.session = None
        self.last_event = None
        self.latest_rx = None
        self.cyclic = False
        self.root.title("PIP 自定义控制器 · 串口发送与解码")
        self.root.geometry("1000x760")
        self.root.minsize(850, 680)
        self.root.configure(bg="#f4f5f7")
        style = ttk.Style(root)
        style.theme_use("clam")
        style.configure("TFrame", background="#f4f5f7")
        style.configure("TLabel", background="#f4f5f7")
        style.configure("TButton", padding=6)
        body = ttk.Frame(root, padding=18)
        body.pack(fill="both", expand=True)
        ttk.Label(body, text="自定义控制器 / 图传串口联调", font=("", 18, "bold")).pack(anchor="w")
        self.status = tk.StringVar(value="未连接")
        ttk.Label(body, textvariable=self.status, wraplength=920).pack(anchor="w", pady=8)
        connection = ttk.Frame(body)
        connection.pack(fill="x")
        self.config_vars = {}
        self.config_widgets = []
        choices = [
            ("port", "设备", initial_port, None),
            ("baud", "波特率", "921600", None),
            ("bits", "数据位", "8", [5, 6, 7, 8]),
            ("parity", "校验", "N", ["N", "E", "O", "S", "M"]),
            ("stop", "停止位", "1", ["1", "1.5", "2"]),
            ("flow", "流控", "none", ["none", "hardware", "software"]),
        ]
        for column, (key, label, default, values) in enumerate(choices):
            ttk.Label(connection, text=label).grid(row=0, column=column, sticky="w")
            variable = tk.StringVar(value=default)
            widget = ttk.Combobox(
                connection,
                textvariable=variable,
                values=values or [],
                width=20 if key == "port" else 10,
                state="normal" if values is None else "readonly",
            )
            widget.grid(row=1, column=column, sticky="ew", padx=(0, 7))
            self.config_vars[key] = variable
            self.config_widgets.append((widget, "normal" if values is None else "readonly"))
            connection.columnconfigure(column, weight=1)
        actions = ttk.Frame(body)
        actions.pack(fill="x", pady=10)
        ttk.Button(actions, text="刷新设备", command=self.refresh).pack(side="left")
        ttk.Button(actions, text="连接", command=self.connect).pack(side="left", padx=8)
        ttk.Button(actions, text="断开", command=self.disconnect).pack(side="left")
        self.remote_crc = tk.StringVar(value="ccitt_false")
        ttk.Label(actions, text="遥控 CRC").pack(side="left", padx=(18, 5))
        self.remote_choice = ttk.Combobox(
            actions,
            textvariable=self.remote_crc,
            width=18,
            values=["ccitt_false", "official_sample"],
            state="readonly",
        )
        self.remote_choice.pack(side="left")
        ttk.Separator(body).pack(fill="x", pady=5)
        self.demo = tk.BooleanVar(value=False)
        ttk.Checkbutton(body, text="发送 PIP1 四轴样例（自定义调试数据）", variable=self.demo).pack(
            anchor="w"
        )
        self.payload = tk.StringVar(value="00 A5 FF")
        ttk.Label(body, text="原始负载 HEX（0..60字节；未勾选样例时发送）").pack(
            anchor="w", pady=(6, 2)
        )
        ttk.Entry(body, textvariable=self.payload).pack(fill="x")
        fields = ttk.Frame(body)
        fields.pack(fill="x", pady=8)
        self.fields = {}
        for index, (name, default) in enumerate(
            [(n, "0") for n in ("x", "y", "yaw", "pitch", "buttons", "counter", "seq")]
            + [("rate", "50")]
        ):
            ttk.Label(fields, text=name + (" / Hz" if name == "rate" else "")).grid(
                row=0, column=index, sticky="w"
            )
            variable = tk.StringVar(value=default)
            ttk.Entry(fields, textvariable=variable, width=10).grid(
                row=1, column=index, padx=(0, 6), sticky="ew"
            )
            fields.columnconfigure(index, weight=1)
            self.fields[name] = variable
        sending = ttk.Frame(body)
        sending.pack(fill="x", pady=6)
        ttk.Button(sending, text="发送一帧", command=lambda: self.send("once")).pack(side="left")
        ttk.Button(sending, text="开始周期发送", command=lambda: self.send("start")).pack(
            side="left", padx=8
        )
        ttk.Button(sending, text="停止发送", command=lambda: self.send("stop")).pack(side="left")
        self.show_demo = tk.BooleanVar(value=False)
        ttk.Checkbutton(sending, text="接收按 PIP1 样例解析", variable=self.show_demo).pack(
            side="right"
        )
        self.counts = tk.StringVar(value="TX 0 / RX 0 / 遥控 0 / CRC或残帧错误 0")
        ttk.Label(body, textvariable=self.counts).pack(anchor="w", pady=8)
        self.received = tk.StringVar(value="等待有效帧；作为接收端时仅连接，无需点击发送")
        ttk.Label(body, textvariable=self.received, wraplength=920).pack(anchor="w")
        compare = ttk.Frame(body)
        compare.pack(fill="x", pady=5)
        self.expected = tk.StringVar()
        ttk.Label(compare, text="期望负载 HEX").pack(side="left")
        ttk.Entry(compare, textvariable=self.expected).pack(
            side="left", fill="x", expand=True, padx=8
        )
        ttk.Button(compare, text="比较最近负载", command=self.compare).pack(side="right")
        self.log = ScrolledText(
            body, height=9, state="disabled", wrap="none", font=("Consolas", 10)
        )
        self.log.pack(fill="both", expand=True, pady=(8, 0))
        self.refresh()
        self.root.protocol("WM_DELETE_WINDOW", self.close)
        self.root.after(50, self.poll)

    def refresh(self):
        """刷新枚举但不替用户选择/打开设备，仍可手填Linux稳定路径。"""
        self.config_widgets[0][0]["values"] = [port.device for port in list_ports.comports()]

    def connect(self):
        """整组验证由工作线程执行；活动连接禁止改参数，不隐式重连。"""
        if self.session is not None:
            self.status.set("请先断开当前连接")
            return
        try:
            config = {name: value.get() for name, value in self.config_vars.items()}
            config["bits"] = int(config["bits"])
            config["baud"] = int(config["baud"])
            self.session = SerialSession(config, self.remote_crc.get())
            for widget, _ in self.config_widgets:
                widget.configure(state="disabled")
            self.remote_choice.configure(state="disabled")
            self.status.set("正在连接")
            self.session.start()
        except ValueError as error:
            self.status.set(str(error))

    def disconnect(self):
        """取消并join工作线程后再释放UI引用；不保留待发送命令。"""
        if self.session is not None:
            self.session.stopping.set()
            self.session.join()
            self.session = None
        self.cyclic = False
        for widget, state in self.config_widgets:
            widget.configure(state=state)
        self.remote_choice.configure(state="readonly")
        self.status.set("已断开")

    def send(self, command):
        """生成发送快照；周期中禁止单发重置序号，停止后才能更改发送参数。"""
        try:
            if self.session is None or not self.session.is_alive():
                raise ValueError("请先连接设备")
            if self.cyclic and command != "stop":
                raise ValueError("请先停止周期发送，再修改参数或单发")
            settings = None
            if command != "stop":
                rate = float(self.fields["rate"].get())
                if not math.isfinite(rate) or not 1 <= rate <= 75:
                    raise ValueError("发送频率须为1..75Hz")
                seq = int(self.fields["seq"].get(), 0)
                counter = int(self.fields["counter"].get(), 0)
                buttons = int(self.fields["buttons"].get(), 0)
                axes = tuple(float(self.fields[name].get()) for name in ("x", "y", "yaw", "pitch"))
                payload = bytes.fromhex(self.payload.get())
                settings = dict(
                    rate=rate,
                    seq=seq,
                    counter=counter,
                    buttons=buttons,
                    axes=axes,
                    payload=payload,
                    demo=self.demo.get(),
                )
                encode_frame(
                    encode_demo(counter, axes, buttons) if settings["demo"] else payload, seq
                )
            self.session.submit((command, settings))
            self.cyclic = command == "start"
            self.status.set("周期发送中" if self.cyclic else "发送命令已提交")
        except (ValueError, queue.Full) as error:
            self.status.set(str(error) or "发送命令忙，请稍后重试")

    def compare(self):
        """逐字节比较最近自定义负载，空HEX明确表示期望零长度，不比较串口序号。"""
        try:
            if self.latest_rx is None:
                raise ValueError("尚未收到自定义帧")
            expected = bytes.fromhex(self.expected.get())
            actual = bytes.fromhex(self.latest_rx["hex"])
            self.status.set(
                f"负载完全一致（{len(actual)}字节）"
                if actual == expected
                else f"负载不一致：收到{actual.hex(' ')}"
            )
        except ValueError as error:
            self.status.set(str(error))

    def poll(self):
        """每批最多200事件；日志只追加新行并裁至120行，避免高频重排整个文本。"""
        session = self.session
        if session is not None:
            new_logs = []
            for _ in range(200):
                try:
                    kind, value, counts = session.events.get_nowait()
                except queue.Empty:
                    break
                self.last_event = (kind, value)
                self.counts.set(
                    "TX {tx} / RX {rx} / 遥控 {remote} / 错误 {errors} / 显示丢弃 {display_drops}".format(
                        **counts
                    )
                )
                if kind in ("state", "error", "warning"):
                    self.status.set(value)
                elif kind == "rx" and value["kind"] == "custom":
                    self.latest_rx = value
                    text = f"{value['command']} seq={value['seq']} len={value['length']} HEX={value['hex']}"
                    if self.show_demo.get():
                        from serial_protocol import decode_demo

                        try:
                            text += "\n" + json.dumps(
                                decode_demo(bytes.fromhex(value["hex"])), ensure_ascii=False
                            )
                        except ValueError as error:
                            text += "\n" + str(error)
                    self.received.set(text)
                elif kind == "tx":
                    # 手动下一帧从最新序号继续；编辑框仍允许用户在停止后显式改序号。
                    self.fields["seq"].set(str((value["seq"] + 1) & 255))
                # 中性遥控帧约71Hz，计数保留，日志只显示自定义帧和错误以保持可读。
                if kind != "rx" or value["kind"] != "remote":
                    line = f"{time.strftime('%H:%M:%S')} {kind} {value}"
                    new_logs.append(line)
            if not session.is_alive():
                last_status = self.status.get()
                self.disconnect()
                self.status.set(last_status + "；连接已关闭")
            if new_logs:
                self.log.configure(state="normal")
                self.log.insert("end", "\n".join(new_logs) + "\n")
                # Tk保留末尾空行；按逻辑行裁剪，隐藏窗口不触发滚动布局。
                excess = int(self.log.index("end-1c").split(".")[0]) - 121
                if excess > 0:
                    self.log.delete("1.0", f"{excess + 1}.0")
                if self.log.winfo_viewable():
                    self.log.see("end")
                self.log.configure(state="disabled")
        self.root.after(50, self.poll)

    def close(self):
        """窗口关闭先停止串口线程，再销毁Tk资源，避免后台继续发送。"""
        self.disconnect()
        self.root.destroy()


def main():
    """默认GUI；CLI接收有界退出，输出JSON行，期望数据不满足返回非零退出码。"""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="")
    parser.add_argument("--baud", type=int, default=921600)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument(
        "--receive", action="store_true", help="解码并输出JSON行，默认只输出A5自定义帧"
    )
    parser.add_argument("--seconds", type=float, default=30)
    parser.add_argument("--expect-hex", help="接收模式校验0311负载；不匹配立即失败")
    parser.add_argument("--min-frames", type=int, default=1)
    parser.add_argument("--demo", action="store_true", help="接收时显式解码PIP1样例")
    parser.add_argument("--show-remote", action="store_true")
    parser.add_argument(
        "--remote-crc", choices=["ccitt_false", "official_sample"], default="ccitt_false"
    )
    mode.add_argument("--send-hex", help="CLI发送一帧0311，保留全部原始字节")
    parser.add_argument("--seq", type=int, default=0)
    mode.add_argument("--pair-test", nargs=2, metavar=("发送端", "接收端"))
    arguments = parser.parse_args()
    try:
        if arguments.pair_test:
            pair_test(*arguments.pair_test)
        elif arguments.receive:
            if (
                not math.isfinite(arguments.seconds)
                or not 0 < arguments.seconds <= 3600
                or arguments.min_frames < 1
            ):
                raise ValueError("接收时长须为0..3600秒（不含0），最小帧数须≥1")
            expected = None if arguments.expect_hex is None else bytes.fromhex(arguments.expect_hex)
            decoder = FrameDecoder(remote_crc=arguments.remote_crc)
            custom_count = 0
            with open_port(arguments.port, arguments.baud) as port:
                end = time.monotonic() + arguments.seconds
                while time.monotonic() < end:
                    for frame in decoder.feed(port.read(4096)):
                        if frame.command == 0x0311:
                            custom_count += 1
                            if expected is not None and frame.payload != expected:
                                raise ValueError(f"负载不一致：{frame.payload.hex()}")
                        if frame.kind == "custom" or arguments.show_remote:
                            print(
                                json.dumps(frame.record(demo=arguments.demo), ensure_ascii=False),
                                flush=True,
                            )
                    time.sleep(0.004)
            print(
                f"0311={custom_count}, accepted={decoder.accepted}, errors={decoder.errors}",
                file=sys.stderr,
            )
            if custom_count < arguments.min_frames:
                raise ValueError("收到的0311帧数不足")
        elif arguments.send_hex is not None:
            packet = encode_frame(bytes.fromhex(arguments.send_hex), arguments.seq)
            with open_port(arguments.port, arguments.baud) as port:
                write_frame(port, packet)
                port.flush()
            print(packet.hex(" "))
        else:
            import tkinter as tk

            root = tk.Tk()
            window = ControlWindow(root, arguments.port)
            window.config_vars["baud"].set(str(arguments.baud))
            root.mainloop()
    except (OSError, ValueError, serial.SerialException) as error:
        parser.exit(1, f"错误：{error}\n")


if __name__ == "__main__":
    main()
