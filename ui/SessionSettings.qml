pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 会话草稿只在保存时交给 C++；固定页头与动作栏不参与表单滚动。
Item {
    id: panel
    // GUI 线程依赖由宿主持有；本页不管理其生命周期或后台网络。
    required property var session
    required property var receiver
    required property var pipeline
    required property real uiScale
    required property string connectionLabel
    // 菜单每次打开从已保存配置恢复草稿；分类切换不触发恢复或网络操作。
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
        form.contentItem.contentY = 0;
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
    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        ColumnLayout {
            Layout.fillWidth: true
            Layout.margins: 24 * panel.uiScale
            Layout.leftMargin: 28 * panel.uiScale
            Layout.rightMargin: 28 * panel.uiScale
            spacing: 6 * panel.uiScale
            Text {
                text: "SESSION / NETWORK"
                color: "#c94b17"
                font.pixelSize: 11 * panel.uiScale
                font.letterSpacing: 2
            }
            Text {
                text: "当前会话"
                color: "#24272b"
                font.pixelSize: 24 * panel.uiScale
                font.weight: Font.DemiBold
            }
            Text {
                Layout.fillWidth: true
                text: "通过 RJ45 接入裁判系统，视频与数据链路独立运行。"
                color: "#62666e"
                font.pixelSize: 13 * panel.uiScale
                wrapMode: Text.WordWrap
            }
        }
        ScrollView {
            id: form
            objectName: "sessionFormScroll"
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: 28 * panel.uiScale
            Layout.rightMargin: 28 * panel.uiScale
            Layout.bottomMargin: 16 * panel.uiScale
            contentWidth: availableWidth
            clip: true
            ColumnLayout {
                width: form.availableWidth
                spacing: 10 * panel.uiScale
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
                        text: "恢复官方默认"
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
                                panel.session.refreshInterfaces();
                                panel.syncInterfaceChoice();
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
                    Text {
                        text: "MQTT 服务地址"
                        color: "#62666e"
                        font.pixelSize: 12 * panel.uiScale
                    }
                    Text {
                        text: "TCP 端口"
                        color: "#62666e"
                        font.pixelSize: 12 * panel.uiScale
                    }
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
                        Layout.preferredWidth: 108 * panel.uiScale
                        uiScale: panel.uiScale
                        validator: IntValidator {
                            bottom: 1
                            top: 65535
                        }
                        Accessible.name: "MQTT TCP 端口"
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
                Text {
                    objectName: "sessionRobotId"
                    Layout.fillWidth: true
                    text: panel.selectedRobotId > 0 ? "机器人 ID / MQTT clientID · " + panel.selectedRobotId : "机器人 ID / MQTT clientID · 尚未选择"
                    color: "#62666e"
                    font.pixelSize: 12 * panel.uiScale
                    wrapMode: Text.WordWrap
                }
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: 1
                    color: "#d9dce0"
                }
                Text {
                    text: "连接状态"
                    color: "#24272b"
                    font.pixelSize: 14 * panel.uiScale
                    font.weight: Font.DemiBold
                }
                Text {
                    objectName: "sessionStatus"
                    Layout.fillWidth: true
                    text: "MQTT · " + panel.connectionLabel + "    /    网卡 · " + (panel.session.networkReady ? "地址可用" : "尚未配置或不可用") + "\n图传 · " + panel.receiver.status + "    /    控制 · 尚未接入"
                    color: "#62666e"
                    lineHeight: 1.6
                    font.pixelSize: 13 * panel.uiScale
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
        }
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 1
            color: "#d9dce0"
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 16 * panel.uiScale
            spacing: 10 * panel.uiScale
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
