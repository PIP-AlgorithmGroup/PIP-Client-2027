import QtQuick
import QtQuick.Controls

// 通用键盘可达按钮，外观不依赖系统 Controls 风格；无业务状态或网络副作用。
Button {
    id: control

    // 金色用于主动作；quiet 用于低优先级边缘入口。
    property bool primary: false
    property bool quiet: false
    // 档位来自应用偏好，不随父窗口尺寸变化。
    property real uiScale: 1
    // 图标按钮保留 text 作为语义名称；SVG 随包，避免 Unicode 字形和系统图标差异。
    property url iconSource

    implicitWidth: 120 * uiScale
    implicitHeight: 48 * uiScale
    padding: 12 * uiScale
    font.pixelSize: 16 * uiScale
    focusPolicy: Qt.StrongFocus
    hoverEnabled: true
    Accessible.name: text
    ToolTip {
        visible: control.iconSource.toString() !== "" && (control.hovered || control.activeFocus)
        text: control.text
        delay: control.hovered ? 400 : 0
        font.pixelSize: 14 * control.uiScale
    }
    Keys.onReturnPressed: if (enabled)
        clicked()
    Keys.onEnterPressed: if (enabled)
        clicked()

    contentItem: Item {
        implicitWidth: label.implicitWidth
        implicitHeight: label.implicitHeight
        Text {
            id: label
            anchors.fill: parent
            visible: control.iconSource.toString() === ""
            text: control.text
            color: !control.enabled ? "#9aabb7" : control.primary ? "#183a4e" : "#f4f8fc"
            font: control.font
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        Image {
            objectName: "buttonIcon"
            readonly property int rasterPixels: Math.ceil(width * Screen.devicePixelRatio)
            anchors.centerIn: parent
            width: 24 * control.uiScale
            height: width
            // 一次绑定整个 QSize，避免宽高子属性互相触发图像尺寸绑定。
            sourceSize: Qt.size(rasterPixels, rasterPixels)
            source: control.iconSource
            fillMode: Image.PreserveAspectFit
            visible: control.iconSource.toString() !== ""
        }
    }
    background: Rectangle {
        radius: control.quiet ? height / 2 : 4
        color: !control.enabled ? "#41566a" : control.primary ? (control.down ? "#e8bc58" : control.hovered ? "#ffe7a6" : "#ffda80") : (control.down ? "#304e65" : control.hovered ? "#34566b" : "#dc102c40")
        border.color: control.activeFocus ? "#ffdf93" : control.primary ? "#fff0bf" : "#69879c"
        border.width: control.activeFocus ? 2 : 1
    }
}
