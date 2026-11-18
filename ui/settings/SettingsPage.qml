pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 配置与检查同处一个滚动文档；窄窗将检查卡排到表单下方，不能直接隐藏检查入口。
Item {
    id: page
    // GUI 线程提供档位、页头与只读检查快照；组件不持有或控制后端资源。
    required property real uiScale
    required property string title
    required property string eyebrow
    required property string description
    required property var check
    property string formObjectName: ""
    // 前置内容（例如完整键盘）横跨表单与检查卡，保持原型的阅读顺序。
    property alias leadingData: leading.data
    default property alias formData: form.data
    property alias actionData: result.actionData
    property alias contentItem: scroll.contentItem
    // 新比赛会话初始化时回到文档顶部；分类切换或重开菜单保留滚动位置。
    function resetScroll() {
        const flickable = scroll.contentItem as Flickable;
        if (flickable)
            flickable.contentY = 0;
    }
    data: ScrollView {
        id: scroll
        objectName: page.formObjectName
        anchors.fill: parent
        clip: true
        contentWidth: availableWidth
        ColumnLayout {
            width: scroll.availableWidth
            spacing: 0
            ColumnLayout {
                Layout.fillWidth: true
                Layout.margins: 34 * page.uiScale
                Layout.leftMargin: (page.width >= 800 * page.uiScale ? 40 : 26) * page.uiScale
                Layout.rightMargin: (page.width >= 800 * page.uiScale ? 40 : 26) * page.uiScale
                spacing: 22 * page.uiScale
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12 * page.uiScale
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 9 * page.uiScale
                        Text {
                            text: page.eyebrow
                            color: "#c54d25"
                            font.pixelSize: 10 * page.uiScale
                            font.letterSpacing: 2
                        }
                        Text {
                            Layout.fillWidth: true
                            text: page.title
                            color: "#23272c"
                            font.pixelSize: 29 * page.uiScale
                            font.weight: Font.DemiBold
                            wrapMode: Text.WordWrap
                        }
                        Text {
                            Layout.fillWidth: true
                            text: page.description
                            color: "#7a8088"
                            font.pixelSize: 12 * page.uiScale
                            wrapMode: Text.WordWrap
                            lineHeight: 1.5
                        }
                    }
                    Rectangle {
                        Layout.alignment: Qt.AlignTop
                        visible: page.width > 780 * page.uiScale
                        implicitWidth: tag.implicitWidth + 22 * page.uiScale
                        implicitHeight: 29 * page.uiScale
                        color: "#f4f5f5"
                        border.color: "#e8eaeb"
                        radius: 3 * page.uiScale
                        Text {
                            id: tag
                            anchors.centerIn: parent
                            text: "随时切换项目"
                            color: "#899099"
                            font.pixelSize: 11 * page.uiScale
                        }
                    }
                }
                ColumnLayout {
                    id: leading
                    Layout.fillWidth: true
                    spacing: 16 * page.uiScale
                    visible: children.length > 0
                }
                GridLayout {
                    Layout.fillWidth: true
                    columns: page.width >= 800 * page.uiScale ? 2 : 1
                    columnSpacing: 28 * page.uiScale
                    rowSpacing: 24 * page.uiScale
                    ColumnLayout {
                        id: form
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        Layout.alignment: Qt.AlignTop
                        spacing: 16 * page.uiScale
                    }
                    CheckResult {
                        id: result
                        objectName: "checkResult_" + page.formObjectName
                        Layout.fillWidth: true
                        Layout.preferredWidth: 280 * page.uiScale
                        Layout.maximumWidth: page.width >= 800 * page.uiScale ? 280 * page.uiScale : Infinity
                        Layout.alignment: Qt.AlignTop
                        uiScale: page.uiScale
                        check: page.check
                    }
                }
            }
        }
    }
}
