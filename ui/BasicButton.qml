import QtQuick
import QtQuick.Controls

// 通用键盘可达按钮，外观不依赖系统 Controls 风格；无业务状态或网络副作用。
Button {
    id: control

    // 金色用于主动作；quiet 用于低优先级边缘入口。
    property bool primary: false
    property bool quiet: false

    implicitWidth: 120
    implicitHeight: 48
    padding: 12
    focusPolicy: Qt.StrongFocus
    hoverEnabled: true
    Accessible.name: text
    Keys.onReturnPressed: if (enabled)
        clicked()
    Keys.onEnterPressed: if (enabled)
        clicked()

    contentItem: Text {
        text: control.text
        color: !control.enabled ? "#9aabb7" : control.primary ? "#183a4e" : "#f4f8fc"
        font: control.font
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
    background: Rectangle {
        radius: control.quiet ? height / 2 : 4
        color: !control.enabled ? "#41566a" : control.primary ? (control.down ? "#e8bc58" : control.hovered ? "#ffe7a6" : "#ffda80") : (control.down ? "#304e65" : control.hovered ? "#34566b" : "#dc102c40")
        border.color: control.activeFocus ? "#ffdf93" : control.primary ? "#fff0bf" : "#69879c"
        border.width: control.activeFocus ? 2 : 1
    }
}
