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
    function value(key, digits) {
        const number = metrics[key];
        return typeof number === "number" && isFinite(number) && number >= 0 ? number.toFixed(digits) : "—";
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
        text: "RX " + debug.value("receiveFps", 1) + " | DEC " + debug.value("decodeFps", 1) + " | OUT " + (debug.view.presentationFps >= 0 ? debug.view.presentationFps.toFixed(1) : "—") + " | 5/1%L " + debug.value("low5Fps", 1) + "/" + debug.value("low1Fps", 1) + " | GAP1s " + debug.value("frameLossPercent", 1) + "% | UDP —" + " | " + debug.value("megabitsPerSecond", 2) + " Mb/s" + " | " + debug.value("decodeMs", 1) + " ms CPU"
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
    ToolTip.text: "RX：最近 1 秒应用完整收帧；DEC：有效解码输出；OUT：含新视频纹理的 Qt 呈交。\n" + "5/1%L：最近 10 秒最慢 5%/1% 收帧间隔的平均值换算，至少 20/100 个间隔。\n" + "GAP1s：最近 1 秒检测到的整帧序号缺口率，含发送跳帧和重组失败，不等于 UDP 丢包率。\n" + "UDP：协议无分片总数且允许变长分片，无法准确测量包损。\n" + "ms CPU：最近 1 秒平均软件解码调用耗时，包含色彩转换；不是端到端延迟。\n" + "RX 与解码共用接收线程，会受到解码阻塞和读包批次影响，不代表网卡硬件到包时刻。\n" + "最近 1 秒：非法包 " + debug.value("invalidPackets", 0) + "，重复分片 " + debug.value("duplicatePackets", 0) + "，未完成帧 " + debug.value("incompleteFrames", 0) + "。"
}
