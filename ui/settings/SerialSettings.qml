pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"

// 本页只编辑串口草稿；设备、协议和 MQTT 转发均由 C++ 管理。
SettingsPage {
    id: panel
    // GUI 对象由比赛会话拥有；页面不改变它的生命周期。
    required property var controller
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
    // 新比赛会话恢复已保存值，开关菜单和切换分区不提交或丢弃当前草稿。
    function resetDraft() {
        const config = controller.configuration;
        device.text = config.portName;
        baud.text = String(config.baudRate);
        selectValue(bits, config.dataBits);
        selectValue(parity, config.parity);
        selectValue(stop, config.stopBits);
        selectValue(flow, config.flowControl);
        detected.currentIndex = -1;
        resetScroll();
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

    title: "接入自定义控制器"
    eyebrow: "CUSTOM CONTROLLER / 03"
    description: "如果使用自定义控制器，请连接串口并检查有效数据。"
    formObjectName: "serialFormScroll"
    actionData: [
        VideoButton {
            objectName: "serialConnectButton"
            Layout.fillWidth: true
            visible: panel.inUse
            uiScale: panel.uiScale
            primary: !panel.check.passed
            text: panel.check.passed ? "重新检查" : "连接并检查"
            enabled: device.text.length > 0 && baud.acceptableInput
            onClicked: panel.saveDraft(true)
        },
        VideoButton {
            objectName: "serialSaveButton"
            Layout.fillWidth: true
            visible: panel.inUse
            uiScale: panel.uiScale
            quiet: true
            text: "仅保存参数"
            enabled: device.text.length > 0 && baud.acceptableInput
            onClicked: panel.saveDraft(false)
        },
        VideoButton {
            objectName: "serialDisconnectButton"
            Layout.fillWidth: true
            visible: panel.inUse
            uiScale: panel.uiScale
            quiet: true
            text: "断开"
            enabled: panel.controller.state !== "disconnected"
            onClicked: panel.controller.stop()
        }
    ]
    GridLayout {
        Layout.fillWidth: true
        columns: panel.width < 700 * panel.uiScale ? 1 : 2
        columnSpacing: 12 * panel.uiScale
        rowSpacing: 12 * panel.uiScale
        Repeater {
            model: [false, true]
            Button {
                id: choice
                required property bool modelData
                objectName: modelData ? "serialUseController" : "serialNotUsed"
                Layout.fillWidth: true
                implicitHeight: 96 * panel.uiScale
                text: modelData ? "使用自定义控制器" : "不使用自定义控制器"
                onClicked: panel.usageChanged(modelData)
                contentItem: RowLayout {
                    spacing: 12 * panel.uiScale
                    Rectangle {
                        implicitWidth: 15 * panel.uiScale
                        implicitHeight: implicitWidth
                        radius: width / 2
                        color: "white"
                        border.width: panel.inUse === choice.modelData ? 4 * panel.uiScale : 1
                        border.color: panel.inUse === choice.modelData ? "#c54d25" : "#cbd0d5"
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        Text {
                            Layout.fillWidth: true
                            text: choice.text
                            wrapMode: Text.WordWrap
                            color: "#414952"
                            font.pixelSize: 13 * panel.uiScale
                        }
                        Text {
                            Layout.fillWidth: true
                            text: choice.modelData ? "通过串口连接，检查协议有效帧" : "无需连接串口设备"
                            wrapMode: Text.WordWrap
                            color: "#959ca4"
                            font.pixelSize: 10 * panel.uiScale
                        }
                    }
                }
                padding: 16 * panel.uiScale
                background: Rectangle {
                    radius: 5 * panel.uiScale
                    color: panel.inUse === choice.modelData ? "#fdf7f4" : "white"
                    border.color: choice.activeFocus || panel.inUse === choice.modelData ? "#c54d25" : "#dce0e3"
                }
            }
        }
    }
    Rectangle {
        Layout.fillWidth: true
        visible: !panel.inUse
        implicitHeight: 170 * panel.uiScale
        color: "#f8f9f9"
        radius: 5 * panel.uiScale
        border.color: "#d9dee2"
        ColumnLayout {
            anchors.centerIn: parent
            width: parent.width - 36 * panel.uiScale
            spacing: 12 * panel.uiScale
            Text {
                Layout.fillWidth: true
                text: "这一步可以跳过"
                horizontalAlignment: Text.AlignHCenter
                color: "#414952"
                font.pixelSize: 14 * panel.uiScale
            }
            Text {
                Layout.fillWidth: true
                text: "不需要选择设备或修改串口参数。\n已有参数会保留，不影响键鼠检查。"
                wrapMode: Text.WordWrap
                horizontalAlignment: Text.AlignHCenter
                color: "#929aa3"
                font.pixelSize: 12 * panel.uiScale
                lineHeight: 1.7
            }
        }
    }
    ColumnLayout {
        Layout.fillWidth: true
        visible: panel.inUse
        spacing: 16 * panel.uiScale
        Text {
            text: "串口配置"
            color: "#23272c"
            font.pixelSize: 16 * panel.uiScale
            font.weight: Font.DemiBold
        }
        VideoButton {
            id: serialAdvanced
            objectName: "serialAdvanced"
            uiScale: panel.uiScale
            quiet: true
            checkable: true
            text: checked ? "收起串口参数" : "展开串口参数"
        }
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
                visible: serialAdvanced.checked
                text: "数据位"
                color: "#62666e"
                font.pixelSize: 14 * panel.uiScale
            }
            VideoChoice {
                id: bits
                visible: serialAdvanced.checked
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
                visible: serialAdvanced.checked
                text: "校验位"
                color: "#62666e"
                font.pixelSize: 14 * panel.uiScale
            }
            VideoChoice {
                id: parity
                visible: serialAdvanced.checked
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
                visible: serialAdvanced.checked
                text: "停止位"
                color: "#62666e"
                font.pixelSize: 14 * panel.uiScale
            }
            VideoChoice {
                id: stop
                visible: serialAdvanced.checked
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
                visible: serialAdvanced.checked
                text: "流控"
                color: "#62666e"
                font.pixelSize: 14 * panel.uiScale
            }
            VideoChoice {
                id: flow
                visible: serialAdvanced.checked
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
        Text {
            objectName: "serialError"
            Layout.fillWidth: true
            visible: text !== ""
            text: panel.controller.error
            color: "#b34337"
            font.pixelSize: 11 * panel.uiScale
            wrapMode: Text.WrapAnywhere
        }
    }
}
