pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"

// 四项证据来自原生事件线程；输出开关是准备检查门槛，测试页仍不发送控制。
SettingsPage {
    id: panel
    required property var input
    property real sensitivity: input.configuration.sensitivity
    property bool invertY: input.configuration.invertY
    property int frequency: input.configuration.frequency
    readonly property var stats: input.diagnostics
    readonly property var pressed: stats.keys || []
    property bool confirmed: false
    // 使用后端本场输出意图判断；重新进入比赛后必须手动开启。
    readonly property bool outputEnabled: input.configuration.enabled
    readonly property bool directionsChecked: [17, 30, 31, 32].every(code => (stats.testedKeys || []).indexOf(code) >= 0)
    readonly property bool comboChecked: !!stats.testComboReleased
    readonly property bool motionChecked: !!stats.testMouseMoved
    readonly property bool buttonsChecked: [272, 273].every(code => (stats.testedKeys || []).indexOf(code) >= 0) && !!stats.testWheelMoved
    // 键名和组合键证据独立于 GUI 刷新率，短促的按下/松开也由原生状态保存。
    readonly property var checkItems: [
        {
            label: "依次按 W / A / S / D",
            detail: "观察按键高亮与已测标记",
            passed: directionsChecked
        },
        {
            label: "同时按住 Shift + W，再全部松开",
            detail: "确认组合键和释放状态",
            passed: comboChecked
        },
        {
            label: "移动鼠标",
            detail: "观察相对移动量",
            passed: motionChecked
        },
        {
            label: "左键、右键与滚轮",
            detail: "分别点击并滚动滚轮",
            passed: buttonsChecked
        }
    ]
    readonly property bool canConfirm: outputEnabled && checkItems.every(item => item.passed) && !stats.error
    onOutputEnabledChanged: confirmed = false
    onSensitivityChanged: confirmed = false
    onInvertYChanged: confirmed = false
    onFrequencyChanged: confirmed = false
    Connections {
        target: panel.input
        function onConfigurationChanged() {
            panel.confirmed = false;
        }
        function onDiagnosticsChanged() {
            if (panel.stats.error)
                panel.confirmed = false;
        }
    }
    function resetDraft() {
        sensitivity = input.configuration.sensitivity;
        invertY = input.configuration.invertY;
        frequency = input.configuration.frequency;
        resetScroll();
    }
    function metric(rate, key, decimals) {
        const value = (stats[rate] || {})[key];
        return typeof value === "number" && isFinite(value) ? value.toFixed(decimals) : "0";
    }
    function isDown(code) {
        return pressed.indexOf(code) >= 0;
    }
    title: "试一下你的键鼠"
    eyebrow: "INPUT CHECK / 04"
    description: "完成一组简短操作，确认常用按键和鼠标响应。"
    formObjectName: "inputFormScroll"
    actionData: VideoButton {
        objectName: "inputConfirmAvailable"
        Layout.fillWidth: true
        uiScale: panel.uiScale
        primary: true
        text: panel.confirmed ? "键鼠可用已确认" : "确认键鼠可用"
        enabled: panel.canConfirm && !panel.confirmed
        onClicked: {
            panel.input.setTestLocked(false);
            panel.confirmed = true;
        }
    }
    leadingData: [
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 4 * panel.uiScale
            Switch {
                objectName: "inputEnabled"
                text: "启用输出"
                checked: panel.outputEnabled
                onToggled: {
                    panel.input.setEnabled(checked);
                    checked = Qt.binding(() => panel.outputEnabled);
                }
            }
            Text {
                Layout.fillWidth: true
                text: panel.outputEnabled ? "已启用输出；完成下方测试后确认键鼠可用。" : "请先启用输出，否则键鼠自检无法通过。"
                color: panel.outputEnabled ? "#858c95" : "#c54d25"
                font.pixelSize: 11 * panel.uiScale
                wrapMode: Text.WordWrap
            }
        },
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: banner.implicitHeight + 28 * panel.uiScale
            color: "#f5f7f6"
            radius: 5 * panel.uiScale
            RowLayout {
                id: banner
                anchors.fill: parent
                anchors.margins: 14 * panel.uiScale
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4 * panel.uiScale
                    Text {
                        Layout.fillWidth: true
                        text: "本地测试，不发送控制"
                        color: "#23272c"
                        font.pixelSize: 12 * panel.uiScale
                        font.weight: Font.Medium
                    }
                    Text {
                        Layout.fillWidth: true
                        text: "按 Esc 停止测试并释放光标；原生测试支持无限相对移动。"
                        wrapMode: Text.WordWrap
                        color: "#89918e"
                        font.pixelSize: 10 * panel.uiScale
                    }
                }
                VideoButton {
                    objectName: "inputLockTest"
                    uiScale: panel.uiScale
                    primary: !panel.stats.testLocked
                    text: panel.stats.testLocked ? "停止测试" : "开始测试"
                    onClicked: {
                        if (!panel.stats.testLocked)
                            panel.confirmed = false;
                        panel.input.setTestLocked(!panel.stats.testLocked);
                    }
                }
            }
        },
        RowLayout {
            Layout.fillWidth: true
            Text {
                Layout.fillWidth: true
                text: "键盘响应   ANSI 104 · 完整键盘"
                color: "#23272c"
                font.pixelSize: 14 * panel.uiScale
                font.weight: Font.DemiBold
            }
            VideoButton {
                objectName: "inputReset"
                uiScale: panel.uiScale
                quiet: true
                text: "清空已测记录"
                onClicked: {
                    panel.confirmed = false;
                    panel.input.resetStatistics();
                }
            }
        },
        ScrollView {
            id: keyboardScroll
            objectName: "inputKeyboardScroll"
            Layout.fillWidth: true
            implicitHeight: keyboard.implicitHeight + 16 * panel.uiScale
            contentWidth: Math.max(availableWidth, 668 * panel.uiScale)
            contentHeight: keyboard.implicitHeight
            clip: true
            ScrollBar.vertical.policy: ScrollBar.AlwaysOff
            InputKeyboard {
                id: keyboard
                objectName: "inputKeyboard"
                width: keyboardScroll.contentWidth
                uiScale: panel.uiScale
                pressedKeys: panel.pressed
                testedKeys: panel.stats.testedKeys || []
            }
        },
        Text {
            Layout.fillWidth: true
            text: "● 当前按下     ● 已经测试     ○ 尚未测试"
            color: "#969da5"
            font.pixelSize: 10 * panel.uiScale
        },
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 1
            color: "#e3e5e7"
        }
    ]
    Rectangle {
        Layout.fillWidth: true
        implicitHeight: mouseContent.implicitHeight + 38 * panel.uiScale
        color: "#fafbfb"
        radius: 6 * panel.uiScale
        border.color: "#cfd6dc"
        RowLayout {
            id: mouseContent
            anchors.fill: parent
            anchors.margins: 19 * panel.uiScale
            spacing: 20 * panel.uiScale
            Rectangle {
                Layout.alignment: Qt.AlignTop
                implicitWidth: 43 * panel.uiScale
                implicitHeight: 68 * panel.uiScale
                radius: 22 * panel.uiScale
                color: "transparent"
                border.color: "#b6bfc7"
                Rectangle {
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: 1
                    height: 25 * panel.uiScale
                    color: "#bdc6cd"
                }
                Rectangle {
                    y: 27 * panel.uiScale
                    width: parent.width
                    height: 1
                    color: "#bdc6cd"
                }
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 10 * panel.uiScale
                Text {
                    Layout.fillWidth: true
                    text: "把鼠标移到这里试一试"
                    color: "#414952"
                    font.pixelSize: 12 * panel.uiScale
                }
                Text {
                    Layout.fillWidth: true
                    text: "移动、点击左键和右键，然后滚动滚轮。\n开始测试后，整个窗口都接收原生输入。"
                    color: "#969ea6"
                    font.pixelSize: 10 * panel.uiScale
                    wrapMode: Text.WordWrap
                    lineHeight: 1.6
                }
                Text {
                    Layout.fillWidth: true
                    text: "ΔX " + Number(panel.stats.x || 0).toFixed(1) + " / ΔY " + Number(panel.stats.y || 0).toFixed(1) + " · 滚轮 " + Number(panel.stats.wheel || 0).toFixed(1)
                    color: "#808a93"
                    font.pixelSize: 10 * panel.uiScale
                    wrapMode: Text.WrapAnywhere
                }
                Flow {
                    Layout.fillWidth: true
                    spacing: 6 * panel.uiScale
                    Repeater {
                        model: [
                            {
                                name: "移动",
                                passed: !!panel.stats.testMouseMoved
                            },
                            {
                                name: "左键",
                                passed: (panel.stats.testedKeys || []).indexOf(272) >= 0
                            },
                            {
                                name: "右键",
                                passed: (panel.stats.testedKeys || []).indexOf(273) >= 0
                            },
                            {
                                name: "滚轮",
                                passed: !!panel.stats.testWheelMoved
                            }
                        ]
                        Rectangle {
                            id: pill
                            required property var modelData
                            implicitWidth: pillText.implicitWidth + 14 * panel.uiScale
                            implicitHeight: 25 * panel.uiScale
                            radius: 3 * panel.uiScale
                            color: modelData.passed ? "#edf5f0" : "transparent"
                            border.color: modelData.passed ? "#cdded4" : "#dfe3e6"
                            Text {
                                id: pillText
                                anchors.centerIn: parent
                                text: pill.modelData.name
                                color: pill.modelData.passed ? "#32745a" : "#9aa1a9"
                                font.pixelSize: 9 * panel.uiScale
                            }
                        }
                    }
                }
            }
        }
    }
    VideoButton {
        id: preferences
        objectName: "inputPreferences"
        uiScale: panel.uiScale
        quiet: true
        checkable: true
        text: checked ? "收起灵敏度与频率设置" : "灵敏度与频率设置"
    }
    ColumnLayout {
        Layout.fillWidth: true
        visible: preferences.checked
        spacing: 12 * panel.uiScale
        Text {
            text: "鼠标灵敏度  " + panel.sensitivity.toFixed(2) + "×"
            color: "#656d77"
            font.pixelSize: 12 * panel.uiScale
        }
        Slider {
            objectName: "inputSensitivity"
            Layout.fillWidth: true
            from: 0.05
            to: 10
            stepSize: 0.05
            value: panel.sensitivity
            onMoved: panel.sensitivity = value
        }
        CheckBox {
            text: "反转 Y"
            checked: panel.invertY
            onToggled: panel.invertY = checked
        }
        ComboBox {
            objectName: "inputFrequency"
            model: ["200 Hz · 联调配置", "75 Hz · 协议标注"]
            currentIndex: panel.frequency === 200 ? 0 : 1
            onActivated: panel.frequency = currentIndex === 0 ? 200 : 75
        }
        VideoButton {
            objectName: "inputApply"
            uiScale: panel.uiScale
            text: "保存设置"
            onClicked: panel.input.configure(panel.sensitivity, panel.invertY, panel.frequency)
        }
        Text {
            Layout.fillWidth: true
            text: "200 Hz 的实际转发与接纳需实机验证。"
            color: "#858c95"
            font.pixelSize: 11 * panel.uiScale
            wrapMode: Text.WordWrap
        }
    }
    Text {
        Layout.fillWidth: true
        visible: text !== ""
        text: panel.stats.error || panel.stats.configError || ""
        color: "#b34337"
        font.pixelSize: 11 * panel.uiScale
        wrapMode: Text.WrapAnywhere
    }
    VideoButton {
        id: detailsToggle
        objectName: "inputDetailsToggle"
        uiScale: panel.uiScale
        quiet: true
        checkable: true
        text: checked ? "收起详细统计" : "展开详细统计与说明"
    }
    ColumnLayout {
        Layout.fillWidth: true
        visible: detailsToggle.checked
        spacing: 14 * panel.uiScale
        Repeater {
            model: [
                {
                    key: "keyboard",
                    label: "键盘事件率"
                },
                {
                    key: "mouse",
                    label: "鼠标回报率"
                },
                {
                    key: "send",
                    label: "MQTT 实际发送率"
                }
            ]
            Rectangle {
                id: rateCard
                required property var modelData
                Layout.fillWidth: true
                implicitHeight: rateContents.implicitHeight + 20 * panel.uiScale
                color: "#edeef0"
                radius: 4
                Column {
                    id: rateContents
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: 10 * panel.uiScale
                    spacing: 5 * panel.uiScale
                    Text {
                        width: parent.width
                        wrapMode: Text.WordWrap
                        objectName: "inputRate_" + rateCard.modelData.key
                        text: rateCard.modelData.label + "   " + panel.metric(rateCard.modelData.key, "hz", 0) + " Hz（最近 1 秒）   活跃间隔换算 " + panel.metric(rateCard.modelData.key, "activeHz", 0) + " Hz   累计 " + panel.metric(rateCard.modelData.key, "count", 0)
                        font.pixelSize: 13 * panel.uiScale
                        color: "#24272b"
                    }
                    Text {
                        width: parent.width
                        wrapMode: Text.WordWrap
                        text: "间隔均值 / 最小 / P99 / 最大   " + panel.metric(rateCard.modelData.key, "meanMs", 2) + " / " + panel.metric(rateCard.modelData.key, "minMs", 2) + " / " + panel.metric(rateCard.modelData.key, "p99Ms", 2) + " / " + panel.metric(rateCard.modelData.key, "maxMs", 2) + " ms"
                        font.pixelSize: 12 * panel.uiScale
                        color: "#62666e"
                    }
                }
            }
        }
        Text {
            Layout.fillWidth: true
            text: "统计的是系统实际交付的原生事件，不是 USB 扫描率。键盘只在按下/松开时报告，长按自动重复不计入事件率；静止鼠标没有运动回报。X11 / Wayland 可能合并或拆分设备报告；Wayland 未加速相对量不等于硬件原始计数。间隔统计取最近 256 个样本。"
            wrapMode: Text.WordWrap
            font.pixelSize: 11 * panel.uiScale
            color: "#737780"
        }
        Text {
            objectName: "inputMotion"
            Layout.fillWidth: true
            text: "累计原始相对量   X " + Number(panel.stats.x || 0).toFixed(2) + "   Y " + Number(panel.stats.y || 0).toFixed(2) + "   滚轮 " + Number(panel.stats.wheel || 0).toFixed(2) + "\n按住的物理键码   " + panel.pressed.join(", ") + "\n重复按下 " + (panel.stats.repeats || 0) + "   超限设备 " + (panel.stats.rejectedDevices || 0) + "   中立释放帧 " + (panel.stats.neutralCount || 0)
            wrapMode: Text.WrapAnywhere
            font.pixelSize: 13 * panel.uiScale
            color: "#42464e"
        }
        Text {
            Layout.fillWidth: true
            text: "控制位序：W S A D Shift Ctrl Q E R F G Z X C V B；左右 Shift/Ctrl 合并，鼠标左/右/中独立。关闭总开关、菜单、其他交互页面和失焦暂停控制；MQTT 断线只停止发送，继续采集。离开子页或恢复焦点后，按住的键须释放后重按。"
            wrapMode: Text.WordWrap
            font.pixelSize: 12 * panel.uiScale
            color: "#737780"
        }
        Text {
            Layout.fillWidth: true
            text: [panel.stats.error || "", panel.stats.configError || ""].filter(value => value !== "").join("\n")
            visible: text !== ""
            wrapMode: Text.WordWrap
            color: "#a33d1b"
        }
    }
    Text {
        Layout.fillWidth: true
        visible: !detailsToggle.checked && (panel.stats.error || panel.stats.configError || "") !== ""
        text: panel.stats.error || panel.stats.configError || ""
        color: "#a33d1b"
        wrapMode: Text.WrapAnywhere
    }
}
