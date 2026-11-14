pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls

// 仅替代缺失的视频内容；占位显隐不改变同级业务叠层的生命周期。
Rectangle {
    id: placeholder
    // C++ 持有接收器；此处不推断业务数据新鲜度。
    required property var receiver
    // UI 档位倍率，尺寸为逻辑像素，不随视频分辨率缩放。
    required property real uiScale
    // 请求宿主菜单，不直接修改接收配置。
    signal menuRequested
    readonly property bool hasError: receiver.error !== ""
    color: "#f1f2f3"
    Column {
        anchors.centerIn: parent
        width: Math.min(520 * placeholder.uiScale, parent.width - 48)
        spacing: 16 * placeholder.uiScale
        Image {
            anchors.horizontalCenter: parent.horizontalCenter
            width: 58 * placeholder.uiScale
            height: width
            source: "qrc:/resources/icons/video_off.svg"
            sourceSize: Qt.size(Math.ceil(width * Screen.devicePixelRatio), Math.ceil(height * Screen.devicePixelRatio))
        }
        Text {
            objectName: "videoEmptyTitle"
            width: parent.width
            text: placeholder.hasError ? "图传暂不可用" : !placeholder.receiver.running ? "图传已暂停" : placeholder.receiver.status === "等待关键帧" ? "等待关键帧" : placeholder.receiver.status === "图传中断，等待画面" ? "图传信号中断" : "等待图传画面"
            color: "#24272b"
            font.pixelSize: 24 * placeholder.uiScale
            font.weight: Font.Medium
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
        }
        Text {
            width: parent.width
            text: placeholder.hasError ? "请在菜单中检查图传配置与运行诊断" : !placeholder.receiver.running ? "可在菜单的图传设置中恢复接收" : "画面接入后自动显示"
            color: "#62666e"
            font.pixelSize: 13 * placeholder.uiScale
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
        }
        Button {
            id: menuHint
            objectName: "videoEmptySettingsButton"
            anchors.horizontalCenter: parent.horizontalCenter
            width: 154 * placeholder.uiScale
            height: 36 * placeholder.uiScale
            text: "打开菜单"
            Accessible.name: "打开客户端菜单，快捷键 Esc"
            hoverEnabled: true
            onClicked: placeholder.menuRequested()
            background: Rectangle {
                radius: 3 * placeholder.uiScale
                color: menuHint.hovered ? "#e5e6e8" : "transparent"
                border.color: menuHint.activeFocus ? "#c94b17" : "transparent"
            }
            contentItem: Item {
                Row {
                    anchors.centerIn: parent
                    spacing: 10 * placeholder.uiScale
                    Rectangle {
                        width: 36 * placeholder.uiScale
                        height: 23 * placeholder.uiScale
                        anchors.verticalCenter: parent.verticalCenter
                        radius: 3 * placeholder.uiScale
                        color: "#fafafa"
                        border.color: "#c9cdd2"
                        Text {
                            anchors.centerIn: parent
                            text: "Esc"
                            font.pixelSize: 11 * placeholder.uiScale
                            color: "#62666e"
                        }
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: menuHint.text
                        color: "#62666e"
                        font.pixelSize: 12 * placeholder.uiScale
                    }
                }
            }
        }
    }
}
