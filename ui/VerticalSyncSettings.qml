pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 欢迎页与赛场菜单共享同一启动偏好；不重建窗口，也不推断实际显示器同步状态。
ColumnLayout {
    id: syncSettings
    // GUI 线程的 AppController 由宿主持有；开关只保存下一次启动请求。
    required property var controller
    // 控件大小仅跟随用户 UI 档位，窗口尺寸只影响布局和文字换行。
    required property real uiScale
    spacing: 6 * uiScale

    Switch {
        objectName: "verticalSyncSwitch"
        Layout.fillWidth: true
        text: "垂直同步"
        font.pixelSize: 14 * syncSettings.uiScale
        // 两个宿主均为浅色面板；固定基本色，避免暗色系统主题把标签变成浅背景白字。
        palette.windowText: "#262930"
        palette.window: "#f9fafb"
        palette.light: "#ffffff"
        palette.mid: "#9ba0a8"
        palette.midlight: "#c5c9cf"
        palette.dark: "#c6322d"
        palette.highlight: "#c6322d"
        palette.disabled.windowText: "#9ba0a8"
        checked: syncSettings.controller.verticalSync
        enabled: syncSettings.controller.startupState === "ready"
        Accessible.name: "垂直同步"
        Accessible.description: "更改后重启客户端生效。关闭可能降低等待，也可能出现画面撕裂。"
        onClicked: {
            syncSettings.controller.setVerticalSync(checked);
            // 写入失败仍显示已保存值；改变待启动请求不代表当前窗口已切换呈交模式。
            checked = Qt.binding(function () {
                return syncSettings.controller.verticalSync;
            });
        }
    }
    Text {
        Layout.fillWidth: true
        text: "开启可减少画面撕裂；关闭可能降低等待。实际呈现仍受系统与驱动约束。更改后重启客户端生效。"
        color: "#62666e"
        font.pixelSize: 13 * syncSettings.uiScale
        wrapMode: Text.WordWrap
    }
    Text {
        objectName: "verticalSyncStatus"
        Layout.fillWidth: true
        text: syncSettings.controller.verticalSyncStatus
        color: syncSettings.controller.verticalSyncRestartRequired ? "#a33d1b" : "#62666e"
        font.pixelSize: 13 * syncSettings.uiScale
        wrapMode: Text.WordWrap
        Accessible.name: text
    }
}
