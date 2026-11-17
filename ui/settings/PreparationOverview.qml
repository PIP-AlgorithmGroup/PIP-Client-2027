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
    readonly property int completeCount: checks.filter(check => check.passed).length
    // 宿主负责导航，页面不持有任何设备连接。
    signal sectionRequested(string section)
    clip: true
    contentWidth: availableWidth
    ColumnLayout {
        width: overview.availableWidth
        spacing: 0
        ColumnLayout {
            Layout.fillWidth: true
            Layout.bottomMargin: 30 * overview.uiScale
            spacing: 12 * overview.uiScale
            Rectangle {
                Layout.alignment: Qt.AlignHCenter
                implicitWidth: 58 * overview.uiScale
                implicitHeight: implicitWidth
                radius: width / 2
                color: overview.completeCount === 4 ? "#e3efe7" : "#e9ecee"
                Text {
                    anchors.centerIn: parent
                    text: overview.completeCount === 4 ? "✓" : "!"
                    color: overview.completeCount === 4 ? "#32745a" : "#8c949c"
                    font.pixelSize: 26 * overview.uiScale
                }
            }
            Text {
                Layout.fillWidth: true
                text: overview.completeCount === 4 ? "准备好了，可以进入画面" : "还有检查项需要确认"
                horizontalAlignment: Text.AlignHCenter
                color: "#23272c"
                font.pixelSize: 27 * overview.uiScale
                font.weight: Font.DemiBold
                wrapMode: Text.WordWrap
            }
            Text {
                Layout.fillWidth: true
                text: "已完成 " + overview.completeCount + " / 4 项，可返回对应页面处理。"
                horizontalAlignment: Text.AlignHCenter
                color: "#9299a1"
                font.pixelSize: 12 * overview.uiScale
                wrapMode: Text.WordWrap
            }
        }
        Repeater {
            model: overview.checks
            Rectangle {
                id: card
                required property var modelData
                required property int index
                Layout.fillWidth: true
                implicitHeight: cardContents.implicitHeight + 40 * overview.uiScale
                color: "white"
                border.color: "#e3e5e7"
                RowLayout {
                    id: cardContents
                    anchors.fill: parent
                    anchors.margins: 20 * overview.uiScale
                    spacing: 16 * overview.uiScale
                    Text {
                        text: card.modelData.passed ? "✓" : "0" + (card.index + 1)
                        font.pixelSize: 16 * overview.uiScale
                        color: card.modelData.passed ? "#32745a" : "#8b959f"
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 6 * overview.uiScale
                        Text {
                            text: card.modelData.title
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            color: card.modelData.failed ? "#ad4325" : "#24272b"
                            font.pixelSize: 13 * overview.uiScale
                            font.weight: Font.DemiBold
                        }
                        Text {
                            text: card.modelData.detail
                            Layout.fillWidth: true
                            wrapMode: Text.WrapAnywhere
                            color: "#737780"
                            font.pixelSize: 11 * overview.uiScale
                        }
                    }
                    Text {
                        text: card.modelData.status
                        color: card.modelData.passed ? "#32745a" : card.modelData.failed ? "#b34337" : "#969da6"
                        font.pixelSize: 10 * overview.uiScale
                    }
                    VideoButton {
                        objectName: "overviewOpen_" + card.modelData.key
                        uiScale: overview.uiScale
                        text: "查看"
                        quiet: true
                        implicitWidth: 54 * overview.uiScale
                        enabled: card.modelData.available
                        onClicked: overview.sectionRequested(card.modelData.key)
                    }
                }
            }
        }
        Text {
            Layout.fillWidth: true
            Layout.topMargin: 20 * overview.uiScale
            text: "检查仅对当前配置有效。连接中断或配置变化后，需要重新确认对应项目。\n未完成检查也可返回比赛画面。"
            horizontalAlignment: Text.AlignHCenter
            color: "#9aa1a9"
            wrapMode: Text.WordWrap
            font.pixelSize: 11 * overview.uiScale
        }
    }
}
