pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import "../components"

// 欢迎页只展示本地应用状态和通用连接状态，不解析协议、不自动连接。
Item {
    id: page

    // C++ AppController 注入，属性与方法契约见 app_controller.h。
    required property var controller
    required property string displayFont
    required property string mediumFont
    required property string connectionLabel
    required property bool backgroundFailed
    signal panelRequested(string panel)

    required property real uiScale
    readonly property real unit: 0.8 * uiScale
    readonly property real leftMargin: 64 * uiScale
    readonly property bool isReady: controller.startupState === "ready"

    // 按实际可见字形而非字体行框定位，避免跨平台字体上沿留白造成两行重叠。
    TextMetrics {
        id: titleMetrics
        font: titleText.font
        text: "PIP"
    }

    // 工具面板关闭或返回后恢复到首个模式，避免焦点留在不可见控件。
    function focusModes() {
        league.forceActiveFocus(Qt.TabFocusReason);
    }

    Text {
        x: page.leftMargin
        y: 48 * page.unit
        text: "PIP / CLIENT"
        font.family: page.displayFont
        font.pixelSize: 18 * page.uiScale
        font.weight: Font.DemiBold
        color: "white"
    }
    Rectangle {
        anchors.right: parent.right
        anchors.rightMargin: page.leftMargin
        y: 37 * page.unit
        width: 172 * page.unit
        height: 32 * page.uiScale
        radius: height / 2
        color: "#b0304b60"
        Text {
            anchors.centerIn: parent
            text: "SEASON 2027"
            color: "white"
            font.family: page.displayFont
            font.pixelSize: 13 * page.uiScale
        }
    }
    // 品牌区在左上方可用空间自适应定位；只允许品牌区滚动，底部赛事操作始终可见。
    Flickable {
        id: menuViewport
        x: page.leftMargin
        y: Math.max(80 * page.uiScale, Math.min(page.height * 0.22, actions.y - 8 * page.uiScale - contentHeight))
        width: 390 * page.uiScale
        height: Math.max(0, actions.y - 8 * page.uiScale - y)
        contentWidth: width
        contentHeight: menu.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        flickableDirection: Flickable.VerticalFlick
        ScrollBar.vertical: ScrollBar {
            policy: ScrollBar.AsNeeded
        }
        Column {
            id: menu
            width: 370 * page.uiScale
            Column {
                spacing: 12 * page.uiScale
                Text {
                    text: "ROBOMASTER"
                    color: "#c4f4ff"
                    font.family: page.displayFont
                    font.pixelSize: 14 * page.uiScale
                    font.letterSpacing: page.uiScale
                }
                Item {
                    objectName: "titleBlock"
                    readonly property rect glyphBounds: titleMetrics.tightBoundingRect
                    width: glyphBounds.width + 4
                    height: glyphBounds.height
                    Text {
                        x: titleText.x + 1
                        y: titleText.y + 3
                        text: titleText.text
                        font: titleText.font
                        color: "#90102030"
                    }
                    Text {
                        id: titleText
                        objectName: "titleText"
                        x: -parent.glyphBounds.x
                        y: -baselineOffset - parent.glyphBounds.y
                        text: "PIP"
                        color: "#f6f8f5"
                        font.family: page.displayFont
                        font.pixelSize: 138 * page.uiScale
                        font.weight: Font.Bold
                        font.letterSpacing: 3.2 * page.uiScale
                    }
                }
                Text {
                    text: "CLIENT"
                    color: "#ffe7b5"
                    font.family: page.mediumFont
                    font.pixelSize: 30 * page.uiScale
                    font.weight: Font.Medium
                    font.letterSpacing: 19 * page.uiScale
                }
            }
            Item {
                height: 12 * page.uiScale
                width: 1
            }
            Rectangle {
                width: 44 * page.uiScale
                height: 2
                color: "#ffda80"
            }
            Item {
                height: 12 * page.uiScale
                width: 1
            }
            Text {
                objectName: "welcomeSubtitle"
                text: "机甲大师自定义终端"
                color: "white"
                font.pixelSize: 20 * page.uiScale
                font.weight: Font.Medium
            }
            Item {
                height: 4 * page.uiScale
                width: 1
            }
            Text {
                text: "选择赛事，准备出发。"
                color: "#e3f1fa"
                font.pixelSize: 14 * page.uiScale
            }
        }
    }

    // 赛事操作固定左下；窗口扩大只增加上方留白，档位只控制组内尺寸。
    Column {
        id: actions
        objectName: "welcomeActions"
        anchors.left: parent.left
        anchors.leftMargin: page.leftMargin
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 64 * page.uiScale
        width: 370 * page.uiScale
        spacing: 12 * page.uiScale
        Column {
            width: parent.width
            spacing: 12 * page.uiScale
            visible: page.isReady
            Text {
                text: "赛事模式"
                font.pixelSize: 14 * page.uiScale
                color: "#daeaf5"
            }
            Row {
                spacing: 18 * page.uiScale
                width: parent.width

                Button {
                    id: league
                    objectName: "leagueMode"
                    width: (parent.width - parent.spacing) / 2
                    height: 72 * page.uiScale
                    hoverEnabled: true
                    focusPolicy: Qt.StrongFocus
                    enabled: !page.controller.isEntering
                    checked: page.controller.selectedMode === "RMUL"
                    Accessible.name: "RMUL 机甲大师高校联盟赛"
                    Accessible.role: Accessible.RadioButton
                    Accessible.checkable: true
                    Accessible.checked: checked
                    onClicked: page.controller.selectMode("RMUL")
                    Keys.onReturnPressed: clicked()
                    Keys.onEnterPressed: clicked()
                    Keys.onRightPressed: {
                        championship.forceActiveFocus();
                        page.controller.selectMode("RMUC");
                    }
                    background: Rectangle {
                        color: league.hovered || league.activeFocus ? "#400b2438" : "transparent"
                        radius: 4
                        // 键盘焦点用短侧线，不重新引入用户否定的大矩形边框。
                        Rectangle {
                            x: -6
                            anchors.verticalCenter: parent.verticalCenter
                            width: 2
                            height: parent.height * 0.65
                            color: "#ffda80"
                            visible: league.activeFocus
                        }
                    }
                    contentItem: Column {
                        spacing: 5 * page.uiScale
                        Text {
                            text: "RMUL"
                            font.family: page.displayFont
                            font.pixelSize: 26 * page.uiScale
                            color: league.checked ? "#fff1c6" : "white"
                        }
                        Text {
                            text: "高校联盟赛"
                            font.pixelSize: 14 * page.uiScale
                            color: league.checked ? "#fff1c6" : "#e2edf7"
                        }
                    }
                    Rectangle {
                        anchors.bottom: parent.bottom
                        width: parent.width * 0.8
                        height: 2
                        color: "#ffda80"
                        visible: league.checked
                        Rectangle {
                            width: 6
                            height: 6
                            y: -2
                            rotation: 45
                            color: "#ffda80"
                        }
                    }
                }
                Button {
                    id: championship
                    objectName: "championshipMode"
                    width: league.width
                    height: league.height
                    hoverEnabled: true
                    focusPolicy: Qt.StrongFocus
                    enabled: league.enabled
                    checked: page.controller.selectedMode === "RMUC"
                    Accessible.name: "RMUC 超级对抗赛"
                    Accessible.role: Accessible.RadioButton
                    Accessible.checkable: true
                    Accessible.checked: checked
                    onClicked: page.controller.selectMode("RMUC")
                    Keys.onReturnPressed: clicked()
                    Keys.onEnterPressed: clicked()
                    Keys.onLeftPressed: {
                        league.forceActiveFocus();
                        page.controller.selectMode("RMUL");
                    }
                    background: Rectangle {
                        color: championship.hovered || championship.activeFocus ? "#400b2438" : "transparent"
                        radius: 4
                        Rectangle {
                            x: -6
                            anchors.verticalCenter: parent.verticalCenter
                            width: 2
                            height: parent.height * 0.65
                            color: "#ffda80"
                            visible: championship.activeFocus
                        }
                    }
                    contentItem: Column {
                        spacing: 5 * page.uiScale
                        Text {
                            text: "RMUC"
                            font.family: page.displayFont
                            font.pixelSize: 26 * page.uiScale
                            color: championship.checked ? "#fff1c6" : "white"
                        }
                        Text {
                            text: "超级对抗赛"
                            font.pixelSize: 14 * page.uiScale
                            color: championship.checked ? "#fff1c6" : "#e2edf7"
                        }
                    }
                    Rectangle {
                        anchors.bottom: parent.bottom
                        width: parent.width * 0.8
                        height: 2
                        color: "#ffda80"
                        visible: championship.checked
                        Rectangle {
                            width: 6
                            height: 6
                            y: -2
                            rotation: 45
                            color: "#ffda80"
                        }
                    }
                }
            }
            BasicButton {
                uiScale: page.uiScale
                objectName: "enterArenaButton"
                width: parent.width
                height: 56 * page.uiScale
                primary: true
                font.pixelSize: 20 * page.uiScale
                font.bold: true
                text: page.controller.isEntering ? "正在进入…" : page.controller.selectedMode === "" ? "请选择赛事" : "进入赛场    →"
                enabled: page.controller.selectedMode !== "" && !page.controller.isEntering
                onClicked: page.controller.enterArena()
            }
        }
        Column {
            width: parent.width
            spacing: 10 * page.uiScale
            visible: !page.isReady
            Text {
                width: parent.width
                wrapMode: Text.WordWrap
                color: "white"
                font.pixelSize: 16 * page.uiScale
                text: page.controller.startupState === "loading" ? "正在读取本地设置…" : page.controller.lastError
            }
            Row {
                spacing: 12 * page.uiScale
                visible: page.controller.startupState === "error"
                BasicButton {
                    uiScale: page.uiScale
                    text: "重试"
                    onClicked: page.controller.initialize()
                }
                BasicButton {
                    uiScale: page.uiScale
                    text: "使用默认设置"
                    primary: true
                    onClicked: page.controller.resetPreferences()
                }
            }
        }
        Text {
            width: parent.width
            visible: page.isReady && page.controller.lastError !== ""
            text: page.controller.lastError
            wrapMode: Text.WordWrap
            color: "#ffe2a6"
            font.pixelSize: 14 * page.uiScale
        }
    }

    Column {
        objectName: "welcomeTools"
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 64 * page.uiScale
        anchors.right: parent.right
        anchors.rightMargin: 48 * page.uiScale
        spacing: 14 * page.uiScale
        Repeater {
            model: ["设置", "诊断", "帮助", "关于"]
            BasicButton {
                required property string modelData
                objectName: "tool" + modelData
                uiScale: page.uiScale
                quiet: true
                width: height
                height: Math.max(44, 48 * page.uiScale)
                text: modelData
                iconSource: "qrc:/resources/icons/" + ({
                        "设置": "settings-2",
                        "诊断": "activity",
                        "帮助": "help",
                        "关于": "info"
                    })[modelData] + ".svg"
                font.pixelSize: 15 * page.uiScale
                onClicked: page.panelRequested(modelData)
            }
        }
    }
    Text {
        x: page.leftMargin
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 24 * page.unit
        text: "v" + page.controller.version + "     ·     " + page.connectionLabel
        color: "#e3edf5"
        font.pixelSize: 14 * page.uiScale
    }
    Text {
        anchors.right: parent.right
        anchors.rightMargin: 48 * page.uiScale
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 24 * page.unit
        text: page.backgroundFailed ? "背景不可用 · 已使用备用画面" : "背景：RoboMaster 2026 官方主视觉"
        color: "#e3edf5"
        font.pixelSize: 12 * page.uiScale
    }
}
