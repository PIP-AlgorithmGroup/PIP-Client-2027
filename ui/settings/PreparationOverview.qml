pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"

// 概览与配置共用检查快照；不以完成数量限制导航或返回比赛画面。
ScrollView {
    id: overview
    required property var checks
    required property real uiScale
    // 宿主负责导航，页面不持有任何设备连接。
    signal sectionRequested(string section)
    clip: true
    contentWidth: availableWidth
    ColumnLayout {
        width: overview.availableWidth
        spacing: 18 * overview.uiScale
        Text {
            text: "MATCH PREPARATION"
            color: "#c94b17"
            font.pixelSize: 11 * overview.uiScale
            font.letterSpacing: 2
        }
        Text {
            text: "准备概览"
            color: "#24272b"
            font.pixelSize: 26 * overview.uiScale
            font.weight: Font.DemiBold
        }
        Text {
            Layout.fillWidth: true
            text: "建议先接入 MQTT，再检查图传、控制器与键鼠。可以从任意一项开始，遇到问题也可以稍后回来处理。"
            wrapMode: Text.WordWrap
            color: "#737780"
            font.pixelSize: 13 * overview.uiScale
        }
        Repeater {
            model: overview.checks
            Rectangle {
                id: card
                required property var modelData
                required property int index
                Layout.fillWidth: true
                implicitHeight: cardContents.implicitHeight + 32 * overview.uiScale
                radius: 5 * overview.uiScale
                color: "#f7f8f8"
                border.color: "#e4e6e7"
                RowLayout {
                    id: cardContents
                    anchors.fill: parent
                    anchors.margins: 16 * overview.uiScale
                    spacing: 16 * overview.uiScale
                    Text {
                        text: "0" + (card.index + 1)
                        font.pixelSize: 18 * overview.uiScale
                        color: "#c94b17"
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 6 * overview.uiScale
                        Text {
                            text: card.modelData.title + "  ·  " + card.modelData.status
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            color: card.modelData.failed ? "#ad4325" : "#24272b"
                            font.pixelSize: 16 * overview.uiScale
                            font.weight: Font.DemiBold
                        }
                        Text {
                            text: card.modelData.detail
                            Layout.fillWidth: true
                            wrapMode: Text.WrapAnywhere
                            color: "#737780"
                            font.pixelSize: 12 * overview.uiScale
                        }
                    }
                    VideoButton {
                        objectName: "overviewOpen_" + card.modelData.key
                        uiScale: overview.uiScale
                        text: "打开设置"
                        enabled: card.modelData.available
                        onClicked: overview.sectionRequested(card.modelData.key)
                    }
                }
            }
        }
        Text {
            Layout.fillWidth: true
            text: "状态随当前连接更新；键鼠结果由本地测试后确认。未完成检查也可返回画面。"
            color: "#737780"
            wrapMode: Text.WordWrap
            font.pixelSize: 12 * overview.uiScale
        }
    }
}
