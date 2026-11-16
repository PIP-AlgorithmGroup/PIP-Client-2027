"""分析手动 Qt/原生视频实验；所有时间戳须为同机 steady_clock 微秒。

跳过每阶段前后 1.5 秒，排除创建/终止独立遮挡进程的同步等待。
low1 是本稳定阶段最慢 1% 新视频提交间隔均值的倒数；不是产品的滚动 10 秒窗口。
输出只能证明应用提交节拍，不能代替 PresentMon/光电测量的实际显示时间。
"""

import argparse
import json
import math
from pathlib import Path


def distribution(values, *, intervals=False):
    """给毫秒样本计算 nearest-rank 分位；仅间隔输出 low，少于 100 项时无 low1。"""
    ordered = sorted(values)
    if not ordered:
        return None

    def percentile(fraction):
        return ordered[max(0, math.ceil(len(ordered) * fraction) - 1)]

    result = {
        "n": len(ordered),
        "mean": sum(ordered) / len(ordered),
        "p95": percentile(0.95),
        "p99": percentile(0.99),
        "max": ordered[-1],
    }
    if intervals:
        count = max(1, math.ceil(len(ordered) * 0.01))
        result["low1"] = (
            1000 / (sum(ordered[-count:]) / count) if len(ordered) >= 100 else None
        )
    return result


def analyze(trace):
    """返回各稳定阶段的输入、输出、等待与窗口状态；拒绝失败或旧格式实验。"""
    if trace.get("failure"):
        raise ValueError(trace["failure"])
    if "senderUs" not in trace:
        raise ValueError("发送时间线缺失，不能使用旧实验二进制")
    native = trace.get("noUdp", False)
    phases = [phase for phase in trace["phases"] if not phase["name"].startswith("block-")]
    report = []
    spans = [("sourceToPresent" if native else "receive", "firstUs", "swapUs")]
    if native:
        spans += [("decode", "firstUs", "decodedUs"),
                  ("wake", "decodedUs", "pickedUs"), ("submit", "pickedUs", "swapUs")]
    elif trace.get("independentRhi", False):
        spans += [("decodeAndAssembly", "firstUs", "decodedUs"),
                  ("mailbox", "decodedUs", "pickedUs"),
                  ("texture", "pickedUs", "readyUs"),
                  ("render", "renderingUs", "renderedUs"),
                  ("afterRender", "renderedUs", "swapUs")]
    else:
        spans += [("decodeAndAssembly", "firstUs", "decodedUs"),
                  ("mailbox", "decodedUs", "guiUs"),
                  ("guiToBegin", "requestedUs", "beginUs"),
                  ("beginToSync", "beginUs", "syncUs"),
                  ("sync", "syncUs", "syncedUs"),
                  ("render", "renderingUs", "renderedUs"),
                  ("afterRender", "renderedUs", "swapUs")]
    for index, phase in enumerate(phases):
        lower = phase["atUs"] + 1_500_000
        upper = (phases[index + 1]["atUs"] if index + 1 < len(phases)
                 else trace["endedUs"]) - 1_500_000
        if upper <= lower:
            raise ValueError("阶段太短，无法剔除窗口切换边界")
        frames = [frame for frame in trace["frames"] if lower <= frame["swapUs"] < upper]
        sent = [at for at in trace["senderUs"] if lower <= at < upper]
        states = [state for state in trace["windowStates"] if lower <= state["atUs"] < upper]
        events = [event for event in trace.get("costlyGuiEvents", [])
                  if lower <= event["atUs"] < upper]
        active_count = sum(state["active"] for state in states)
        if phase["name"] in ("unfocused", "covered") and states:
            if active_count > len(states) / 5:
                raise ValueError(f"{phase['name']} 未取得预期失焦状态，拒绝作为后台实验")
            if any("coverRunning" in state and not state["coverRunning"] for state in states):
                raise ValueError(f"{phase['name']} 遮挡程序提前退出")
            # 新格式已使用原生像素坐标；旧格式在混合 DPI 下的中心命中不作严格证据。
            native_geometry_states = [state for state in states if "screenName" in state]
            if phase["name"] == "covered" and native_geometry_states:
                covered_count = sum(state["centerCoveredByProbe"] for state in native_geometry_states)
                if covered_count < len(native_geometry_states) * 0.8:
                    raise ValueError("完全遮挡阶段的原生中心命中验证失败")
        result = {
            "phase": phase["name"], "durationMs": (upper - lower) / 1000,
            "fps": len(frames) / ((upper - lower) / 1e6),
            "interval": distribution([(right["swapUs"] - left["swapUs"]) / 1000
                                      for left, right in zip(frames, frames[1:])], intervals=True),
            "senderInterval": distribution(
                [(right - left) / 1000 for left, right in zip(sent, sent[1:])], intervals=True
            ),
            "stages": {label: distribution([(frame[end] - frame[start]) / 1000
                                            for frame in frames if frame[start] >= 0
                                            and frame[end] >= frame[start]])
                       for label, start, end in spans},
            "active": active_count, "stateSamples": len(states),
            "screens": sorted({(state.get("screenName", "unrecorded"),
                                state.get("screenHz", -1), state.get("devicePixelRatio", -1))
                               for state in states}),
            "heartbeat": distribution([tick["intervalUs"] / 1000
                                        for tick in trace.get("heartbeats", [])
                                        if lower <= tick["atUs"] < upper]),
            "costlyGuiEvents": sorted(
                events, key=lambda event: event["durationUs"], reverse=True
            )[:20],
        }
        # 帧号差只描述从已显示 AU 间跳过的编号，不冒充网络丢包或准确邮箱覆盖次数。
        result["skippedPresentedIds"] = sum(
            max(0, ((right["frameId"] - left["frameId"]) % 65536) - 1)
            for left, right in zip(frames, frames[1:])
        )
        result["timerPolicies"] = sorted({(state.get("timerPolicy", {}).get("control", -1),
                                            state.get("timerPolicy", {}).get("state", -1))
                                           for state in states})
        report.append(result)
    return report


def main():
    """读取显式指定的 JSON，旁写 summary；不操作窗口或产品配置。"""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("trace", type=Path)
    args = parser.parse_args()
    report = analyze(json.loads(args.trace.read_text(encoding="utf-8")))
    args.trace.with_name(args.trace.stem + "_summary.json").write_text(
        json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8", newline="\n")
    for phase in report:
        print(phase["phase"], "OUT", phase["interval"], "SOURCE", phase["senderInterval"])


if __name__ == "__main__":
    main()
