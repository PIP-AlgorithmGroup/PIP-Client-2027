pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 只展示宿主提供的实时检查快照，不启动设备、不缓存或推断历史通过结果。
Rectangle {
    id: result
    // check 包含 title/status/detail/items，items 的 passed 仅表示对应本地条件。
    required property var check
    required property real uiScale
    implicitHeight: contents.implicitHeight + 40 * uiScale
    color: "#f5f6f6"
    border.color: "#e4e6e7"
    radius: 5 * uiScale
    ScrollView {
        id: resultScroll
        anchors.fill: parent
        anchors.margins: 20 * result.uiScale
        contentWidth: availableWidth
        clip: true
        ColumnLayout {
            id: contents
            width: resultScroll.availableWidth
            spacing: 18 * result.uiScale
            Text {
                text: "检查结果 / LIVE"
                color: "#737780"
                font.pixelSize: 11 * result.uiScale
                font.letterSpacing: 1
            }
            Text {
                Layout.fillWidth: true
                text: result.check.status
                color: result.check.failed ? "#ad4325" : result.check.passed ? "#34825b" : "#24272b"
                font.pixelSize: 22 * result.uiScale
                font.weight: Font.DemiBold
                wrapMode: Text.WordWrap
            }
            Text {
                Layout.fillWidth: true
                text: result.check.detail
                color: "#62666e"
                font.pixelSize: 13 * result.uiScale
                wrapMode: Text.WrapAnywhere
                lineHeight: 1.5
            }
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: 1
                color: "#e0e3e5"
            }
            Repeater {
                model: result.check.items
                RowLayout {
                    id: condition
                    required property var modelData
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignTop
                    spacing: 10 * result.uiScale
                    Text {
                        Layout.alignment: Qt.AlignTop
                        text: condition.modelData.passed ? "✓" : "○"
                        color: condition.modelData.passed ? "#34825b" : "#8a8e95"
                        font.pixelSize: 15 * result.uiScale
                    }
                    Text {
                        Layout.fillWidth: true
                        text: condition.modelData.label
                        color: "#62666e"
                        font.pixelSize: 13 * result.uiScale
                        wrapMode: Text.WordWrap
                    }
                }
            }
            Text {
                Layout.fillWidth: true
                text: "检查可稍后处理，各项设置随时切换。"
                color: "#8a8e95"
                font.pixelSize: 11 * result.uiScale
                wrapMode: Text.WordWrap
            }
        }
    }
}
