pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"

// 本页只编辑串口草稿；设备、协议和 MQTT 转发均由 C++ 管理。
Item {
    id: panel
    // GUI 对象由比赛会话拥有；页面不改变它的生命周期。
    required property var controller
    required property real uiScale
    // 可选设备意图由菜单保存；选择不使用会让宿主断开串口，但保留表单草稿。
    required property bool inUse
    signal usageChanged(bool is_used)
    readonly property var counters: controller.diagnostics
    // 使用完整参数比较，未提交的串口草稿不改变实际连接或检查证据。
    readonly property bool hasDraftChanges: device.text !== controller.configuration.portName || Number(baud.text) !== controller.configuration.baudRate || bits.currentValue !== controller.configuration.dataBits || parity.currentValue !== controller.configuration.parity || stop.currentValue !== controller.configuration.stopBits || flow.currentValue !== controller.configuration.flowControl
    readonly property string stateLabel: ({
            disconnected: "已断开",
            connecting: "正在连接",
            waiting: "等待有效数据",
            receiving: "正在接收",
            error: "连接错误"
        })[controller.state] || controller.state

    function selectValue(choice, value) {
        choice.currentIndex = -1;
        for (let index = 0; index < choice.model.length; ++index) {
            if (String(choice.model[index].value) === String(value)) {
                choice.currentIndex = index;
                break;
            }
        }
    }
    // 每次打开菜单恢复已保存值，切换菜单分区不提交或丢弃当前草稿。
    function resetDraft() {
        const config = controller.configuration;
        device.text = config.portName;
        baud.text = String(config.baudRate);
        selectValue(bits, config.dataBits);
        selectValue(parity, config.parity);
        selectValue(stop, config.stopBits);
        selectValue(flow, config.flowControl);
        detected.currentIndex = -1;
        const flickable = form.contentItem as Flickable;
        if (flickable)
            flickable.contentY = 0;
    }
    function saveDraft(connectDevice) {
        const config = {
            portName: device.text,
            baudRate: Number(baud.text),
            dataBits: bits.currentValue,
            parity: parity.currentValue,
            stopBits: stop.currentValue,
            flowControl: flow.currentValue
        };
        if (controller.configure(config) && connectDevice)
            controller.start();
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 28 * panel.uiScale
        spacing: 16 * panel.uiScale
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 6 * panel.uiScale
            Text {
                text: "CONTROLLER / SERIAL"
                color: "#c94b17"
                font.pixelSize: 11 * panel.uiScale
                font.letterSpacing: 2
            }
            Text {
                text: "自定义控制器"
                color: "#24272b"
                font.pixelSize: 24 * panel.uiScale
                font.weight: Font.DemiBold
            }
            Text {
                Layout.fillWidth: true
                text: "可选 · 配置串口并检查有效数据。设置页内不转发控制；返回画面后仅转发新数据。"
                color: "#62666e"
                font.pixelSize: 13 * panel.uiScale
                wrapMode: Text.WordWrap
            }
        }
        RowLayout {
            Layout.fillWidth: true
            VideoButton {
                objectName: "serialUseController"
                uiScale: panel.uiScale
                text: "使用控制器"
                primary: panel.inUse
                onClicked: panel.usageChanged(true)
            }
            VideoButton {
                objectName: "serialNotUsed"
                uiScale: panel.uiScale
                text: "不使用"
                primary: !panel.inUse
                onClicked: panel.usageChanged(false)
            }
        }
        Text {
            Layout.fillWidth: true
            text: panel.inUse ? "串口：" + panel.stateLabel + "    MQTT 转发：" + panel.controller.forwardingState : "本次不使用控制器；配置仍会保留。"
            color: "#24272b"
            font.pixelSize: 14 * panel.uiScale
            wrapMode: Text.WordWrap
        }
        Text {
            objectName: "serialError"
            Layout.fillWidth: true
            text: panel.controller.error
            visible: text.length > 0
            color: "#a33420"
            font.pixelSize: 13 * panel.uiScale
            wrapMode: Text.WrapAnywhere
        }
        ScrollView {
            id: form
            objectName: "serialFormScroll"
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: availableWidth
            clip: true
            ColumnLayout {
                width: form.availableWidth
                spacing: 14 * panel.uiScale
                RowLayout {
                    Layout.fillWidth: true
                    VideoChoice {
                        id: detected
                        objectName: "serialPortChoice"
                        Layout.fillWidth: true
                        uiScale: panel.uiScale
                        model: panel.controller.ports
                        valueRole: "portName"
                        placeholderText: count ? "选择检测到的设备" : "未检测到串口，可手动填写路径"
                        onActivated: device.text = currentValue
                    }
                    VideoButton {
                        objectName: "serialRefreshButton"
                        uiScale: panel.uiScale
                        text: "刷新"
                        onClicked: panel.controller.refreshPortsAsync()
                    }
                }
                GridLayout {
                    Layout.fillWidth: true
                    columns: 2
                    columnSpacing: 16 * panel.uiScale
                    rowSpacing: 12 * panel.uiScale
                    Text {
                        text: "设备路径"
                        color: "#62666e"
                        font.pixelSize: 14 * panel.uiScale
                    }
                    VideoField {
                        id: device
                        objectName: "serialPortName"
                        Layout.fillWidth: true
                        uiScale: panel.uiScale
                        placeholderText: "COM 端口或 /dev/ 设备路径"
                        maximumLength: 1024
                        Accessible.name: "串口设备路径"
                    }
                    Text {
                        text: "波特率"
                        color: "#62666e"
                        font.pixelSize: 14 * panel.uiScale
                    }
                    VideoField {
                        id: baud
                        objectName: "serialBaudRate"
                        Layout.fillWidth: true
                        uiScale: panel.uiScale
                        validator: IntValidator {
                            bottom: 1
                            top: 4000000
                        }
                        Accessible.name: "串口波特率"
                    }
                    Text {
                        text: "数据位"
                        color: "#62666e"
                        font.pixelSize: 14 * panel.uiScale
                    }
                    VideoChoice {
                        id: bits
                        objectName: "serialDataBits"
                        Layout.fillWidth: true
                        uiScale: panel.uiScale
                        valueRole: "value"
                        model: [
                            {
                                label: "5",
                                value: 5
                            },
                            {
                                label: "6",
                                value: 6
                            },
                            {
                                label: "7",
                                value: 7
                            },
                            {
                                label: "8",
                                value: 8
                            }
                        ]
                        Accessible.name: "串口数据位"
                    }
                    Text {
                        text: "校验位"
                        color: "#62666e"
                        font.pixelSize: 14 * panel.uiScale
                    }
                    VideoChoice {
                        id: parity
                        objectName: "serialParity"
                        Layout.fillWidth: true
                        uiScale: panel.uiScale
                        valueRole: "value"
                        model: [
                            {
                                label: "无校验（N）",
                                value: "none"
                            },
                            {
                                label: "偶校验（E）",
                                value: "even"
                            },
                            {
                                label: "奇校验（O）",
                                value: "odd"
                            },
                            {
                                label: "固定 0（S）",
                                value: "space"
                            },
                            {
                                label: "固定 1（M）",
                                value: "mark"
                            }
                        ]
                        Accessible.name: "串口校验位"
                    }
                    Text {
                        text: "停止位"
                        color: "#62666e"
                        font.pixelSize: 14 * panel.uiScale
                    }
                    VideoChoice {
                        id: stop
                        objectName: "serialStopBits"
                        Layout.fillWidth: true
                        uiScale: panel.uiScale
                        valueRole: "value"
                        model: [
                            {
                                label: "1",
                                value: "1"
                            },
                            {
                                label: "1.5",
                                value: "1.5"
                            },
                            {
                                label: "2",
                                value: "2"
                            }
                        ]
                        Accessible.name: "串口停止位"
                    }
                    Text {
                        text: "流控"
                        color: "#62666e"
                        font.pixelSize: 14 * panel.uiScale
                    }
                    VideoChoice {
                        id: flow
                        objectName: "serialFlowControl"
                        Layout.fillWidth: true
                        uiScale: panel.uiScale
                        valueRole: "value"
                        model: [
                            {
                                label: "无流控",
                                value: "none"
                            },
                            {
                                label: "硬件 RTS/CTS",
                                value: "hardware"
                            },
                            {
                                label: "软件 XON/XOFF",
                                value: "software"
                            }
                        ]
                        Accessible.name: "串口流控"
                    }
                }
                Text {
                    Layout.fillWidth: true
                    text: "有效帧 " + (panel.counters.acceptedFrames || 0) + "  ·  帧错误 " + (panel.counters.frameErrors || 0) + "  ·  序号丢弃 " + (panel.counters.sequenceDrops || 0) + "\nMQTT 本地发送 " + (panel.counters.sentCount || 0) + "  ·  发送失败 " + (panel.counters.failedCount || 0) + "\n合并 " + (panel.counters.coalescedFrames || 0) + "  ·  离线丢弃 " + (panel.counters.offlineDrops || 0) + "  ·  过期丢弃 " + (panel.counters.staleDrops || 0)
                    color: "#62666e"
                    font.pixelSize: 12 * panel.uiScale
                    wrapMode: Text.WordWrap
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 10 * panel.uiScale
            VideoButton {
                objectName: "serialSaveButton"
                uiScale: panel.uiScale
                text: "保存"
                enabled: device.text.length > 0 && baud.acceptableInput
                onClicked: panel.saveDraft(false)
            }
            VideoButton {
                objectName: "serialConnectButton"
                uiScale: panel.uiScale
                primary: true
                text: "连接并检查"
                enabled: device.text.length > 0 && baud.acceptableInput
                onClicked: {
                    panel.usageChanged(true);
                    panel.saveDraft(true);
                }
            }
            Item {
                Layout.fillWidth: true
            }
            VideoButton {
                objectName: "serialDisconnectButton"
                uiScale: panel.uiScale
                text: "断开"
                enabled: panel.controller.state !== "disconnected"
                onClicked: panel.controller.stop()
            }
        }
    }
}
