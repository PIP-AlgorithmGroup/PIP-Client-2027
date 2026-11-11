pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 单一模态工具面板；关闭后由调用者恢复焦点，不持有比赛或网络会话。
Popup {
    id: panel
    // C++ AppController 注入，不在面板中创建或持有。
    required property var controller
    required property string connectionLabel
    required property string graphicsBackend
    required property string backgroundState
    property string section: "设置"
    width: Math.min(540, parent.width - 48)
    height: Math.min(570, parent.height - 48)
    anchors.centerIn: parent
    modal: true
    focus: true
    padding: 26
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    onOpened: closeButton.forceActiveFocus(Qt.TabFocusReason)
    background: Rectangle {
        color: "#f4f8fc"
        radius: 16
        border.color: "#cbdce7"
    }
    Overlay.modal: Rectangle {
        color: "#6620374b"
    }
    contentItem: ColumnLayout {
        Accessible.name: panel.section
        spacing: 20
        RowLayout {
            Layout.fillWidth: true
            Text {
                text: panel.section
                font.pixelSize: 26
                font.bold: true
                color: "#183a4e"
            }
            Item {
                Layout.fillWidth: true
            }
            Button {
                id: closeButton
                objectName: "closePanelButton"
                text: "关闭"
                Accessible.name: "关闭" + panel.section
                onClicked: panel.close()
            }
        }
        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: availableWidth
            clip: true
            ColumnLayout {
                width: parent.width
                spacing: 16
                ColumnLayout {
                    Layout.fillWidth: true
                    visible: panel.section === "设置"
                    spacing: 16
                    Text {
                        Layout.fillWidth: true
                        text: "显示与动效"
                        color: "#183a4e"
                        font.pixelSize: 18
                        font.bold: true
                    }
                    Switch {
                        id: motionSwitch
                        objectName: "motionSwitch"
                        text: "启用背景轻动效"
                        checked: panel.controller.motionEnabled
                        enabled: panel.controller.startupState === "ready"
                        onClicked: {
                            panel.controller.setMotionEnabled(checked);
                            // 写入失败时也重新取已应用值，避免开关显示未保存的偏好。
                            checked = Qt.binding(function () {
                                return panel.controller.motionEnabled;
                            });
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        text: "关闭后保持静态画面。最小化、切到其他窗口或进入赛场时会暂停动效。"
                        wrapMode: Text.WordWrap
                        color: "#526b7d"
                        font.pixelSize: 14
                    }
                    Switch {
                        objectName: "fullscreenSwitch"
                        text: "全屏显示"
                        checked: panel.controller.fullScreen
                        enabled: panel.controller.startupState === "ready"
                        onClicked: {
                            panel.controller.setFullScreen(checked);
                            checked = Qt.binding(function () {
                                return panel.controller.fullScreen;
                            });
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        text: "F11 切换全屏；Esc 优先关闭面板，再退出全屏。设置自动保存。"
                        wrapMode: Text.WordWrap
                        color: "#526b7d"
                        font.pixelSize: 14
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    visible: panel.section === "诊断"
                    spacing: 12
                    Repeater {
                        model: panel.controller.diagnostics
                        Text {
                            required property var modelData
                            Layout.fillWidth: true
                            text: modelData.label + "：" + modelData.value
                            color: "#183a4e"
                            font.pixelSize: 15
                            wrapMode: Text.WrapAnywhere
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        text: "渲染后端：" + panel.graphicsBackend + "\n背景加载：" + panel.backgroundState + "\n本地设置：" + panel.controller.startupState + "\n数据链路：" + panel.connectionLabel
                        color: "#183a4e"
                        font.pixelSize: 15
                        wrapMode: Text.WordWrap
                    }
                    Text {
                        Layout.fillWidth: true
                        text: "GPU / NPU 工作负载尚未检查；设备名称不代表推理或视频能力已验证。"
                        color: "#526b7d"
                        font.pixelSize: 14
                        wrapMode: Text.WordWrap
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    visible: panel.section === "帮助"
                    spacing: 16
                    Text {
                        Layout.fillWidth: true
                        text: "1. 选择 RMUL 高校联盟赛或 RMUC 超级对抗赛。\n\n" + "2. 点击进入赛场，打开对应的本地页面。当前比赛工作台尚未设计。\n\n" + "3. Tab / Shift+Tab 移动焦点，方向键切换赛事，Space 选择，Enter 激活按钮。\n\n" + "4. 进入赛事后可返回欢迎页重新选择。启动和进入均不会自动连接网络。"
                        font.pixelSize: 16
                        wrapMode: Text.WordWrap
                        color: "#183a4e"
                    }
                    Button {
                        text: "打开 RoboMaster 官方资料中心 ↗"
                        onClicked: Qt.openUrlExternally("https://bbs.robomaster.com/wiki/20204847")
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    visible: panel.section === "关于"
                    spacing: 16
                    Text {
                        Layout.fillWidth: true
                        text: "PIP CLIENT  v" + panel.controller.version + "\nPIP 战队 · 机甲大师自定义终端\n\n" + "背景：RoboMaster 2026 全国赛官方主视觉，来自组委会宣传礼包。版权归原权利人，" + "仅按礼包许可用于战队宣传，禁止商业化使用及品牌标识变形变色。\n\n" + "字体：Barlow / SIL Open Font License 1.1。\n" + "框架：Qt 6 / Qt Quick / Qt MQTT；协议：Protobuf 3.20.3。"
                        color: "#183a4e"
                        wrapMode: Text.WordWrap
                        font.pixelSize: 15
                    }
                    Button {
                        text: "查看资源来源与字体许可"
                        onClicked: licenseText.visible = !licenseText.visible
                    }
                    TextArea {
                        id: licenseText
                        objectName: "licenseText"
                        Layout.fillWidth: true
                        visible: false
                        text: panel.controller.resourceNotices
                        readOnly: true
                        wrapMode: TextEdit.Wrap
                        selectByMouse: true
                        font.pixelSize: 13
                        Accessible.name: "资源来源与许可全文"
                    }
                }
                Text {
                    Layout.fillWidth: true
                    visible: panel.controller.lastError !== ""
                    text: panel.controller.lastError
                    color: "#9b3c17"
                    wrapMode: Text.WordWrap
                    font.pixelSize: 14
                }
            }
        }
    }
}
