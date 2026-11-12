pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 本地接入草稿；协议字段和 Topic 不开放编辑，网络操作交给 C++ 会话。
ScrollView {
    id: panel
    objectName: "sessionFormScroll"
    required property var session
    required property var receiver
    required property var pipeline
    required property real uiScale
    required property string connectionLabel
    contentWidth: availableWidth
    clip: true

    function resetDraft() {
        const cfg = session.configuration;
        host.text = cfg.brokerHost;
        port.text = String(cfg.brokerPort);
        // 持久化仍只有 robotId；从官方选项恢复两级草稿，不自动选择默认身份。
        team.currentIndex = cfg.robotId > 0 ? (cfg.robotId > 100 ? 1 : 0) : -1;
        robot.currentIndex = -1;
        for (let i = 0; i < robot.count; ++i) {
            if (robot.model[i].robotId === cfg.robotId)
                robot.currentIndex = i;
        }
        local.text = cfg.localAddress;
        selectedInterface = cfg.interfaceName;
        syncInterfaceChoice();
    }
    // 刷新网卡后按稳定名称/IP 恢复选中项，保留未提交的其他草稿。
    function syncInterfaceChoice() {
        interfaceChoice.currentIndex = -1;
        for (let i = 0; i < session.interfaces.length; ++i) {
            const entry = session.interfaces[i];
            if (entry.name === selectedInterface && entry.address === local.text)
                interfaceChoice.currentIndex = i;
        }
    }
    property string selectedInterface: ""
    // 切换模型时 currentValue 可能短暂未定义，此时身份无效，不允许保存连接。
    readonly property int selectedRobotId: robot.currentIndex >= 0 && typeof robot.currentValue === "number" ? robot.currentValue : 0
    // 身份下拉沿用表单外观，Qt 负责键盘导航、弹层与无障碍行为。
    component IdentityChoice: ComboBox {
        id: choice
        required property string placeholderText
        Layout.fillWidth: true
        implicitHeight: 46 * panel.uiScale
        font.pixelSize: 16 * panel.uiScale
        textRole: "label"
        currentIndex: -1
        contentItem: Text {
            leftPadding: 14 * panel.uiScale
            rightPadding: 26 * panel.uiScale
            text: choice.currentIndex < 0 ? choice.placeholderText : choice.displayText
            color: !choice.enabled || choice.currentIndex < 0 ? "#8a9fac" : "#203c50"
            font: choice.font
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        background: Rectangle {
            radius: 7 * panel.uiScale
            color: choice.enabled ? "#f7fafc" : "#edf2f6"
            border.color: choice.activeFocus ? "#268bad" : "#d6e2e9"
        }
    }
    ColumnLayout {
        width: panel.availableWidth
        spacing: 12 * panel.uiScale
        Text {
            text: "SESSION / NETWORK"
            color: "#32809b"
            font.pixelSize: 11 * panel.uiScale
            font.letterSpacing: 2
        }
        Text {
            text: "当前会话"
            color: "#19384c"
            font.pixelSize: 28 * panel.uiScale
            font.weight: Font.DemiBold
        }
        Text {
            Layout.fillWidth: true
            text: "同一 RJ45 接收视频与裁判数据，发送终端指令。"
            color: "#627e8f"
            wrapMode: Text.WordWrap
            font.pixelSize: 13 * panel.uiScale
        }
        VideoButton {
            objectName: "sessionRestoreDefaults"
            uiScale: panel.uiScale
            text: "恢复官方默认"
            quiet: true
            // 只改当前草稿，保留网卡/机器人选择；用户保存前不影响现有连接。
            onClicked: {
                local.text = "192.168.12.2";
                host.text = "192.168.12.1";
                port.text = "3333";
                panel.syncInterfaceChoice();
            }
        }
        Text {
            text: "接入网卡 / IPv4"
            color: "#627e8f"
            font.pixelSize: 12 * panel.uiScale
        }
        RowLayout {
            Layout.fillWidth: true
            ComboBox {
                id: interfaceChoice
                objectName: "sessionInterface"
                Layout.fillWidth: true
                implicitHeight: 44 * panel.uiScale
                font.pixelSize: 13 * panel.uiScale
                model: panel.session.interfaces
                textRole: "label"
                contentItem: Text {
                    leftPadding: 14 * panel.uiScale
                    rightPadding: 26 * panel.uiScale
                    text: interfaceChoice.currentIndex < 0 ? "请选择接入网卡" : interfaceChoice.displayText
                    color: interfaceChoice.currentIndex < 0 ? "#8a9fac" : "#203c50"
                    font: interfaceChoice.font
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                }
                background: Rectangle {
                    radius: 7 * panel.uiScale
                    color: "#f7fafc"
                    border.color: interfaceChoice.activeFocus ? "#268bad" : "#d6e2e9"
                }
                onActivated: {
                    const entry = panel.session.interfaces[currentIndex];
                    panel.selectedInterface = entry.name;
                    local.text = entry.address;
                }
            }
            VideoButton {
                uiScale: panel.uiScale
                text: "刷新"
                onClicked: {
                    panel.session.refreshInterfaces();
                    panel.syncInterfaceChoice();
                }
            }
        }
        VideoField {
            id: local
            objectName: "sessionLocalAddress"
            Layout.fillWidth: true
            uiScale: panel.uiScale
            Accessible.name: "本机 IPv4"
        }
        Text {
            Layout.fillWidth: true
            text: "官方默认本机 IPv4：192.168.12.2。地址须已配置到所选网卡；同机测试可用 127.0.0.1。"
            wrapMode: Text.WordWrap
            color: "#8b9faa"
            font.pixelSize: 11 * panel.uiScale
        }
        Text {
            text: "MQTT 服务地址 / TCP 端口 · 默认 192.168.12.1 : 3333"
            color: "#627e8f"
            font.pixelSize: 12 * panel.uiScale
        }
        RowLayout {
            Layout.fillWidth: true
            VideoField {
                id: host
                objectName: "sessionBrokerHost"
                Layout.fillWidth: true
                uiScale: panel.uiScale
                Accessible.name: "MQTT 服务地址"
            }
            VideoField {
                id: port
                objectName: "sessionBrokerPort"
                Layout.preferredWidth: 100 * panel.uiScale
                uiScale: panel.uiScale
                validator: IntValidator {
                    bottom: 1
                    top: 65535
                }
                Accessible.name: "MQTT TCP 端口"
            }
        }
        Text {
            text: "机器人身份 · 先选阵营，再选兵种"
            color: "#627e8f"
            font.pixelSize: 12 * panel.uiScale
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 10 * panel.uiScale
            IdentityChoice {
                id: team
                objectName: "sessionTeam"
                placeholderText: "选择红方 / 蓝方"
                valueRole: "key"
                model: [
                    {
                        label: "红方",
                        key: "red"
                    },
                    {
                        label: "蓝方",
                        key: "blue"
                    }
                ]
                Accessible.name: "机器人阵营"
                // 换阵营后重新选择兵种，防止沿用旧身份而误连接。
                onActivated: robot.currentIndex = -1
            }
            IdentityChoice {
                id: robot
                objectName: "sessionRobotType"
                placeholderText: team.currentIndex < 0 ? "请先选择阵营" : "选择兵种"
                enabled: team.currentIndex >= 0
                valueRole: "robotId"
                model: panel.session.robotOptions(team.currentIndex >= 0 ? team.currentValue : "")
                Accessible.name: "机器人兵种"
            }
        }
        Text {
            objectName: "sessionRobotId"
            Layout.fillWidth: true
            text: panel.selectedRobotId > 0 ? "机器人 ID / MQTT clientID · " + panel.selectedRobotId : "机器人 ID / MQTT clientID · 尚未选择"
            color: "#526f80"
            font.pixelSize: 12 * panel.uiScale
        }
        Text {
            Layout.fillWidth: true
            text: "使用机器人编号，不使用 0x0103 等选手端 ID。Topic、Protobuf 和频率按官方协议管理。"
            wrapMode: Text.WordWrap
            color: "#8b9faa"
            font.pixelSize: 11 * panel.uiScale
        }
        Text {
            objectName: "sessionStatus"
            Layout.fillWidth: true
            text: "MQTT · " + panel.connectionLabel + "\n图传 · " + panel.receiver.status + "\n网卡 · " + (panel.session.networkReady ? "地址可用" : "尚未配置或不可用") + "\n控制 · 尚未接入"
            color: "#526f80"
            lineHeight: 1.5
            font.pixelSize: 13 * panel.uiScale
        }
        Text {
            objectName: "sessionError"
            Layout.fillWidth: true
            text: panel.session.error !== "" ? panel.session.error : panel.pipeline.lastError
            visible: text !== ""
            wrapMode: Text.WrapAnywhere
            color: "#a65d2e"
            font.pixelSize: 12 * panel.uiScale
        }
        RowLayout {
            Layout.fillWidth: true
            VideoButton {
                objectName: "sessionDisconnect"
                uiScale: panel.uiScale
                text: "断开 MQTT"
                enabled: panel.session.active
                onClicked: panel.session.stop()
            }
            Item {
                Layout.fillWidth: true
            }
            VideoButton {
                objectName: "sessionConnect"
                uiScale: panel.uiScale
                primary: true
                text: "保存并连接"
                enabled: panel.selectedInterface !== "" && panel.selectedRobotId > 0 && port.acceptableInput
                onClicked: {
                    if (panel.session.configure(panel.selectedInterface, local.text, panel.selectedRobotId, host.text, Number(port.text)))
                        panel.session.start();
                }
            }
        }
    }
}
