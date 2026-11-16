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
        // 标准物理键位使用 evdev 编号，协议之外的键同样可观察；布局不改变控制映射。
        Repeater {
            model: [[
                    {
                        c: 1,
                        n: "Esc"
                    },
                    {
                        c: 59,
                        n: "F1"
                    },
                    {
                        c: 60,
                        n: "F2"
                    },
                    {
                        c: 61,
                        n: "F3"
                    },
                    {
                        c: 62,
                        n: "F4"
                    },
                    {
                        c: 63,
                        n: "F5"
                    },
                    {
                        c: 64,
                        n: "F6"
                    },
                    {
                        c: 65,
                        n: "F7"
                    },
                    {
                        c: 66,
                        n: "F8"
                    },
                    {
                        c: 67,
                        n: "F9"
                    },
                    {
                        c: 68,
                        n: "F10"
                    },
                    {
                        c: 87,
                        n: "F11"
                    },
                    {
                        c: 88,
                        n: "F12"
                    },
                    {
                        c: 99,
                        n: "PrtSc"
                    },
                    {
                        c: 70,
                        n: "Scroll"
                    },
                    {
                        c: 119,
                        n: "Pause"
                    }
                ], [
                    {
                        c: 41,
                        n: "`"
                    },
                    {
                        c: 2,
                        n: "1"
                    },
                    {
                        c: 3,
                        n: "2"
                    },
                    {
                        c: 4,
                        n: "3"
                    },
                    {
                        c: 5,
                        n: "4"
                    },
                    {
                        c: 6,
                        n: "5"
                    },
                    {
                        c: 7,
                        n: "6"
                    },
                    {
                        c: 8,
                        n: "7"
                    },
                    {
                        c: 9,
                        n: "8"
                    },
                    {
                        c: 10,
                        n: "9"
                    },
                    {
                        c: 11,
                        n: "0"
                    },
                    {
                        c: 12,
                        n: "-"
                    },
                    {
                        c: 13,
                        n: "="
                    },
                    {
                        c: 14,
                        n: "⌫"
                    }
                ], [
                    {
                        c: 15,
                        n: "Tab"
                    },
                    {
                        c: 16,
                        n: "Q"
                    },
                    {
                        c: 17,
                        n: "W"
                    },
                    {
                        c: 18,
                        n: "E"
                    },
                    {
                        c: 19,
                        n: "R"
                    },
                    {
                        c: 20,
                        n: "T"
                    },
                    {
                        c: 21,
                        n: "Y"
                    },
                    {
                        c: 22,
                        n: "U"
                    },
                    {
                        c: 23,
                        n: "I"
                    },
                    {
                        c: 24,
                        n: "O"
                    },
                    {
                        c: 25,
                        n: "P"
                    },
                    {
                        c: 26,
                        n: "["
                    },
                    {
                        c: 27,
                        n: "]"
                    },
                    {
                        c: 43,
                        n: "\\"
                    }
                ], [
                    {
                        c: 58,
                        n: "Caps"
                    },
                    {
                        c: 30,
                        n: "A"
                    },
                    {
                        c: 31,
                        n: "S"
                    },
                    {
                        c: 32,
                        n: "D"
                    },
                    {
                        c: 33,
                        n: "F"
                    },
                    {
                        c: 34,
                        n: "G"
                    },
                    {
                        c: 35,
                        n: "H"
                    },
                    {
                        c: 36,
                        n: "J"
                    },
                    {
                        c: 37,
                        n: "K"
                    },
                    {
                        c: 38,
                        n: "L"
                    },
                    {
                        c: 39,
                        n: ";"
                    },
                    {
                        c: 40,
                        n: "'"
                    },
                    {
                        c: 28,
                        n: "Enter"
                    }
                ], [
                    {
                        c: 42,
                        n: "LShift"
                    },
                    {
                        c: 44,
                        n: "Z"
                    },
                    {
                        c: 45,
                        n: "X"
                    },
                    {
                        c: 46,
                        n: "C"
                    },
                    {
                        c: 47,
                        n: "V"
                    },
                    {
                        c: 48,
                        n: "B"
                    },
                    {
                        c: 49,
                        n: "N"
                    },
                    {
                        c: 50,
                        n: "M"
                    },
                    {
                        c: 51,
                        n: ","
                    },
                    {
                        c: 52,
                        n: "."
                    },
                    {
                        c: 53,
                        n: "/"
                    },
                    {
                        c: 54,
                        n: "RShift"
                    }
                ], [
                    {
                        c: 29,
                        n: "LCtrl"
                    },
                    {
                        c: 125,
                        n: "Win"
                    },
                    {
                        c: 56,
                        n: "LAlt"
                    },
                    {
                        c: 57,
                        n: "Space"
                    },
                    {
                        c: 100,
                        n: "RAlt"
                    },
                    {
                        c: 126,
                        n: "Win"
                    },
                    {
                        c: 127,
                        n: "Menu"
                    },
                    {
                        c: 97,
                        n: "RCtrl"
                    },
                    {
                        c: 103,
                        n: "↑"
                    },
                    {
                        c: 105,
                        n: "←"
                    },
                    {
                        c: 108,
                        n: "↓"
                    },
                    {
                        c: 106,
                        n: "→"
                    }
                ], [
                    {
                        c: 110,
                        n: "Ins"
                    },
                    {
                        c: 111,
                        n: "Del"
                    },
                    {
                        c: 102,
                        n: "Home"
                    },
                    {
                        c: 107,
                        n: "End"
                    },
                    {
                        c: 104,
                        n: "PgUp"
                    },
                    {
                        c: 109,
                        n: "PgDn"
                    },
                    {
                        c: 69,
                        n: "Num"
                    },
                    {
                        c: 98,
                        n: "/"
                    },
                    {
                        c: 55,
                        n: "*"
                    },
                    {
                        c: 74,
                        n: "−"
                    },
                    {
                        c: 78,
                        n: "+"
                    }
                ], [
                    {
                        c: 71,
                        n: "Num7"
                    },
                    {
                        c: 72,
                        n: "Num8"
                    },
                    {
                        c: 73,
                        n: "Num9"
                    },
                    {
                        c: 75,
                        n: "Num4"
                    },
                    {
                        c: 76,
                        n: "Num5"
                    },
                    {
                        c: 77,
                        n: "Num6"
                    },
                    {
                        c: 79,
                        n: "Num1"
                    },
                    {
                        c: 80,
                        n: "Num2"
                    },
                    {
                        c: 81,
                        n: "Num3"
                    },
                    {
                        c: 82,
                        n: "Num0"
                    },
                    {
                        c: 83,
                        n: "Num."
                    },
                    {
                        c: 96,
                        n: "Num↵"
                    }
                ], [
                    {
                        c: 272,
                        n: "鼠标左"
                    },
                    {
                        c: 274,
                        n: "鼠标中"
                    },
                    {
                        c: 273,
                        n: "鼠标右"
                    },
                    {
                        c: 275,
                        n: "侧键 1"
                    },
                    {
                        c: 276,
                        n: "侧键 2"
                    }
                ]]
            RowLayout {
                id: keyboardRow
                required property var modelData
                Layout.fillWidth: true
                spacing: 3 * panel.uiScale
                Repeater {
                    model: keyboardRow.modelData
                    Rectangle {
                        id: keyCap
                        required property var modelData
                        Layout.fillWidth: true
                        implicitHeight: 30 * panel.uiScale
                        color: panel.isDown(modelData.c) ? "#f07842" : "#e4e6e9"
                        radius: 3
                        Text {
                            anchors.centerIn: parent
                            text: keyCap.modelData.n
                            font.pixelSize: 10 * panel.uiScale
                            color: panel.isDown(keyCap.modelData.c) ? "white" : "#42464e"
                        }
                    }
                }
            }
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
            text: "控制位序：W S A D Shift Ctrl Q E R F G Z X C V B；左右 Shift/Ctrl 合并，鼠标左/右/中独立。菜单、其他交互页面、失焦和断线立即撤销控制；恢复时按住的键须释放后重按。"
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
