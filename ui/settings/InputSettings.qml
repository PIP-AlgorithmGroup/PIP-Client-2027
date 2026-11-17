pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"

// 只展示 GUI 合并快照；测试采集在原生线程，测试页始终禁止机器人控制。
Item {
    id: panel
    required property var input
    required property real uiScale
    property real sensitivity: input.configuration.sensitivity
    property bool invertY: input.configuration.invertY
    property int frequency: input.configuration.frequency
    readonly property var stats: input.diagnostics
    readonly property var pressed: stats.keys || []
    // 人工确认只对本次本地测试有效；保存配置或清空统计会撤销，不推断全部 104 键通过。
    property bool keyboardConfirmed: false
    property bool mouseConfirmed: false
    onSensitivityChanged: {
        keyboardConfirmed = false;
        mouseConfirmed = false;
    }
    onInvertYChanged: {
        keyboardConfirmed = false;
        mouseConfirmed = false;
    }
    onFrequencyChanged: {
        keyboardConfirmed = false;
        mouseConfirmed = false;
    }
    // 保留滚动入口供宿主/可访问测试使用，底部动作不属于滚动区域。
    property alias contentItem: inputScroll.contentItem
    function metric(rate, key, decimals) {
        const value = (stats[rate] || {})[key];
        return typeof value === "number" && isFinite(value) ? value.toFixed(decimals) : "0";
    }
    function resetDraft() {
        sensitivity = input.configuration.sensitivity;
        invertY = input.configuration.invertY;
        frequency = input.configuration.frequency;
        const flickable = inputScroll.contentItem as Flickable;
        if (flickable)
            flickable.contentY = 0;
    }
    function isDown(code) {
        return pressed.indexOf(code) >= 0;
    }
    Connections {
        target: panel.input
        function onConfigurationChanged() {
            panel.keyboardConfirmed = false;
            panel.mouseConfirmed = false;
        }
        function onDiagnosticsChanged() {
            if (panel.stats.error) {
                panel.keyboardConfirmed = false;
                panel.mouseConfirmed = false;
            }
        }
    }
    ColumnLayout {
        id: heading
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        spacing: 6 * panel.uiScale
        Text {
            text: "INPUT / LOCAL TEST"
            color: "#c94b17"
            font.pixelSize: 11 * panel.uiScale
            font.letterSpacing: 2
        }
        Text {
            text: "键鼠测试"
            font.pixelSize: 24 * panel.uiScale
            font.weight: Font.DemiBold
            color: "#24272b"
        }
        Text {
            Layout.fillWidth: true
            text: "按键与鼠标响应仅在本地展示，不发送控制。开始测试后隐藏并锁定光标，Esc 或切换窗口可释放。"
            wrapMode: Text.WordWrap
            font.pixelSize: 12 * panel.uiScale
            color: "#737780"
        }
    }
    ScrollView {
        id: inputScroll
        anchors.top: heading.bottom
        anchors.bottom: actions.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.topMargin: 18 * panel.uiScale
        anchors.bottomMargin: 14 * panel.uiScale
        contentWidth: availableWidth
        clip: true
        ColumnLayout {
            width: inputScroll.availableWidth
            spacing: 14 * panel.uiScale
            Text {
                Layout.fillWidth: true
                text: "键盘测试 · ANSI 104 键    橙色：按住    绿色：已测    白色：未测"
                font.pixelSize: 12 * panel.uiScale
                color: "#62666e"
                wrapMode: Text.WordWrap
            }
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
            }
            RowLayout {
                Layout.fillWidth: true
                Repeater {
                    model: [
                        {
                            c: 272,
                            n: "鼠标左键"
                        },
                        {
                            c: 274,
                            n: "鼠标中键"
                        },
                        {
                            c: 273,
                            n: "鼠标右键"
                        },
                        {
                            c: 275,
                            n: "侧键 1"
                        },
                        {
                            c: 276,
                            n: "侧键 2"
                        }
                    ]
                    Rectangle {
                        id: mouseButton
                        required property var modelData
                        Layout.fillWidth: true
                        implicitHeight: 34 * panel.uiScale
                        radius: 5 * panel.uiScale
                        color: panel.isDown(modelData.c) ? "#d65319" : (panel.stats.testedKeys || []).indexOf(modelData.c) >= 0 ? "#e0eee5" : "#e7e9ec"
                        Text {
                            anchors.centerIn: parent
                            text: mouseButton.modelData.n
                            font.pixelSize: 11 * panel.uiScale
                            color: panel.isDown(mouseButton.modelData.c) ? "white" : "#42464e"
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                CheckBox {
                    objectName: "inputConfirmKeyboard"
                    Layout.fillWidth: true
                    text: "确认键盘响应正常"
                    enabled: (panel.stats.testedKeys || []).some(code => code < 272)
                    checked: panel.keyboardConfirmed
                    onToggled: panel.keyboardConfirmed = checked
                }
                CheckBox {
                    objectName: "inputConfirmMouse"
                    Layout.fillWidth: true
                    text: "确认鼠标移动与按钮正常"
                    enabled: (panel.stats.mouse || {}).count > 0 && (panel.stats.testedKeys || []).some(code => code >= 272 && code <= 276)
                    checked: panel.mouseConfirmed
                    onToggled: panel.mouseConfirmed = checked
                }
            }
            Text {
                Layout.fillWidth: true
                text: "鼠标相对量  X " + Number(panel.stats.x || 0).toFixed(1) + "  /  Y " + Number(panel.stats.y || 0).toFixed(1) + "    滚轮 " + Number(panel.stats.wheel || 0).toFixed(1)
                color: "#62666e"
                font.pixelSize: 12 * panel.uiScale
                wrapMode: Text.WordWrap
            }
            Text {
                Layout.fillWidth: true
                text: panel.stats.status + " · " + panel.stats.backend + "\n观察常用键、组合键松开、鼠标移动/滚轮与按钮响应后再确认；无需逐一测完全部键。"
                wrapMode: Text.WordWrap
                font.pixelSize: 12 * panel.uiScale
                color: "#737780"
            }
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: 1
                color: "#e0e3e5"
            }
            Text {
                text: "控制输出设置"
                font.pixelSize: 16 * panel.uiScale
                font.weight: Font.DemiBold
                color: "#24272b"
            }
            RowLayout {
                Layout.fillWidth: true
                Text {
                    text: "灵敏度 " + panel.sensitivity.toFixed(2) + "×"
                    color: "#24272b"
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
            }
            RowLayout {
                Text {
                    text: "MQTT 发布频率"
                    color: "#24272b"
                }
                ComboBox {
                    objectName: "inputFrequency"
                    model: ["200 Hz", "75 Hz"]
                    currentIndex: panel.frequency === 200 ? 0 : 1
                    onActivated: panel.frequency = currentIndex === 0 ? 200 : 75
                }
            }
            Text {
                Layout.fillWidth: true
                text: "200 Hz 每 5 ms 发送；75 Hz 为协议标注频率。200 Hz 能缩短客户端等待，裁判端是否接纳以及实际总延迟须实测。鼠标按每个发布周期的相对增量发送，灵敏度仅作用于控制输出。"
                wrapMode: Text.WordWrap
                font.pixelSize: 12 * panel.uiScale
                color: "#737780"
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
    }
    RowLayout {
        id: actions
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        spacing: 10 * panel.uiScale
        Switch {
            objectName: "inputEnabled"
            text: "控制输出"
            checked: panel.input.configuration.enabled
            Accessible.name: "键鼠控制输出总开关"
            ToolTip.visible: hovered
            ToolTip.text: "开启后，返回主画面即持续采集；MQTT 就绪后发送。测试页面不发送控制。"
            onToggled: {
                panel.input.setEnabled(checked);
                checked = Qt.binding(() => panel.input.configuration.enabled);
            }
        }
        VideoButton {
            objectName: "inputApply"
            uiScale: panel.uiScale
            text: "保存设置"
            onClicked: panel.input.configure(panel.sensitivity, panel.invertY, panel.frequency)
        }
        Item {
            Layout.fillWidth: true
        }
        VideoButton {
            objectName: "inputReset"
            uiScale: panel.uiScale
            quiet: true
            text: "重新检查"
            onClicked: {
                panel.keyboardConfirmed = false;
                panel.mouseConfirmed = false;
                panel.input.resetStatistics();
            }
        }
        VideoButton {
            objectName: "inputLockTest"
            uiScale: panel.uiScale
            primary: true
            text: panel.stats.testLocked ? "停止测试" : "开始测试"
            onClicked: panel.input.setTestLocked(!panel.stats.testLocked)
        }
    }
}
