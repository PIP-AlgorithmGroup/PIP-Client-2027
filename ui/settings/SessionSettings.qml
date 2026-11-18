pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"

// 会话草稿只在检查动作提交；表单与检查卡一起滚动，公共导航由菜单固定显示。
SettingsPage {
    id: panel
    // GUI 线程依赖由宿主持有；本页不管理其生命周期或后台网络。
    required property var session
    required property var receiver
    required property var pipeline
    required property string connectionLabel
    // 新比赛会话从已保存配置恢复草稿；开关菜单和分类切换不触发恢复或网络操作。
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
        resetScroll();
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
    Connections {
        target: panel.session
        function onInterfacesChanged() {
            panel.syncInterfaceChoice();
        }
    }
    // 切换模型时 currentValue 可能短暂未定义，此时身份无效，不允许保存连接。
    readonly property int selectedRobotId: robot.currentIndex >= 0 && typeof robot.currentValue === "number" ? robot.currentValue : 0
    // 草稿与实际连接分开：未保存编辑不能沿用旧连接的通过结论。
    readonly property bool hasDraftChanges: selectedInterface !== session.configuration.interfaceName || local.text !== session.configuration.localAddress || host.text !== session.configuration.brokerHost || Number(port.text) !== session.configuration.brokerPort || selectedRobotId !== session.configuration.robotId

    title: "接入 MQTT"
    eyebrow: "CONNECTION / 01"
    description: "确认接入网络和机器人身份，建立比赛数据链路。"
    formObjectName: "sessionFormScroll"
    actionData: [
        VideoButton {
            objectName: "sessionConnect"
            Layout.fillWidth: true
            uiScale: panel.uiScale
            primary: !panel.check.passed
            text: panel.session.connecting ? "正在检查…" : panel.check.passed ? "重新检查" : "连接并检查"
            enabled: !panel.session.connecting && panel.selectedInterface !== "" && panel.selectedRobotId > 0 && port.acceptableInput
            onClicked: panel.session.connectAsync(panel.selectedInterface, local.text, panel.selectedRobotId, host.text, Number(port.text))
        },
        VideoButton {
            objectName: "sessionDisconnect"
            Layout.fillWidth: true
            uiScale: panel.uiScale
            quiet: true
            text: "断开 MQTT"
            enabled: panel.session.active || panel.session.connecting
            onClicked: panel.session.stop()
        }
    ]
    RowLayout {
        Layout.fillWidth: true
        Text {
            text: "接入网络"
            color: "#24272b"
            font.pixelSize: 14 * panel.uiScale
            font.weight: Font.DemiBold
        }
        Item {
            Layout.fillWidth: true
        }
        VideoButton {
            objectName: "sessionRestoreDefaults"
            uiScale: panel.uiScale
            implicitHeight: 32 * panel.uiScale
            text: "恢复默认"
            quiet: true
            // 仅修改草稿；保存前保留当前连接与身份。
            onClicked: {
                local.text = "192.168.12.2";
                host.text = "192.168.12.1";
                port.text = "3333";
                panel.syncInterfaceChoice();
            }
        }
    }
    GridLayout {
        Layout.fillWidth: true
        columns: 2
        columnSpacing: 20 * panel.uiScale
        rowSpacing: 8 * panel.uiScale
        Text {
            text: "接入网卡"
            color: "#62666e"
            font.pixelSize: 12 * panel.uiScale
        }
        Text {
            text: "本机 IPv4"
            color: "#62666e"
            font.pixelSize: 12 * panel.uiScale
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredWidth: 1
            spacing: 6 * panel.uiScale
            VideoChoice {
                id: interfaceChoice
                objectName: "sessionInterface"
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                uiScale: panel.uiScale
                model: panel.session.interfaces
                placeholderText: "请选择接入网卡"
                Accessible.name: "接入网卡"
                onActivated: {
                    const entry = panel.session.interfaces[currentIndex];
                    panel.selectedInterface = entry.name;
                    local.text = entry.address;
                }
            }
            VideoButton {
                uiScale: panel.uiScale
                implicitWidth: 54 * panel.uiScale
                text: "刷新"
                onClicked: {
                    panel.session.refreshInterfacesAsync();
                }
            }
        }
        VideoField {
            id: local
            objectName: "sessionLocalAddress"
            Layout.fillWidth: true
            Layout.preferredWidth: 1
            uiScale: panel.uiScale
            Accessible.name: "本机 IPv4"
        }
    }
    ColumnLayout {
        Layout.fillWidth: true
        spacing: 8 * panel.uiScale
        Text {
            text: "MQTT 服务"
            color: "#656d77"
            font.pixelSize: 12 * panel.uiScale
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 12 * panel.uiScale
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 8 * panel.uiScale
                Text {
                    text: "服务器地址"
                    color: "#656d77"
                    font.pixelSize: 12 * panel.uiScale
                }
                VideoField {
                    id: host
                    objectName: "sessionBrokerHost"
                    Layout.fillWidth: true
                    uiScale: panel.uiScale
                    Accessible.name: "MQTT 服务地址"
                }
            }
            ColumnLayout {
                Layout.preferredWidth: 112 * panel.uiScale
                spacing: 8 * panel.uiScale
                Text {
                    text: "TCP 端口"
                    color: "#656d77"
                    font.pixelSize: 12 * panel.uiScale
                }
                VideoField {
                    id: port
                    objectName: "sessionBrokerPort"
                    Layout.fillWidth: true
                    uiScale: panel.uiScale
                    validator: IntValidator {
                        bottom: 1
                        top: 65535
                    }
                    Accessible.name: "MQTT TCP 端口"
                }
            }
        }
    }

    Text {
        Layout.fillWidth: true
        text: "默认本机 192.168.12.2，MQTT 192.168.12.1 : 3333。地址需已配置到网卡；同机测试可用 127.0.0.1。"
        color: "#737780"
        font.pixelSize: 11 * panel.uiScale
        wrapMode: Text.WordWrap
    }
    Rectangle {
        Layout.fillWidth: true
        implicitHeight: 1
        color: "#d9dce0"
    }
    Text {
        text: "机器人身份"
        color: "#24272b"
        font.pixelSize: 14 * panel.uiScale
        font.weight: Font.DemiBold
    }
    RowLayout {
        Layout.fillWidth: true
        spacing: 12 * panel.uiScale
        VideoChoice {
            id: team
            objectName: "sessionTeam"
            Layout.fillWidth: true
            uiScale: panel.uiScale
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
            // 阵营变更后必须重新确认兵种，防止沿用旧身份。
            onActivated: robot.currentIndex = -1
        }
        VideoChoice {
            id: robot
            objectName: "sessionRobotType"
            Layout.fillWidth: true
            uiScale: panel.uiScale
            placeholderText: team.currentIndex < 0 ? "请先选择阵营" : "选择兵种"
            enabled: team.currentIndex >= 0
            valueRole: "robotId"
            model: panel.session.robotOptions(team.currentIndex >= 0 ? team.currentValue : "")
            Accessible.name: "机器人兵种"
        }
    }

    Rectangle {
        Layout.fillWidth: true
        implicitHeight: identityRow.implicitHeight + 26 * panel.uiScale
        color: "#f7f8f9"
        radius: 4 * panel.uiScale
        RowLayout {
            id: identityRow
            anchors.fill: parent
            anchors.margins: 13 * panel.uiScale
            spacing: 10 * panel.uiScale
            Rectangle {
                implicitWidth: 6 * panel.uiScale
                implicitHeight: implicitWidth
                radius: width / 2
                color: team.currentValue === "blue" ? "#3a668c" : "#b34337"
            }
            Text {
                Layout.fillWidth: true
                text: "机器人 ID / MQTT clientID"
                color: "#828994"
                font.pixelSize: 11 * panel.uiScale
                wrapMode: Text.WordWrap
            }
            Text {
                objectName: "sessionRobotId"
                text: panel.selectedRobotId > 0 ? String(panel.selectedRobotId) : "—"
                color: "#414c59"
                font.pixelSize: 17 * panel.uiScale
                font.weight: Font.DemiBold
            }
        }
    }
    Text {
        objectName: "sessionStatus"
        Layout.fillWidth: true
        visible: panel.session.active || panel.session.connecting
        text: "MQTT · " + panel.connectionLabel + "  /  网卡 · " + (panel.session.networkReady ? "地址可用" : "尚未配置或不可用")
        color: "#858c95"
        font.pixelSize: 11 * panel.uiScale
        wrapMode: Text.WordWrap
    }
    Text {
        objectName: "sessionError"
        Layout.fillWidth: true
        text: panel.session.error !== "" ? panel.session.error : panel.pipeline.lastError
        visible: text !== ""
        wrapMode: Text.WrapAnywhere
        color: "#a33d1b"
        font.pixelSize: 12 * panel.uiScale
    }
}
