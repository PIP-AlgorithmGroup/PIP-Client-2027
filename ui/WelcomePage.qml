pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls

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

    readonly property real unit: Math.min(width / 1600, height / 900)
    readonly property real leftMargin: 84 * unit
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
        font.pixelSize: Math.max(18, 22 * page.unit)
        font.weight: Font.DemiBold
        color: "white"
    }
    Rectangle {
        anchors.right: parent.right
        anchors.rightMargin: 84 * page.unit
        y: 37 * page.unit
        width: 172 * page.unit
        height: Math.max(32, 40 * page.unit)
        radius: height / 2
        color: "#b0304b60"
        Text {
            anchors.centerIn: parent
            text: "SEASON 2027"
            color: "white"
            font.family: page.displayFont
            font.pixelSize: Math.max(12, 16 * page.unit)
        }
    }
    Column {
        x: page.leftMargin
        y: 198 * page.unit
        spacing: 15 * page.unit

        Text {
            text: "ROBOMASTER"
            color: "#c4f4ff"
            font.family: page.displayFont
            font.pixelSize: Math.max(14, 18 * page.unit)
            font.letterSpacing: 1
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
                font.pixelSize: 172 * page.unit
                font.weight: Font.Bold
                font.letterSpacing: 4 * page.unit
            }
        }
        Text {
            text: "CLIENT"
            color: "#ffe7b5"
            font.family: page.mediumFont
            font.pixelSize: 38 * page.unit
            font.weight: Font.Medium
            font.letterSpacing: 24 * page.unit
        }
    }
    Rectangle {
        x: page.leftMargin
        y: 452 * page.unit
        width: 54 * page.unit
        height: 2
        color: "#ffda80"
    }
    Text {
        x: page.leftMargin
        y: 474 * page.unit
        text: "机甲大师自定义终端"
        color: "white"
        font.pixelSize: Math.max(18, 25 * page.unit)
    }
    Text {
        x: page.leftMargin
        y: 521 * page.unit
        text: "选择赛事，准备出发。"
        color: "#e3f1fa"
        font.pixelSize: Math.max(14, 17 * page.unit)
    }

    Column {
        x: page.leftMargin
        y: 584 * page.unit
        width: Math.max(370, 462 * page.unit)
        spacing: 16 * page.unit
        visible: page.isReady

        Text {
            text: "赛事模式"
            font.pixelSize: 14
            color: "#daeaf5"
        }
        Row {
            spacing: 22 * page.unit
            width: parent.width

            Button {
                id: league
                objectName: "leagueMode"
                width: (parent.width - parent.spacing) / 2
                height: Math.max(68, 80 * page.unit)
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
                    spacing: 5
                    Text {
                        text: "RMUL"
                        font.family: page.displayFont
                        font.pixelSize: Math.max(26, 30 * page.unit)
                        color: league.checked ? "#fff1c6" : "white"
                    }
                    Text {
                        text: "高校联盟赛"
                        font.pixelSize: Math.max(14, 17 * page.unit)
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
                    spacing: 5
                    Text {
                        text: "RMUC"
                        font.family: page.displayFont
                        font.pixelSize: Math.max(26, 30 * page.unit)
                        color: championship.checked ? "#fff1c6" : "white"
                    }
                    Text {
                        text: "超级对抗赛"
                        font.pixelSize: Math.max(14, 17 * page.unit)
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
            objectName: "enterArenaButton"
            width: parent.width
            height: Math.max(48, 69 * page.unit)
            primary: true
            font.pixelSize: Math.max(20, 25 * page.unit)
            font.bold: true
            text: page.controller.isEntering ? "正在进入…" : page.controller.selectedMode === "" ? "请选择赛事" : "进入赛场    →"
            enabled: page.controller.selectedMode !== "" && !page.controller.isEntering
            onClicked: page.controller.enterArena()
        }
    }
    Column {
        x: page.leftMargin
        y: 586 * page.unit
        width: Math.max(380, 470 * page.unit)
        spacing: 10
        visible: !page.isReady
        Text {
            width: parent.width
            wrapMode: Text.WordWrap
            color: "white"
            font.pixelSize: 16
            text: page.controller.startupState === "loading" ? "正在读取本地设置…" : page.controller.lastError
        }
        Row {
            spacing: 12
            visible: page.controller.startupState === "error"
            BasicButton {
                text: "重试"
                onClicked: page.controller.initialize()
            }
            BasicButton {
                text: "使用默认设置"
                primary: true
                onClicked: page.controller.resetPreferences()
            }
        }
    }

    Column {
        anchors.right: parent.right
        anchors.rightMargin: 60 * page.unit
        y: 464 * page.unit
        spacing: 16 * page.unit
        Repeater {
            model: ["设置", "诊断", "帮助", "关于"]
            BasicButton {
                required property string modelData
                objectName: "tool" + modelData
                quiet: true
                width: Math.max(80, 92 * page.unit)
                height: Math.max(44, 52 * page.unit)
                text: modelData
                font.pixelSize: 15
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
        font.pixelSize: 14
    }
    Text {
        anchors.right: parent.right
        anchors.rightMargin: 60 * page.unit
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 24 * page.unit
        text: page.backgroundFailed ? "背景不可用 · 已使用备用画面" : "背景：RoboMaster 2026 官方主视觉"
        color: "#e3edf5"
        font.pixelSize: 12
    }
    Text {
        x: page.leftMargin
        y: 805 * page.unit
        width: Math.min(650, page.width * 0.65)
        visible: page.isReady && page.controller.lastError !== ""
        text: page.controller.lastError
        wrapMode: Text.WordWrap
        color: "#ffe2a6"
        font.pixelSize: 14
    }
}
