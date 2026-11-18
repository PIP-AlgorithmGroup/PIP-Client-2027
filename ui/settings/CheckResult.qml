pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts

// 只展示宿主提供的实时检查快照，不启动设备、不缓存或推断历史通过结果。
Rectangle {
    id: result
    // check 包含 title/status/detail/items，items 的 passed 仅表示对应本地条件。
    required property var check
    required property real uiScale
    default property alias actionData: actions.data
    implicitHeight: contents.implicitHeight + 44 * uiScale
    color: "#f6f7f7"
    border.color: "#e7e9ea"
    radius: 6 * uiScale
    data: Item {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 22 * result.uiScale
        ColumnLayout {
            id: contents
            width: parent.width
            spacing: 17 * result.uiScale
            Text {
                text: result.check.key === "input" ? "检查清单 / LOCAL TEST" : "检查结果"
                color: "#969ca4"
                font.pixelSize: 10 * result.uiScale
                font.letterSpacing: 1
            }
            Rectangle {
                implicitWidth: 43 * result.uiScale
                implicitHeight: implicitWidth
                radius: width / 2
                color: result.check.passed ? "#e3efe7" : result.check.failed ? "#f7e9e5" : "#e9ecee"
                Text {
                    anchors.centerIn: parent
                    text: result.check.passed ? "✓" : result.check.failed ? "!" : "○"
                    color: result.check.passed ? "#32745a" : result.check.failed ? "#b34337" : "#8c949c"
                    font.pixelSize: 23 * result.uiScale
                }
            }
            Text {
                Layout.fillWidth: true
                text: result.check.heading || result.check.status
                color: "#23272c"
                font.pixelSize: 17 * result.uiScale
                font.weight: Font.DemiBold
                wrapMode: Text.WordWrap
            }
            Text {
                Layout.fillWidth: true
                text: result.check.detail
                color: "#858b94"
                font.pixelSize: 11 * result.uiScale
                wrapMode: Text.WrapAnywhere
                lineHeight: 1.5
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
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 3 * result.uiScale
                        Text {
                            Layout.fillWidth: true
                            text: condition.modelData.label
                            color: "#7c838c"
                            font.pixelSize: 12 * result.uiScale
                            wrapMode: Text.WordWrap
                        }
                        Text {
                            Layout.fillWidth: true
                            text: condition.modelData.detail || ""
                            visible: text !== ""
                            color: "#969ca4"
                            font.pixelSize: 10 * result.uiScale
                            wrapMode: Text.WrapAnywhere
                        }
                    }
                }
            }
            ColumnLayout {
                id: actions
                Layout.fillWidth: true
                spacing: 8 * result.uiScale
            }
            Text {
                Layout.fillWidth: true
                text: result.check.key === "input" ? "仅检查本地原生响应，不发送控制指令" : "结果只对应已应用配置，各项可独立检查"
                color: "#8a8e95"
                font.pixelSize: 10 * result.uiScale
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
            }
        }
    }
}
