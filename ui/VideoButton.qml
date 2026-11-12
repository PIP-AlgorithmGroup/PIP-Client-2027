import QtQuick
import QtQuick.Controls

// 客户端菜单的动作样式；尺寸只受 UI 档位影响，保留 Button 原生键盘和可访问语义。
Button {
    id: control
    // 独立 UI 档位；不读取父窗口缩放比例。
    property real uiScale: 1
    // 主动作使用实色；quiet 是返回/关闭等低优先级入口。
    property bool primary: false
    property bool quiet: false
    // 有图标时 text 仅作可访问名称和悬停提示，避免依赖 Unicode 图标字形。
    property url iconSource
    implicitWidth: Math.max(108 * uiScale, label.implicitWidth + 40 * uiScale)
    implicitHeight: 40 * uiScale
    font.pixelSize: 13 * uiScale
    font.weight: Font.Medium
    padding: 12 * uiScale
    focusPolicy: Qt.StrongFocus
    hoverEnabled: true
    Accessible.name: text
    Keys.onReturnPressed: if (enabled)
        clicked()
    Keys.onEnterPressed: if (enabled)
        clicked()
    ToolTip.visible: iconSource.toString() !== "" && (hovered || activeFocus)
    ToolTip.text: text
    ToolTip.delay: 400
    contentItem: Item {
        Text {
            id: label
            anchors.fill: parent
            visible: control.iconSource.toString() === ""
            text: control.text
            font: control.font
            color: !control.enabled ? "#8a8e95" : control.primary ? "white" : "#24272b"
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        Image {
            anchors.centerIn: parent
            width: 20 * control.uiScale
            height: width
            source: control.iconSource
            sourceSize: Qt.size(Math.ceil(width * Screen.devicePixelRatio), Math.ceil(height * Screen.devicePixelRatio))
            visible: control.iconSource.toString() !== ""
        }
    }
    background: Rectangle {
        radius: 3 * control.uiScale
        color: !control.enabled ? "#e4e5e7" : control.primary ? (control.down ? "#aa3e12" : control.hovered ? "#d85b25" : "#c94b17") : control.down ? "#dedfe2" : control.hovered ? "#edeef0" : control.quiet ? "transparent" : "#ffffff"
        border.width: control.activeFocus ? 2 : 1
        border.color: control.activeFocus ? "#c94b17" : control.primary || control.quiet ? "transparent" : "#c9cdd2"
    }
}
