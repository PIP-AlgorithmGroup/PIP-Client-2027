import QtQuick
import QtQuick.Controls

// 数字地址/端口输入，焦点、选择、禁用和校验反馈不依赖操作系统 Controls 外观。
TextField {
    id: control
    // 使用应用独立 UI 档位，尺寸为逻辑像素，系统 DPI 由 Qt 处理。
    property real uiScale: 1
    implicitHeight: 46 * uiScale
    leftPadding: 14 * uiScale
    rightPadding: leftPadding
    color: "#203c50"
    placeholderTextColor: "#8a9fac"
    selectionColor: "#c1e4ef"
    selectedTextColor: "#123f53"
    font.pixelSize: 16 * uiScale
    selectByMouse: true
    verticalAlignment: TextInput.AlignVCenter
    inputMethodHints: Qt.ImhNoPredictiveText
    background: Rectangle {
        radius: 7 * control.uiScale
        color: control.activeFocus ? "#ffffff" : "#f7fafc"
        border.width: control.activeFocus ? 2 : 1
        border.color: control.activeFocus ? "#268bad" : "#d6e2e9"
    }
}
