pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// RMUL 唯一面板：草稿仅在“保存并接收”时提交，关闭不会修改当前会话。
Popup {
    id: panel
    required property var receiver
    required property real uiScale
    signal welcomeRequested
    width: Math.min(480 * uiScale, parent.width - 48)
    height: Math.min(510 * uiScale, parent.height - 48)
    anchors.centerIn: parent
    modal: true
    focus: true
    padding: 24 * uiScale
    font.pixelSize: 16 * uiScale
    closePolicy: Popup.CloseOnEscape
    onOpened: {
        address.text = receiver.bindAddress;
        port.text = String(receiver.port);
        indexBase.currentIndex = receiver.indexBase;
        address.forceActiveFocus(Qt.TabFocusReason);
    }
    background: Rectangle {
        color: "#f4f8fc"
        radius: 12
        border.color: "#cbdce7"
    }
    Overlay.modal: Rectangle {
        color: "#66000000"
    }
    contentItem: ColumnLayout {
        spacing: 14 * panel.uiScale
        RowLayout {
            Layout.fillWidth: true
            Label {
                text: "图传设置"
                color: "#183a4e"
                font.pixelSize: 24 * panel.uiScale
                font.bold: true
            }
            Item {
                Layout.fillWidth: true
            }
            Button {
                text: "关闭"
                onClicked: panel.close()
            }
        }
        Label {
            text: "本机监听地址"
            color: "#183a4e"
        }
        TextField {
            id: address
            objectName: "videoBindAddress"
            Layout.fillWidth: true
            selectByMouse: true
            Accessible.name: "本机 UDP 监听地址"
            placeholderText: "0.0.0.0"
        }
        RowLayout {
            Layout.fillWidth: true
            ColumnLayout {
                Layout.fillWidth: true
                Label {
                    text: "UDP 端口"
                    color: "#183a4e"
                }
                TextField {
                    id: port
                    objectName: "videoPort"
                    Layout.fillWidth: true
                    selectByMouse: true
                    Accessible.name: "UDP 监听端口"
                    validator: IntValidator {
                        bottom: 1
                        top: 65535
                    }
                }
            }
            ColumnLayout {
                Layout.fillWidth: true
                Label {
                    text: "分片起始序号"
                    color: "#183a4e"
                }
                ComboBox {
                    id: indexBase
                    objectName: "videoIndexBase"
                    Layout.fillWidth: true
                    model: ["0", "1"]
                    Accessible.name: "发送端分片起始序号"
                }
            }
        }
        Label {
            text: panel.receiver.status
            color: "#526b7d"
        }
        Label {
            Layout.fillWidth: true
            text: panel.receiver.error
            color: "#9b3c17"
            wrapMode: Text.WordWrap
            visible: text !== ""
        }
        Item {
            Layout.fillHeight: true
        }
        RowLayout {
            Layout.fillWidth: true
            Button {
                objectName: "videoApplyButton"
                text: "保存并接收"
                enabled: address.text.trim() !== "" && port.acceptableInput
                onClicked: {
                    if (panel.receiver.configure(address.text, Number(port.text), indexBase.currentIndex))
                        panel.receiver.start();
                }
            }
            Button {
                objectName: "videoStopButton"
                text: "停止接收"
                enabled: panel.receiver.running
                onClicked: panel.receiver.stop()
            }
        }
        Button {
            objectName: "videoReturnButton"
            text: "返回欢迎页"
            onClicked: {
                panel.close();
                panel.welcomeRequested();
            }
        }
    }
}
