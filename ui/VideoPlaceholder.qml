pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls

// 无有效帧时展示监看待机画幅；收到视频后由宿主整体隐藏，不覆盖实况。
Rectangle {
    id: placeholder
    // C++ 持有接收器；这里只读取 GUI 状态，不推断网络统计或输入视频尺寸。
    required property var receiver
    required property real uiScale
    required property string displayFont
    // 客户端级入口，图传配置位于菜单的独立分区。
    signal menuRequested
    readonly property bool hasError: receiver.error !== ""
    readonly property color accent: hasError ? "#e7ac76" : "#91c7d2"
    gradient: Gradient {
        GradientStop { position: 0; color: "#263b49" }
        GradientStop { position: 1; color: "#14212b" }
    }
    // 静态参考画幅仅表达待机监看器，不表示收到实况或连接进度。
    Item {
        id: monitor
        anchors.centerIn: parent
        width: Math.min(parent.width - 104 * placeholder.uiScale, (parent.height - 168 * placeholder.uiScale) * 16 / 9)
        height: width * 9 / 16
        opacity: 0.6
        Repeater {
            model: 9
            Rectangle {
                required property int index
                x: index * monitor.width / 8
                width: 1
                height: monitor.height
                color: "#344a58"
                opacity: index === 4 ? 0.75 : 0.35
            }
        }
        Repeater {
            model: 7
            Rectangle {
                required property int index
                y: index * monitor.height / 6
                height: 1
                width: monitor.width
                color: "#344a58"
                opacity: index === 3 ? 0.75 : 0.35
            }
        }
        Repeater {
            model: 4
            Item {
                id: corner
                required property int index
                x: index % 2 === 0 ? 0 : monitor.width
                y: index < 2 ? 0 : monitor.height
                Rectangle {
                    x: corner.index % 2 === 0 ? 0 : -22 * placeholder.uiScale
                    width: 22 * placeholder.uiScale
                    height: 1
                    color: "#9ab6c4"
                }
                Rectangle {
                    y: corner.index < 2 ? 0 : -22 * placeholder.uiScale
                    height: 22 * placeholder.uiScale
                    width: 1
                    color: "#9ab6c4"
                }
            }
        }
        Rectangle {
            anchors.centerIn: parent
            width: 250 * placeholder.uiScale
            height: width
            radius: width / 2
            color: "transparent"
            border.color: "#49616d"
            opacity: 0.35
        }
    }
    Row {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: 34 * placeholder.uiScale
        spacing: 18 * placeholder.uiScale
        Text {
            text: "PIP"
            font.family: placeholder.displayFont
            font.pixelSize: 24 * placeholder.uiScale
            color: "#d3e2e9"
        }
        Rectangle {
            width: 1
            height: 20 * placeholder.uiScale
            anchors.verticalCenter: parent.verticalCenter
            color: "#566e7c"
        }
        Column {
            spacing: 2 * placeholder.uiScale
            anchors.verticalCenter: parent.verticalCenter
            Text {
                text: "RMUL"
                font.family: placeholder.displayFont
                font.pixelSize: 13 * placeholder.uiScale
                font.letterSpacing: 2
                color: "#b9cbd5"
            }
            Text {
                text: "VIDEO RECEIVER"
                font.pixelSize: 9 * placeholder.uiScale
                font.letterSpacing: 1.5
                color: "#7893a2"
            }
        }
    }
    Row {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 38 * placeholder.uiScale
        spacing: 9 * placeholder.uiScale
        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            width: 5 * placeholder.uiScale
            height: width
            radius: width / 2
            color: placeholder.accent
        }
        Text {
            text: "STANDBY"
            color: "#9eb7c4"
            font.pixelSize: 11 * placeholder.uiScale
            font.letterSpacing: 2
        }
    }
    Column {
        anchors.centerIn: parent
        anchors.verticalCenterOffset: -8 * placeholder.uiScale
        width: parent.width - 80 * placeholder.uiScale
        spacing: 14 * placeholder.uiScale
        Text {
            objectName: "videoEmptyHeadline"
            anchors.horizontalCenter: parent.horizontalCenter
            text: placeholder.hasError ? "INPUT ERROR" : !placeholder.receiver.running ? "VIDEO PAUSED" : "NO SIGNAL"
            font.family: placeholder.displayFont
            font.pixelSize: 88 * placeholder.uiScale
            font.letterSpacing: 3 * placeholder.uiScale
            color: "#e1ebef"
        }
        Row {
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 14 * placeholder.uiScale
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: 28 * placeholder.uiScale
                height: 1
                color: placeholder.accent
            }
            Text {
                objectName: "videoEmptyTitle"
                text: placeholder.hasError ? "图传暂不可用" : !placeholder.receiver.running ? "图传已暂停" : placeholder.receiver.status === "等待关键帧" ? "等待关键帧" : placeholder.receiver.status === "图传中断，等待画面" ? "图传信号中断" : "等待图传画面"
                color: placeholder.accent
                font.pixelSize: 18 * placeholder.uiScale
                font.letterSpacing: 2 * placeholder.uiScale
            }
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: 28 * placeholder.uiScale
                height: 1
                color: placeholder.accent
            }
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: placeholder.hasError ? "在客户端菜单中查看接收诊断与图传配置" : !placeholder.receiver.running ? "可在客户端菜单的图传设置中恢复接收" : "画面接入后自动显示"
            color: "#8da5b3"
            font.pixelSize: 13 * placeholder.uiScale
        }
    }
    Column {
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.margins: 34 * placeholder.uiScale
        spacing: 7 * placeholder.uiScale
        Text {
            text: "UDP ENDPOINT"
            color: "#718d9d"
            font.pixelSize: 9 * placeholder.uiScale
            font.letterSpacing: 1.5
        }
        Text {
            text: "UDP   " + placeholder.receiver.bindAddress + " : " + placeholder.receiver.port
            color: "#b3c7d2"
            font.pixelSize: 13 * placeholder.uiScale
        }
    }
    Button {
        id: menuHint
        objectName: "videoEmptySettingsButton"
        anchors.bottom: parent.bottom
        anchors.right: parent.right
        anchors.margins: 28 * placeholder.uiScale
        width: 190 * placeholder.uiScale
        height: 44 * placeholder.uiScale
        text: "客户端菜单"
        Accessible.name: "打开客户端菜单，快捷键 Esc"
        hoverEnabled: true
        onClicked: placeholder.menuRequested()
        background: Rectangle {
            radius: 6 * placeholder.uiScale
            color: menuHint.hovered ? "#344b59" : "transparent"
            border.color: menuHint.activeFocus ? "#91c7d2" : "transparent"
        }
        contentItem: Row {
            spacing: 12 * placeholder.uiScale
            Rectangle {
                width: 38 * placeholder.uiScale
                height: 25 * placeholder.uiScale
                radius: 4 * placeholder.uiScale
                color: "#2b414f"
                border.color: "#536d7c"
                Text {
                    anchors.centerIn: parent
                    text: "ESC"
                    font.pixelSize: 10 * placeholder.uiScale
                    color: "#c1d2dc"
                }
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: menuHint.text
                color: "#b3c7d2"
                font.pixelSize: 13 * placeholder.uiScale
            }
        }
    }
}
