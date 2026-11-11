import QtQuick

// 单张纹理背景与静态海报回退；无视频解码、厂商 SDK 或持续 shader。
Item {
    id: scene

    // 可覆盖路径便于故障测试；尺寸上限避免将5760原图全部上传到核显纹理。
    property url backgroundSource: "qrc:/resources/welcome/arena.jpg"
    property url posterSource: "qrc:/resources/welcome/poster.jpg"
    property bool motionAllowed: false
    readonly property bool backgroundFailed: backdrop.status === Image.Error
    readonly property string imageState: backgroundFailed ? "error" : backdrop.status === Image.Ready ? "ready" : "loading"
    // 分档解码，拖动窗口时避免按每个像素尺寸反复解码；纹理最长边不超过4K。
    readonly property real targetPixels: width * Screen.devicePixelRatio
    readonly property int decodeWidth: targetPixels <= 1920 ? 1920 : targetPixels <= 2560 ? 2560 : 3840

    clip: true
    Rectangle {
        anchors.fill: parent
        color: "#76b8d2"
    }
    Item {
        id: picture
        anchors.fill: parent
        transformOrigin: Item.Center

        Image {
            anchors.fill: parent
            source: scene.posterSource
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
        }
        Image {
            id: backdrop
            objectName: "arenaBackground"
            anchors.fill: parent
            source: scene.backgroundSource
            sourceSize.width: scene.decodeWidth
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
            visible: status === Image.Ready
        }
        // 仅用户启用且窗口活跃时运行；离开欢迎页/最小化/隐藏后停止并复位缩放。
        SequentialAnimation on scale {
            running: scene.motionAllowed && backdrop.status === Image.Ready
            loops: Animation.Infinite
            NumberAnimation {
                from: 1
                to: 1.01
                duration: 8000
                easing.type: Easing.InOutSine
            }
            NumberAnimation {
                from: 1.01
                to: 1
                duration: 8000
                easing.type: Easing.InOutSine
            }
            onRunningChanged: if (!running)
                picture.scale = 1
        }
    }
    // 原生渐变矩形，无模糊通道。仅增强菜单左侧和底部，中央主体保持原亮度。
    Rectangle {
        width: parent.width * 0.51
        height: parent.height
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop {
                position: 0
                color: "#a60a1e30"
            }
            GradientStop {
                position: 0.65
                color: "#400a1e30"
            }
            GradientStop {
                position: 1
                color: "transparent"
            }
        }
    }
    Rectangle {
        width: parent.width
        height: parent.height * 0.38
        anchors.bottom: parent.bottom
        gradient: Gradient {
            GradientStop {
                position: 0
                color: "transparent"
            }
            GradientStop {
                position: 1
                color: "#a60a1e30"
            }
        }
    }
}
