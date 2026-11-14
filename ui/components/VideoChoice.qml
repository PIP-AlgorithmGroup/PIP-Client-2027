pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls

// Qt 管理键盘和弹层；单向箭头保持双平台外观一致。
ComboBox {
    id: choice
    // UI 档位倍率；模型及选中值由调用方管理。
    property real uiScale: 1
    // 未选择时的提示，不自动提交首项。
    property string placeholderText: "请选择"
    implicitHeight: 40 * uiScale
    font.pixelSize: 14 * uiScale
    textRole: "label"
    currentIndex: -1
    contentItem: Text {
        leftPadding: 12 * choice.uiScale
        rightPadding: 32 * choice.uiScale
        text: choice.currentIndex < 0 ? choice.placeholderText : choice.displayText
        color: !choice.enabled || choice.currentIndex < 0 ? "#62666e" : "#24272b"
        font: choice.font
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
    indicator: Image {
        x: choice.width - width - 12 * choice.uiScale
        y: (choice.height - height) / 2
        width: 16 * choice.uiScale
        height: width
        source: "qrc:/resources/icons/chevron_down.svg"
        sourceSize: Qt.size(Math.ceil(width * Screen.devicePixelRatio), Math.ceil(height * Screen.devicePixelRatio))
        opacity: choice.enabled ? 1 : 0.4
    }
    background: Rectangle {
        radius: 3 * choice.uiScale
        color: choice.enabled ? "#ffffff" : "#edeef0"
        border.width: choice.activeFocus ? 2 : 1
        border.color: choice.activeFocus ? "#c94b17" : "#c9cdd2"
    }
    delegate: ItemDelegate {
        id: option
        required property int index
        required property var modelData
        width: choice.width
        height: Math.max(40 * choice.uiScale, contentItem.implicitHeight + 16 * choice.uiScale)
        highlighted: choice.highlightedIndex === index
        enabled: option.modelData.available !== false
        contentItem: Column {
            spacing: 3 * choice.uiScale
            Text {
                width: parent.width
                text: option.modelData[choice.textRole]
                font: choice.font
                color: option.enabled ? "#24272b" : "#969aa1"
                elide: Text.ElideRight
            }
            // 禁选项也直接展示原因，用户无需先选中故障设备才能读到诊断。
            Text {
                width: parent.width
                visible: !option.enabled
                text: option.modelData.reason || "设备不可用"
                font.pixelSize: 11 * choice.uiScale
                color: "#737780"
                wrapMode: Text.WrapAnywhere
                maximumLineCount: 2
                elide: Text.ElideRight
            }
        }
        background: Rectangle {
            color: option.highlighted ? "#f5e7df" : "#fafafa"
        }
    }
}
