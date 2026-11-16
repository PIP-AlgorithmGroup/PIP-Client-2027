import QtQuick
import QtQuick.Controls

// 本机图传诊断独立于视频内容和业务叠层；只读统计，不参与协议或解码控制。
Rectangle {
    id: debug
    // GUI 线程对象由宿主持有；view 的 OUT 是 Qt 呈交，不是显示器实际扫描。
    required property var receiver
    required property var view
    // 独立 UI 档位；窄窗口只省略单行尾部，不缩小字号或换行遮挡视频。
    required property real uiScale
    readonly property var metrics: receiver.diagnostics
    readonly property var outputMetrics: view.presentationDiagnostics
    function value(key, digits, source) {
        const number = (source || metrics)[key];
        return typeof number === "number" && isFinite(number) && number >= 0 ? number.toFixed(digits) : "—";
    }
    function stage(key) {
        const cost = (metrics.timings || {})[key] || {};
        return typeof cost.meanMs === "number" && cost.meanMs >= 0 ? cost.meanMs.toFixed(2) : "—";
    }
    width: Math.min(debugText.implicitWidth + 16 * uiScale, parent.width - 20 * uiScale)
    height: debugText.implicitHeight + 8 * uiScale
    radius: 3 * uiScale
    color: "#bf17191d"
    Text {
        id: debugText
        objectName: "videoDebugText"
        anchors.fill: parent
        anchors.leftMargin: 8 * debug.uiScale
        anchors.rightMargin: 8 * debug.uiScale
        text: "RX " + debug.value("receiveFps", 1) + " | DEC " + debug.value("decodeFps", 1) + " | OUT " + (debug.view.presentationFps >= 0 ? debug.view.presentationFps.toFixed(1) : "—") + " | OUT 5/1%L " + debug.value("low5Fps", 1, debug.outputMetrics) + "/" + debug.value("low1Fps", 1, debug.outputMetrics) + " | RX 5/1%L " + debug.value("low5Fps", 1) + "/" + debug.value("low1Fps", 1) + " | GAP1s " + debug.value("frameLossPercent", 1) + "% | UDP —" + " | " + debug.value("megabitsPerSecond", 2) + " Mb/s" + " | C/D/R " + debug.stage("codec") + "/" + debug.stage("download") + "/" + debug.stage("convert") + " ms " + (debug.metrics.decoder || "—")
        font.pixelSize: 11 * debug.uiScale
        color: "#f5f5f6"
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
        maximumLineCount: 1
    }
    HoverHandler {
        id: hover
    }
    ToolTip.visible: hover.hovered
    ToolTip.delay: 600
    ToolTip.text: "RX：最近 1 秒应用完整收帧；DEC：本代有效解码输出；OUT：含新视频纹理的 Qt 呈交。\n" + "OUT 5/1%L：最近 10 秒最慢 5%/1% 新视频呈交间隔的平均值换算，不是显示器扫描；RX low 使用收帧间隔。\n" + "两组 low 均至少需要 20/100 个间隔，样本不足显示 —；不能用平均 FPS 代替 low。\n" + "GAP1s：最近 1 秒检测到的整帧序号缺口率，含发送跳帧和重组失败，不等于 UDP 丢包率。\n" + "UDP：协议无分片总数且允许变长分片，无法准确测量包损。\n" + "C/D/R：最近 1 秒 codec 调用 / 硬件帧下载 / RGBA 转换的平均毫秒；正常通路保留 YUV 引用，不执行的阶段显示 —。显示回退的下载和转色另计入纹理准备。\n" + "解码平均 / 最大（最近 1 秒）：" + debug.value("decodeMs", 1) + " / " + debug.value("decodeMaxMs", 1) + " ms，正常通路止于帧引用就绪；不是端到端延迟。\n" + "最近一次解码排队：" + debug.value("decodeQueueMs", 1) + " ms；本地待解码丢弃：" + debug.value("decodeQueueDropped", 0) + " 帧（本会话累计，不是网络包损）。\n" + "后端：" + (debug.metrics.decoder || "尚未选择") + "，设备：" + (debug.metrics.decoderDevice || "—") + "；? 表示尚未验证硬件帧输出。\n" + "显示路径：" + (debug.outputMetrics.renderPath || "—") + "；显示回退：" + (debug.outputMetrics.renderFallback || "无") + "\n" + "回退记录：" + (debug.metrics.decoderFallback || "无") + "\n" + "收包与解码分属两线程；RX 仍受系统调度和读包批次影响，不代表网卡硬件到包时刻。\n" + "最近 1 秒：非法包 " + debug.value("invalidPackets", 0) + "，重复分片 " + debug.value("duplicatePackets", 0) + "，未完成帧 " + debug.value("incompleteFrames", 0) + "。"
}
