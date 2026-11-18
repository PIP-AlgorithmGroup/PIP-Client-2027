import QtQuick
import QtQuick.Controls

// 数字地址/端口输入，焦点、选择、禁用和校验反馈不依赖操作系统 Controls 外观。
TextField {
    id: control
    // 使用应用独立 UI 档位，尺寸为逻辑像素，系统 DPI 由 Qt 处理。
    property real uiScale: 1
    implicitHeight: 42 * uiScale
    leftPadding: 14 * uiScale
    rightPadding: leftPadding
    color: "#24272b"
    placeholderTextColor: "#737780"
    selectionColor: "#f2d5c7"
    selectedTextColor: "#24272b"
    font.pixelSize: 13 * uiScale
    selectByMouse: true
    verticalAlignment: TextInput.AlignVCenter
    inputMethodHints: Qt.ImhNoPredictiveText
    background: Rectangle {
        radius: 4 * control.uiScale
        color: "#ffffff"
        border.width: control.activeFocus ? 2 : 1
        border.color: control.activeFocus ? "#c54d25" : "#d9dde1"
    }
}
