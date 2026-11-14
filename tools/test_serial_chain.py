"""显式硬件验收：实体输入→产品MQTT，再重放实际MQTT字节→MockServer→实体输出。

两个实体端口在两阶段间移交所有权，不伪称为两段同时使用实体设备的连续链路。
可选 --linux-probe 在WSL中用PTY代替第一段，追加验证连续链路。
只启动本次专有MockServer/临时broker，停止顺序为桥接→MQTT→串口→broker→进程。
"""

import argparse
import json
import os
from pathlib import Path
import queue
import socket
import subprocess
import threading
import time
import urllib.error
import urllib.request
import uuid

import paho.mqtt.client as mqtt

from serial_control import open_port, write_frame
from serial_protocol import FrameDecoder, encode_demo, encode_frame


def free_port():
    """选择本机临时TCP端口，绑定失败仍由真实服务报告，不能把已有服务当成测试服务。"""
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        return listener.getsockname()[1]


def wait_until(operation, timeout=8):
    """用有界轮询等待测试条件；operation返回真值即成功，异常保留真实原因。"""
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        result = operation()
        if result:
            return result
        time.sleep(0.03)
    raise TimeoutError("等待测试条件超时")


class MockSession:
    """仅管理本次子进程HTTP端点；不连接用户已有服务或修改浏览器/磁盘设置。"""

    def __init__(self, port):
        self.base = f"http://127.0.0.1:{port}/api/v1"

    def get(self, path):
        """读取本测试服务的管理JSON，HTTP请求最多等待2秒。"""
        with urllib.request.urlopen(self.base + path, timeout=2) as response:
            return json.load(response)

    def execute(self, target, action, parameters=None):
        """等待本地命令完成；仅未受理的revision冲突允许在控制状态不变时重试一次。"""
        snapshot = self.get("/snapshot")
        body = dict(
            session_id=snapshot["session_id"],
            expected_revision=snapshot["revision"],
            key=str(uuid.uuid4()),
            target=target,
            action=action,
            parameters=parameters or {},
        )
        for attempt in range(2):
            request = urllib.request.Request(
                self.base + "/commands",
                data=json.dumps(body).encode(),
                headers={"Content-Type": "application/json"},
                method="POST",
            )
            try:
                with urllib.request.urlopen(request, timeout=2) as response:
                    receipt = json.load(response)
                break
            except urllib.error.HTTPError as error:
                detail = json.load(error)
                if attempt or error.code != 409 or detail.get("error") != "revision_conflict":
                    raise RuntimeError(f"{target}/{action}: HTTP {error.code}: {detail}") from error
                current = self.get("/snapshot")

                # broker日志等观测事件也增加revision。只忽略计数/日志，保留所有模块的
                # 生命周期、错误、可用动作与配置基线；跨会话或真实状态变化绝不重试。
                def controls(value):
                    return [
                        {
                            key: item
                            for key, item in module.items()
                            if key in ("id", "state", "error", "actions") or key.endswith("_config")
                        }
                        for module in value["modules"]
                    ]

                if current["session_id"] != snapshot["session_id"] or controls(current) != controls(
                    snapshot
                ):
                    raise RuntimeError(f"{target}/{action}: 冲突期间控制状态改变") from error
                body["expected_revision"] = current["revision"]
        result = wait_until(lambda: self._completed(receipt["request_id"]))
        if result["state"] != "completed":
            raise RuntimeError(f"{target}/{action}: {result}")
        return result

    def _completed(self, request_id):
        result = self.get("/commands/" + request_id)
        return result if result["state"] != "accepted" else None


def start_probe(executable, receive_port, mqtt_port, output):
    """启动实际产品链路并等待ready；stdout只由一个reader线程消费，退出回收线程。"""
    process = subprocess.Popen(
        [str(executable), "--port", receive_port, "--mqtt-port", str(mqtt_port), "--seconds", "5"],
        stdout=subprocess.PIPE,
        stderr=output,
        text=True,
        creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0,
    )
    lines = queue.Queue(maxsize=32)

    def read_lines():
        for line in process.stdout:
            output.write(line)
            output.flush()
            lines.put(line.rstrip())

    reader = threading.Thread(target=read_lines)
    reader.start()
    try:
        end = time.monotonic() + 4
        while time.monotonic() < end:
            try:
                line = lines.get(timeout=0.1)
            except queue.Empty:
                if process.poll() is not None:
                    raise RuntimeError(f"产品probe退出 {process.returncode}")
                continue
            if line == "ready":
                return process, reader
        raise TimeoutError("产品probe未ready")
    except BaseException:
        process.terminate()
        process.wait(timeout=5)
        reader.join(timeout=2)
        raise


