pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"

// 只展示 GUI 合并快照；测试采集在原生线程，测试页始终禁止机器人控制。
ScrollView {
    id: panel
    required property var input
    required property real uiScale
    property real sensitivity: input.configuration.sensitivity
    property bool invertY: input.configuration.invertY
    property int frequency: input.configuration.frequency
    readonly property var stats: input.diagnostics
    readonly property var pressed: stats.keys || []
    clip: true
    contentWidth: availableWidth
    function metric(rate, key, decimals) {
        const value = (stats[rate] || {})[key];
        return typeof value === "number" && isFinite(value) ? value.toFixed(decimals) : "0";
    }
    // 菜单重新打开恢复已保存值，分类切换保留草稿；滚动位置每次重开归零。
    function resetDraft() {
        sensitivity = input.configuration.sensitivity;
        invertY = input.configuration.invertY;
        frequency = input.configuration.frequency;
        const flickable = contentItem as Flickable;
        if (flickable)
            flickable.contentY = 0;
    }
    function isDown(code) {
        return pressed.indexOf(code) >= 0;
    }

    ColumnLayout {
        width: panel.availableWidth
        spacing: 14 * panel.uiScale
        Text {
            text: "键鼠设置与实时测试"
            font.pixelSize: 25 * panel.uiScale
            font.bold: true
            color: "#24272b"
        }
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: masterRow.implicitHeight + 20 * panel.uiScale
            color: "#edeef0"
            radius: 5 * panel.uiScale
            RowLayout {
                id: masterRow
                anchors.fill: parent
                anchors.margins: 10 * panel.uiScale
                ColumnLayout {
                    Layout.fillWidth: true
                    Text {
                        text: "键鼠控制输出总开关"
                        font.pixelSize: 15 * panel.uiScale
                        font.bold: true
                        color: "#24272b"
                    }
                    Text {
                        Layout.fillWidth: true
                        text: "开启后主画面持续采集，MQTT 连接后自动发送；离线、无图像也采集。"
                        wrapMode: Text.WordWrap
                        font.pixelSize: 11 * panel.uiScale
                        color: "#62666e"
                    }
                }
                Switch {
                    id: masterSwitch
                    objectName: "inputEnabled"
                    checked: panel.input.configuration.enabled
                    text: checked ? "已开启" : "已关闭"
                    Accessible.name: "键鼠控制输出总开关"
                    // 开关立即持久化，保存失败恢复 C++ 已应用值；不提交其他设置草稿。
                    onToggled: {
                        panel.input.setEnabled(checked);
                        checked = Qt.binding(() => panel.input.configuration.enabled);
                    }
                }
            }
        }
        Text {
            Layout.fillWidth: true
            text: panel.stats.status + "  ·  " + panel.stats.backend + "\n本页仅本地测试，可在 MQTT 离线时使用。不会发送测试按键或鼠标运动。"
            wrapMode: Text.WordWrap
            font.pixelSize: 13 * panel.uiScale
            color: "#62666e"
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
            VideoButton {
                objectName: "inputApply"
                uiScale: panel.uiScale
                text: "保存设置"
                onClicked: panel.input.configure(panel.sensitivity, panel.invertY, panel.frequency)
            }
        }
        Text {
            Layout.fillWidth: true
            text: "200 Hz 每 5 ms 发送；75 Hz 为协议标注频率。200 Hz 能缩短客户端等待，裁判端是否接纳以及实际总延迟须实测。鼠标按每个发布周期的相对增量发送，灵敏度仅作用于控制输出。"
            wrapMode: Text.WordWrap
            font.pixelSize: 12 * panel.uiScale
            color: "#737780"
        }
        RowLayout {
            VideoButton {
                objectName: "inputLockTest"
                uiScale: panel.uiScale
                text: panel.stats.testLocked ? "停止锁定测试" : "开始无限相对移动测试"
                onClicked: panel.input.setTestLocked(!panel.stats.testLocked)
            }
            VideoButton {
                objectName: "inputReset"
                uiScale: panel.uiScale
                quiet: true
                text: "清空统计"
                onClicked: panel.input.resetStatistics()
            }
        }
        Text {
            Layout.fillWidth: true
            text: "锁定测试隐藏并约束光标；按 Esc 或切换窗口释放。请持续移动鼠标观察回报率，按住多个键后同时松开检查键位。"
            wrapMode: Text.WordWrap
            color: "#62666e"
            font.pixelSize: 12 * panel.uiScale
        }
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
                    color: panel.isDown(modelData.c) ? "#d65319" : "#e7e9ec"
                    Text {
                        anchors.centerIn: parent
                        text: mouseButton.modelData.n
                        font.pixelSize: 11 * panel.uiScale
                        color: panel.isDown(mouseButton.modelData.c) ? "white" : "#42464e"
                    }
                }
            }
        }
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
}
