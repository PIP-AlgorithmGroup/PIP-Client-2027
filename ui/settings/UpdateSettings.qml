pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 欢迎页关于与比赛设置共享 C++ 服务；QML 不访问网络、不比较版本、不启动安装进程。
ColumnLayout {
    id: panel
    required property var updater
    required property string currentVersion
    required property real uiScale
    property string namePrefix: ""
    spacing: 12 * uiScale

    Text {
        Layout.fillWidth: true
        text: "软件更新 · 当前 v" + panel.currentVersion
        color: "#24272b"
        font.pixelSize: 17 * panel.uiScale
        font.weight: Font.DemiBold
        wrapMode: Text.WordWrap
    }
    CheckBox {
        objectName: panel.namePrefix + "updatePrerelease"
        text: "包含预发布版本（alpha / beta / rc）"
        checked: panel.updater.includePrerelease
        enabled: !panel.updater.busy
        font.pixelSize: 13 * panel.uiScale
        onToggled: panel.updater.includePrerelease = checked
    }
    Text {
        objectName: "updateStatus"
        Layout.fillWidth: true
        text: panel.updater.status
        textFormat: Text.PlainText
        wrapMode: Text.WordWrap
        color: panel.updater.state === "error" ? "#a83e22" : "#626972"
        font.pixelSize: 13 * panel.uiScale
        Accessible.role: Accessible.StaticText
        Accessible.name: text
    }
    ProgressBar {
        objectName: "updateProgress"
        Layout.fillWidth: true
        visible: panel.updater.busy
        indeterminate: panel.updater.state !== "downloading"
        value: panel.updater.progress
        Accessible.name: "更新下载进度"
    }
    Flow {
        Layout.fillWidth: true
        spacing: 8 * panel.uiScale
        Button {
            objectName: panel.namePrefix + "checkUpdates"
            text: "检查更新"
            enabled: !panel.updater.busy
            onClicked: panel.updater.check()
        }
        Button {
            objectName: "downloadUpdate"
            text: "下载更新"
            visible: panel.updater.availableVersion !== "" && panel.updater.state !== "ready"
            enabled: panel.updater.canDownload
            onClicked: panel.updater.download()
        }
        Button {
            objectName: "cancelUpdate"
            text: "取消"
            visible: panel.updater.busy
            onClicked: panel.updater.cancel()
        }
        Button {
            objectName: "installUpdate"
            text: "退出并安装更新"
            visible: panel.updater.state === "ready"
            enabled: panel.updater.canInstall
            onClicked: installConfirmation.open()
        }
        Button {
            objectName: "openUpdateDownload"
            text: "打开下载位置"
            visible: panel.updater.state === "ready"
            onClicked: panel.updater.openDownloadDirectory()
        }
        Button {
            objectName: "openUpdateRelease"
            text: "发行页面 ↗"
            onClicked: panel.updater.openReleasePage()
        }
    }
    Text {
        Layout.fillWidth: true
        visible: panel.updater.state === "ready"
        text: panel.updater.installHint
        wrapMode: Text.WordWrap
        font.pixelSize: 12 * panel.uiScale
        color: "#737982"
    }
    TextArea {
        objectName: "updateReleaseNotes"
        Layout.fillWidth: true
        visible: panel.updater.releaseNotes !== ""
        text: panel.updater.releaseNotes
        textFormat: TextEdit.PlainText
        readOnly: true
        selectByMouse: true
        wrapMode: TextEdit.Wrap
        font.pixelSize: 12 * panel.uiScale
        Accessible.name: "新版发行说明"
    }
    Item {
        implicitHeight: 0
        implicitWidth: 0
        Dialog {
            id: installConfirmation
            objectName: panel.namePrefix + "updateInstallConfirmation"
            parent: Overlay.overlay
            anchors.centerIn: parent
            width: Math.min(440 * panel.uiScale, parent.width - 48)
            height: Math.min(240 * panel.uiScale, parent.height - 48)
            modal: true
            title: "安装更新"
            standardButtons: Dialog.Ok | Dialog.Cancel
            onAccepted: panel.updater.install()
            contentItem: Label {
                text: "将退出客户端并安装 v" + panel.updater.availableVersion + "，完成后重新启动。已有设置与日志保留。"
                wrapMode: Text.WordWrap
                font.pixelSize: 14 * panel.uiScale
            }
        }
    }
}