def run(arguments):
    """两段逐字节验证；测试文件仅写显式output目录，finally释放所有本轮资源。"""
    arguments.output.mkdir(parents=True, exist_ok=True)
    http_port, mqtt_port = free_port(), free_port()
    service = MockSession(http_port)
    backend = None
    probe = None
    reader = None
    subscriber = None
    received = queue.Queue(maxsize=128)
    ready = threading.Event()
    records = []
    with (arguments.output / "mockserver.txt").open("w", encoding="utf-8") as log, (
        arguments.output / "probe.txt"
    ).open("w", encoding="utf-8") as probe_log:
        try:
            backend = subprocess.Popen(
                [
                    str(arguments.mockserver.resolve()),
                    "--port",
                    str(http_port),
                    "--run-for-ms",
                    "45000",
                ],
                stdout=log,
                stderr=log,
                creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0,
            )

            def health():
                if backend.poll() is not None:
                    raise RuntimeError(f"MockServer启动失败 {backend.returncode}")
                try:
                    return service.get("/health")
                except (urllib.error.URLError, TimeoutError):
                    return None

            wait_until(health)
            service.execute("broker", "start", {"port": mqtt_port, "bind": "0.0.0.0"})
            subscriber = mqtt.Client(
                mqtt.CallbackAPIVersion.VERSION2,
                client_id="pip_hardware_test_" + uuid.uuid4().hex[:12],
            )
            subscriber.on_connect = lambda client, *_: client.subscribe("CustomControl", qos=1)
            subscriber.on_subscribe = lambda *_: ready.set()

            def on_message(client, userdata, message):
                try:
                    received.put_nowait(bytes(message.payload))
                except queue.Full:
                    ready.clear()  # 过载使验收显式失败，禁止静默丢失测试结果。

            subscriber.on_message = on_message
            subscriber.connect("127.0.0.1", mqtt_port, 10)
            subscriber.loop_start()
            if not ready.wait(4):
                raise TimeoutError("MQTT测试订阅未就绪")
            probe, reader = start_probe(
                arguments.probe.resolve(), arguments.rx, mqtt_port, probe_log
            )
            payloads = [
                b"",
                bytes.fromhex("00a5ff"),
                encode_demo(123, (1, -2, 0.5, 3), 5),
                bytes(range(60)),
            ] * 5
            with open_port(arguments.tx) as source:
                for seq, payload in enumerate(payloads):
                    write_frame(source, encode_frame(payload, seq))
                    protobuf = received.get(timeout=2)
                    # 当前官方CustomControl仅optional bytes data=1；容量≤60，长度varint为1字节。
                    expected = b"\x0a" + bytes([len(payload)]) + payload
                    if protobuf != expected:
                        raise AssertionError(
                            f"产品MQTT负载不同：{protobuf.hex()} != {expected.hex()}"
                        )
                    records.append(
                        dict(
                            stage="physical_input",
                            seq=seq,
                            payload=payload.hex(),
                            protobuf=protobuf.hex(),
                        )
                    )
                    time.sleep(0.03)
            if probe.wait(timeout=8) != 0:
                raise RuntimeError("产品probe异常退出")
            reader.join(timeout=2)
            probe = None
            print(f"PASS 实体输入→产品SerialController→MQTT: {len(payloads)}帧")
            service.execute("serial", "open", {"port": arguments.tx, "baud": 921600})
            service.execute("mqtt", "start", {"host": "127.0.0.1", "port": mqtt_port})
            decoder = FrameDecoder()
            remote_count = 0
            with open_port(arguments.rx) as destination:
                destination.reset_input_buffer()
                service.execute("bridge", "start")
                for record in list(records):
                    publication = subscriber.publish(
                        "CustomControl", bytes.fromhex(record["protobuf"]), qos=0
                    )
                    publication.wait_for_publish(timeout=2)
                    end = time.monotonic() + 2
                    custom = []
                    while not custom and time.monotonic() < end:
                        for frame in decoder.feed(destination.read(4096)):
                            if frame.kind == "remote":
                                remote_count += 1
                            elif frame.command == 0x0311:
                                custom.append(frame)
                        time.sleep(0.004)
                    if len(custom) != 1 or custom[0].payload.hex() != record["payload"]:
                        raise AssertionError("MockServer实体串口负载不一致或超时")
                    records.append(dict(stage="physical_output", **custom[0].record()))
                    time.sleep(0.03)
                service.execute("bridge", "stop")
            if remote_count == 0:
                raise AssertionError("未收到CRC有效的A9 53遥控帧，不能判定混合流通过")
            print(
                f"PASS 实际MQTT字节→MockServer→实体串口→Python解码: {len(payloads)}帧; {remote_count}个遥控帧CRC有效"
            )
            if arguments.linux_probe:
                # 两只实体端口归MockServer及解码器，输入段用Linux PTY，使整条路可同时运行。
                repository = Path(__file__).resolve().parents[1]
                relative = arguments.output.resolve().relative_to(repository)
                payload_file = arguments.output / "pty_payloads.json"
                payload_file.write_text(
                    json.dumps([value.hex() for value in payloads]), encoding="utf-8"
                )
                linux_payloads = (
                    arguments.linux_workspace.rstrip("/")
                    + "/"
                    + relative.as_posix()
                    + "/pty_payloads.json"
                )
                source_log_path = arguments.output / "pty_source.txt"
                with source_log_path.open("w", encoding="utf-8") as source_log, open_port(
                    arguments.rx
                ) as destination:
                    destination.reset_input_buffer()
                    decoder = FrameDecoder()
                    service.execute("bridge", "start")
                    command = [
                        "wsl",
                        "-d",
                        arguments.wsl_distribution,
                        "--",
                        "env",
                        "LD_LIBRARY_PATH=" + arguments.linux_library_path,
                        "python3",
                        arguments.linux_workspace.rstrip("/") + "/tools/serial_pty_source.py",
                        "--probe",
                        arguments.linux_probe,
                        "--payloads",
                        linux_payloads,
                        "--mqtt-port",
                        str(mqtt_port),
                    ]
                    source = subprocess.Popen(
                        command,
                        stdout=source_log,
                        stderr=source_log,
                        creationflags=subprocess.CREATE_NO_WINDOW,
                    )
                    try:
                        actual = []
                        end = time.monotonic() + 16
                        while len(actual) < len(payloads) and time.monotonic() < end:
                            for frame in decoder.feed(destination.read(4096)):
                                if frame.command == 0x0311:
                                    actual.append(frame.payload)
                                    records.append(
                                        dict(
                                            stage="continuous_pty_input_physical_output",
                                            **frame.record(),
                                        )
                                    )
                            time.sleep(0.004)
                        if actual != payloads:
                            raise AssertionError(
                                f"连续链路不一致：期望{len(payloads)}帧，收到{len(actual)}帧；查看{source_log_path}"
                            )
                        if source.wait(timeout=10) != 0:
                            raise RuntimeError("WSL源异常退出，查看pty_source.txt")
                    finally:
                        if source.poll() is None:
                            # helper/probe均有有界生命周期；先等待，避免杀WSL宿主留下孤儿。
                            source.wait(timeout=15)
                        service.execute("bridge", "stop")
                print(
                    f"PASS 连续链路 Python→PTY→产品→MQTT→MockServer→COM4/COM5→Python: {len(payloads)}帧"
                )
            (arguments.output / "records.json").write_text(
                json.dumps(records, indent=2), encoding="utf-8"
            )
        finally:
            if probe is not None:
                probe.terminate()
                probe.wait(timeout=5)
            if reader is not None:
                reader.join(timeout=2)
            if subscriber is not None:
                subscriber.disconnect()
                subscriber.loop_stop()
            if backend is not None and backend.poll() is None:
                for target, action in (
                    ("bridge", "stop"),
                    ("mqtt", "stop"),
                    ("serial", "close"),
                    ("broker", "stop"),
                ):
                    try:
                        service.execute(target, action)
                    except (OSError, ValueError, RuntimeError, TimeoutError) as error:
                        print(f"清理 {target}: {error}")
                # 后端的有界生命周期负责最终RAII；Windows terminate不执行析构，必须先停托管broker。
                backend.terminate()
                backend.wait(timeout=5)
            for port in (http_port, mqtt_port):
                with socket.socket() as check:
                    if check.connect_ex(("127.0.0.1", port)) == 0:
                        raise RuntimeError(f"本轮端口未释放：{port}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tx", required=True, help="发送器/MockServer持有的设备，如COM4")
    parser.add_argument("--rx", required=True, help="客户端/解码器持有的另一端，如COM5")
    parser.add_argument("--probe", type=Path, required=True, help="serial_pipeline_probe可执行文件")
    parser.add_argument("--mockserver", type=Path, required=True, help="独立MockServer可执行文件")
    parser.add_argument("--output", type=Path, required=True, help="本轮验证证据输出目录")
    parser.add_argument(
        "--linux-probe", default="", help="可选：WSL中的probe绝对路径，追加连续链路"
    )
    parser.add_argument("--linux-workspace", default="", help="WSL中的同一仓库绝对路径")
    parser.add_argument("--linux-library-path", default="", help="WSL probe动态库搜索路径")
    parser.add_argument("--wsl-distribution", default="Ubuntu-22.04")
    arguments = parser.parse_args()
    if arguments.linux_probe and (os.name != "nt" or not arguments.linux_workspace):
        parser.error("WSL连续链路须Windows宿主和 --linux-workspace")
    run(arguments)
