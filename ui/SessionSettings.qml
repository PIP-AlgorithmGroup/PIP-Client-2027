pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 本地接入草稿；协议字段和 Topic 不开放编辑，网络操作交给 C++ 会话。
ScrollView {
    id: panel
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
        robot.text = cfg.robotId > 0 ? String(cfg.robotId) : "";
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
            text: "机器人 ID / MQTT clientID"
            color: "#627e8f"
            font.pixelSize: 12 * panel.uiScale
        }
        VideoField {
            id: robot
            objectName: "sessionRobotId"
            Layout.fillWidth: true
            uiScale: panel.uiScale
            placeholderText: "例如：红方步兵 3，蓝方步兵 103"
            validator: IntValidator {
                bottom: 1
                top: 109
            }
            Accessible.name: "机器人 ID"
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
                enabled: panel.selectedInterface !== "" && robot.acceptableInput && port.acceptableInput
                onClicked: {
                    if (panel.session.configure(panel.selectedInterface, local.text, Number(robot.text), host.text, Number(port.text)))
                        panel.session.start();
                }
            }
        }
    }
}
