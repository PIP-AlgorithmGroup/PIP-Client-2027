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
    required property real uiScale
    font.pixelSize: 16 * uiScale
    property string section: "设置"
    width: Math.min(540 * uiScale, parent.width - 48)
    height: Math.min(570 * uiScale, parent.height - 48)
    anchors.centerIn: parent
    modal: true
    focus: true
    padding: 26 * uiScale
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
        spacing: 20 * panel.uiScale
        RowLayout {
            Layout.fillWidth: true
            Text {
                text: panel.section
                font.pixelSize: 26 * panel.uiScale
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
                spacing: 16 * panel.uiScale
                ColumnLayout {
                    Layout.fillWidth: true
                    visible: panel.section === "设置"
                    spacing: 16 * panel.uiScale
                    Text {
                        Layout.fillWidth: true
                        text: "显示与动效"
                        color: "#183a4e"
                        font.pixelSize: 18 * panel.uiScale
                        font.bold: true
                    }
                    Text {
                        Layout.fillWidth: true
                        text: "UI 大小"
                        font.pixelSize: 16 * panel.uiScale
                        color: "#183a4e"
                    }
                    RowLayout {
                        spacing: 12 * panel.uiScale
                        Repeater {
                            model: [
                                {
                                    value: "small",
                                    label: "小"
                                },
                                {
                                    value: "medium",
                                    label: "中"
                                },
                                {
                                    value: "large",
                                    label: "大"
                                }
                            ]
                            RadioButton {
                                required property var modelData
                                objectName: "uiSize_" + modelData.value
                                text: modelData.label
                                checked: panel.controller.uiSize === modelData.value
                                autoExclusive: false
                                enabled: panel.controller.startupState === "ready"
                                Accessible.name: "UI 大小：" + modelData.label
                                onClicked: {
                                    panel.controller.setUiSize(modelData.value);
                                    checked = Qt.binding(function () {
                                        return panel.controller.uiSize === modelData.value;
                                    });
                                }
                            }
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        text: "调整文字和按钮大小，窗口缩放时保持此档位。"
                        font.pixelSize: 14 * panel.uiScale
                        color: "#526b7d"
                        wrapMode: Text.WordWrap
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
                        text: "开启后立即预览背景缓慢推近，文字和按钮保持不动。最小化、切到其他窗口或进入赛场时暂停。"
                        wrapMode: Text.WordWrap
                        color: "#526b7d"
                        font.pixelSize: 14 * panel.uiScale
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
                        text: "F11 切换全屏；欢迎页 Esc 关闭面板或退出全屏，RMUL 中 Esc 打开客户端菜单。设置自动保存。"
                        wrapMode: Text.WordWrap
                        color: "#526b7d"
                        font.pixelSize: 14 * panel.uiScale
                    }
                    VerticalSyncSettings {
                        objectName: "welcomeVerticalSync"
                        Layout.fillWidth: true
                        controller: panel.controller
                        uiScale: panel.uiScale
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    visible: panel.section === "诊断"
                    spacing: 12 * panel.uiScale
                    Repeater {
                        model: panel.controller.diagnostics
                        Text {
                            required property var modelData
                            Layout.fillWidth: true
                            text: modelData.label + "：" + modelData.value
                            color: "#183a4e"
                            font.pixelSize: 15 * panel.uiScale
                            wrapMode: Text.WrapAnywhere
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        text: "渲染后端：" + panel.graphicsBackend + "\n背景加载：" + panel.backgroundState + "\n本地设置：" + panel.controller.startupState + "\n数据链路：" + panel.connectionLabel
                        color: "#183a4e"
                        font.pixelSize: 15 * panel.uiScale
                        wrapMode: Text.WordWrap
                    }
                    Text {
                        Layout.fillWidth: true
                        text: "GPU / NPU 工作负载尚未检查；设备名称不代表推理或视频能力已验证。"
                        color: "#526b7d"
                        font.pixelSize: 14 * panel.uiScale
                        wrapMode: Text.WordWrap
                    }
                    Text {
                        Layout.fillWidth: true
                        text: panel.controller.loggingStatus
                        color: "#526b7d"
                        font.pixelSize: 14 * panel.uiScale
                        wrapMode: Text.WordWrap
                    }
                    Button {
                        objectName: "openWelcomeLogs"
                        text: "打开本地日志目录"
                        enabled: panel.controller.logDirectory !== ""
                        onClicked: panel.controller.openLogDirectory()
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    visible: panel.section === "帮助"
                    spacing: 16 * panel.uiScale
                    Text {
                        Layout.fillWidth: true
                        text: "1. 选择 RMUL 高校联盟赛或 RMUC 超级对抗赛。\n\n" + "2. RMUL 进入后监听已保存的 UDP 图传配置，画面等比居中显示；Esc 打开客户端菜单，图传只是其中一个分区。返回欢迎页停止接收。RMUC 保留占位页面。\n\n" + "3. Tab / Shift+Tab 移动焦点，方向键切换赛事，Space 选择，Enter 激活按钮。\n\n" + "4. F11 切换全屏。启动不联网；MQTT 业务订阅和字段映射保持留空。"
                        font.pixelSize: 16 * panel.uiScale
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
                    spacing: 16 * panel.uiScale
                    Text {
                        Layout.fillWidth: true
                        text: "PIP CLIENT  v" + panel.controller.version + "\nPIP 战队 · 机甲大师自定义终端\n\n" + "背景：RoboMaster 2026 全国赛官方主视觉，来自组委会宣传礼包。版权归原权利人，" + "仅按礼包许可用于战队宣传，禁止商业化使用及品牌标识变形变色。\n\n" + "字体：Noto Sans SC（中文）/ Barlow（品牌标题），SIL Open Font License 1.1。\n图标：Lucide，ISC / MIT。\n" + "框架：Qt 6 / Qt Quick / Qt MQTT；协议：Protobuf 3.20.3。"
                        color: "#183a4e"
                        wrapMode: Text.WordWrap
                        font.pixelSize: 15 * panel.uiScale
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
                        font.pixelSize: 13 * panel.uiScale
                        Accessible.name: "资源来源与许可全文"
                    }
                }
                Text {
                    Layout.fillWidth: true
                    visible: panel.controller.lastError !== ""
                    text: panel.controller.lastError
                    color: "#9b3c17"
                    wrapMode: Text.WordWrap
                    font.pixelSize: 14 * panel.uiScale
                }
            }
        }
    }
}
