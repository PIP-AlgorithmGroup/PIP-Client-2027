"""串行运行手动前后台视频实验；需要预先成功构建的目标和本机 Qt 运行环境。

只启动本实验拥有的窗口；不停止用户程序。Qt 模式走完整 QML/UDP/硬件显示，
原生模式是 Windows 无 HUD/UDP 的架构对照。性能阈值失败返回 1，不等同功能回归失败。
"""

import argparse
import json
import os
import re
import subprocess
from pathlib import Path

from analyze_video_presentation import analyze


def main():
    """通过显式路径选择构建、码流和产物目录，不依赖 cwd 或开发机固定路径。"""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", required=True, type=Path)
    parser.add_argument("--stream", required=True, type=Path)
    parser.add_argument("--output-dir", required=True, type=Path)
    parser.add_argument("--name", required=True)
    parser.add_argument(
        "--stage-ms", type=int, default=8000, choices=range(5000, 30001), metavar="5000..30000"
    )
    parser.add_argument("--platform", default="windows" if os.name == "nt" else "wayland")
    parser.add_argument(
        "--required-path", default="d3d11-shared-yuv" if os.name == "nt" else "yuv-upload"
    )
    parser.add_argument("--timer-guard", action="store_true")
    parser.add_argument("--precise-pacer", action="store_true")
    parser.add_argument("--no-overlay", action="store_true")
    parser.add_argument("--replay-network-poll", action="store_true")
    parser.add_argument("--configured-network", action="store_true")
    parser.add_argument("--idle-ms", type=int, choices=range(0, 101), metavar="0..100")
    parser.add_argument("--native", action="store_true")
    parser.add_argument("--minimum-low-fps", type=float, default=50)
    args = parser.parse_args()
    if not re.fullmatch(r"[A-Za-z0-9_-]+", args.name):
        parser.error("name 只能包含字母、数字、下划线和连字符")
    if args.native and (
        os.name != "nt" or args.timer_guard or args.no_overlay or args.idle_ms is not None
        or args.replay_network_poll or args.configured_network
    ):
        parser.error("原生实验仅用于 Windows，不能附加 Qt 专用变量")
    if args.precise_pacer and os.name != "nt":
        parser.error("此诊断发送器的高精度 waitable timer 对照仅适用于 Windows")
    build = args.build_dir.resolve()
    stream = args.stream.resolve()
    folder = args.output_dir.resolve()
    folder.mkdir(parents=True, exist_ok=True)
    suffix = ".exe" if os.name == "nt" else ""
    # Linux CMake 在 tests 子目录生成工具；Windows 生成器把 exe 放在构建根目录。
    binaries = build if os.name == "nt" else build / "tests"
    target_name = "video_native_present_probe" if args.native else "welcome_ui_test"
    target = binaries / (target_name + suffix)
    cover = binaries / ("video_window_cover" + suffix)
    for path in (target, cover, stream):
        if not path.is_file():
            parser.error(f"缺少输入或已构建目标: {path}")
    output = folder / (args.name + ".json")
    env = os.environ.copy()
    for key in (
        "QT_QUICK_BACKEND", "QSG_RHI_BACKEND", "QT_QPA_UPDATE_IDLE_TIME",
        "PIP_VIDEO_PRESENTATION", "PIP_VIDEO_DECODER", "PIP_VIDEO_PROBE_NO_OVERLAY",
        "PIP_VIDEO_PROBE_PACER", "PIP_VIDEO_PROBE_REPLAY_NETWORK",
        "PIP_VIDEO_PROBE_CONFIGURED_NETWORK"
    ):
        env.pop(key, None)
    env.update(QT_QPA_PLATFORM=args.platform, QSG_RENDER_LOOP="threaded",
               PIP_UI_TEST_VERTICAL_SYNC="0", PIP_VIDEO_TEST_STREAM=str(stream),
               PIP_VIDEO_TEST_REQUIRED_PATH=args.required_path, PIP_VIDEO_PROBE_COVER=str(cover),
               PIP_VIDEO_PROBE_OUTPUT=str(output), PIP_VIDEO_PROBE_STAGE_MS=str(args.stage_ms),
               PIP_VIDEO_PROBE_TIMER="1" if args.timer_guard else "0")
    if args.precise_pacer:
        env["PIP_VIDEO_PROBE_PACER"] = "precise"
    if args.no_overlay:
        env["PIP_VIDEO_PROBE_NO_OVERLAY"] = "1"
    if args.replay_network_poll:
        env["PIP_VIDEO_PROBE_REPLAY_NETWORK"] = "1"
    if args.configured_network:
        env["PIP_VIDEO_PROBE_CONFIGURED_NETWORK"] = "1"
    if args.idle_ms is not None:
        env["QT_QPA_UPDATE_IDLE_TIME"] = str(args.idle_ms)
    if args.native:
        command = [str(target), str(stream), str(output), str(cover), str(args.stage_ms)]
    else:
        command = [str(target), "hdVideoReportsActualBackendAndPresentation", "-o",
                   str(folder / (args.name + ".txt")) + ",txt"]
    # 防止失败后误读上次同名结果。只移除该次调用明确拥有的输出文件。
    output.unlink(missing_ok=True)
    with (folder / (args.name + "_stderr.txt")).open("wb") as log:
        result = subprocess.run(command, env=env, stdout=log, stderr=log,
                                timeout=6 * args.stage_ms / 1000 + 60,
                                creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
    if result.returncode:
        raise RuntimeError(f"实验功能失败 {result.returncode}，检查 {folder / args.name}")
    trace = json.loads(output.read_text(encoding="utf-8"))
    report = analyze(trace)
    (folder / (args.name + "_summary.json")).write_text(
        json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8", newline="\n")
    for phase in report:
        print(phase["phase"], "OUT", phase["interval"], "SOURCE", phase["senderInterval"],
              flush=True)
    bad = [phase["phase"] for phase in report
           if phase["phase"] in ("foreground", "unfocused", "covered", "recovered")
           and (not phase["interval"] or phase["interval"]["low1"] is None
                or phase["interval"]["low1"] < args.minimum_low_fps)]
    print("LOW_CHECK", "FAIL " + ",".join(bad) if bad else "PASS", flush=True)
    return bool(bad)


if __name__ == "__main__":
    raise SystemExit(main())
