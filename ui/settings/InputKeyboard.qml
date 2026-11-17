pragma ComponentBehavior: Bound
import QtQuick

// ANSI 104 物理布局，坐标/宽高单位为 1u；只显示原生状态，不生成任何键盘输入。
Rectangle {
    id: keyboard
    // uiScale 是界面缩放倍数；两个键集合均使用 evdev 物理键码，不受系统文字布局影响。
    // pressedKeys 是当前按住快照，testedKeys 是清空统计以来的本地测试覆盖集合。
    required property real uiScale
    required property var pressedKeys
    required property var testedKeys
    readonly property real unit: (width - 24 * uiScale) / 23
    readonly property real gap: 3 * uiScale
    implicitHeight: 6.5 * unit + 24 * uiScale
    color: "#e7e9ec"
    radius: 8 * uiScale
    border.color: "#d5d8dd"

    // 标准键帽比例与分区间隔来自实体 ANSI 布局；键码沿用跨平台 evdev 编号。
    function layout() {
        const keys = [];
        function key(code, label, x, y, width = 1, height = 1) {
            keys.push({
                code: code,
                label: label,
                x: x,
                y: y,
                width: width,
                height: height
            });
        }
        function row(codes, labels, x, y, widths = []) {
            for (let i = 0; i < codes.length; ++i) {
                const width = widths[i] || 1;
                key(codes[i], labels[i], x, y, width);
                x += width;
            }
        }
        key(1, "Esc", 0, 0);
        row([59, 60, 61, 62], ["F1", "F2", "F3", "F4"], 2, 0);
        row([63, 64, 65, 66], ["F5", "F6", "F7", "F8"], 6.5, 0);
        row([67, 68, 87, 88], ["F9", "F10", "F11", "F12"], 11, 0);
        row([41, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14], ["~\n`", "!\n1", "@\n2", "#\n3", "$\n4", "%\n5", "^\n6", "&\n7", "*\n8", "(\n9", ")\n0", "_\n-", "+\n=", "Backspace"], 0, 1.5, [1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2]);
        row([15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 43], ["Tab", "Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P", "{\n[", "}\n]", "|\n\\"], 0, 2.5, [1.5, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1.5]);
        row([58, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 28], ["Caps Lock", "A", "S", "D", "F", "G", "H", "J", "K", "L", ":\n;", "\"\n'", "Enter"], 0, 3.5, [1.75, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2.25]);
        row([42, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54], ["Shift", "Z", "X", "C", "V", "B", "N", "M", "<\n,", ">\n.", "?\n/", "Shift"], 0, 4.5, [2.25, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2.75]);
        row([29, 125, 56, 57, 100, 126, 127, 97], ["Ctrl", "Win", "Alt", "Space", "Alt", "Win", "Menu", "Ctrl"], 0, 5.5, [1.25, 1.25, 1.25, 6.25, 1.25, 1.25, 1.25, 1.25]);
        row([99, 70, 119], ["Print\nScreen", "Scroll\nLock", "Pause"], 15.5, 0);
        row([110, 102, 104], ["Insert", "Home", "Page\nUp"], 15.5, 1.5);
        row([111, 107, 109], ["Delete", "End", "Page\nDown"], 15.5, 2.5);
        key(103, "↑", 16.5, 4.5);
        row([105, 108, 106], ["←", "↓", "→"], 15.5, 5.5);
        row([69, 98, 55, 74], ["Num\nLock", "/", "*", "−"], 19, 1.5);
        row([71, 72, 73], ["7", "8", "9"], 19, 2.5);
        key(78, "+", 22, 2.5, 1, 2);
        row([75, 76, 77], ["4", "5", "6"], 19, 3.5);
        row([79, 80, 81], ["1", "2", "3"], 19, 4.5);
        key(96, "Enter", 22, 4.5, 1, 2);
        key(82, "0", 19, 5.5, 2);
        key(83, ".", 21, 5.5);
        return keys;
    }
    Repeater {
        model: keyboard.layout()
        Rectangle {
            id: keyCap
            required property var modelData
            readonly property bool isPressed: keyboard.pressedKeys.indexOf(modelData.code) >= 0
            readonly property bool wasTested: keyboard.testedKeys.indexOf(modelData.code) >= 0
            objectName: "inputKey_" + modelData.code
            x: 12 * keyboard.uiScale + modelData.x * keyboard.unit
            y: 12 * keyboard.uiScale + modelData.y * keyboard.unit
            width: modelData.width * keyboard.unit - keyboard.gap
            height: modelData.height * keyboard.unit - keyboard.gap
            radius: 4 * keyboard.uiScale
            color: isPressed ? "#d65319" : wasTested ? "#e0eee5" : "#fafbfc"
            border.color: isPressed ? "#b33c08" : wasTested ? "#82ad91" : "#c1c6ce"
            Accessible.role: Accessible.Indicator
            Accessible.name: modelData.label.replace("\n", " ") + (isPressed ? " 按下" : wasTested ? " 已测" : " 未测")
            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.margins: 2 * keyboard.uiScale
                height: keyboard.uiScale
                color: keyCap.isPressed ? "#f08957" : "#d9dde2"
            }
            Text {
                objectName: "inputKeyLabel"
                anchors.fill: parent
                anchors.margins: 3 * keyboard.uiScale
                text: keyCap.modelData.label
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                // 按最长文本行及实际键帽边界缩小字号，保留多行与完整键名，不能裁切或省略。
                fontSizeMode: Text.Fit
                minimumPixelSize: 1
                font.pixelSize: keyCap.modelData.label.length === 1 ? Math.min(14 * keyboard.uiScale, keyboard.unit * 0.4) : Math.min(11 * keyboard.uiScale, keyboard.unit * 0.3)
                font.weight: keyCap.isPressed ? Font.DemiBold : Font.Normal
                color: keyCap.isPressed ? "#ffffff" : "#39414c"
            }
        }
    }
}
