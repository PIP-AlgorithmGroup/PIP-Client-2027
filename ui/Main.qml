import QtQuick
import QtQuick.Controls
import QtQuick.Window
import PIPVideo 1.0

ApplicationWindow {
    id: root

    // 注入对象的动态属性契约由 C++ 头文件定义，交互测试验证；QML 不接管所有权。
    required property var dataPipeline
    required property var appController
    required property var videoReceiver
    readonly property bool isRmul: appController.currentPage === "arena" && appController.selectedMode === "RMUL"
    readonly property real uiScale: appController.uiScale
    // 全屏只响应明确的偏好改变；保留进入前最大化/窗口状态及原生正常窗口几何。
    property int previousWindowVisibility: Window.Windowed
    // 测试可注入丢失路径以验证回退，产品始终使用打包的离线资源。
    property alias backgroundSource: scene.backgroundSource
    property alias posterSource: scene.posterSource
    readonly property string backgroundState: scene.imageState
    readonly property string connectionLabel: {
        switch (dataPipeline.connectionState) {
        case "ready":
            return "已连接";
        case "connecting":
            return "正在连接";
        case "subscribing":
            return "正在订阅";
        case "reconnecting":
            return "正在重连";
        default:
            return "尚未连接";
        }
    }
    readonly property string graphicsBackend: {
        switch (scene.GraphicsInfo.api) {
        case GraphicsInfo.Software:
            return "Software";
        case GraphicsInfo.OpenGL:
            return "OpenGL";
        case GraphicsInfo.Direct3D11:
            return "Direct3D 11";
        case GraphicsInfo.Vulkan:
            return "Vulkan";
        case GraphicsInfo.Metal:
            return "Metal";
        default:
            return "尚未取得";
        }
    }
    // 设置面板也允许预览；暂停仅由窗口状态、页面和图片就绪状态决定。
    readonly property bool backgroundMotionRunning: appController.motionEnabled && active && visible && visibility !== Window.Minimized && appController.currentPage === "welcome" && backgroundState === "ready"

    visible: true
    width: 1280
    height: 720
    minimumWidth: 1024
    minimumHeight: 640
    title: "PIP CLIENT"
    color: "#76b8d2"
    font.pixelSize: 16 * uiScale
    Component.onCompleted: applyFullScreenPreference()

    function applyFullScreenPreference() {
        if (appController.fullScreen) {
            if (visibility !== Window.FullScreen) {
                previousWindowVisibility = visibility === Window.Maximized ? Window.Maximized : Window.Windowed;
                showFullScreen();
            }
        } else if (visibility === Window.FullScreen) {
            if (previousWindowVisibility === Window.Maximized)
                showMaximized();
            else
                showNormal();
        }
    }

    FontLoader {
        id: titleBold
        source: "qrc:/resources/fonts/barlow_bold.ttf"
    }
    FontLoader {
        id: titleMedium
        source: "qrc:/resources/fonts/barlow_medium.ttf"
    }
    SceneBackdrop {
        id: scene
        anchors.fill: parent
        visible: !root.isRmul
        motionAllowed: root.backgroundMotionRunning
    }
    WelcomePage {
        id: welcome
        objectName: "welcomePage"
        anchors.fill: parent
        visible: root.appController.currentPage === "welcome"
        enabled: visible && !tools.opened
        controller: root.appController
        uiScale: root.uiScale
        displayFont: titleBold.status === FontLoader.Ready ? titleBold.name : root.font.family
        mediumFont: titleMedium.status === FontLoader.Ready ? titleMedium.name : root.font.family
        connectionLabel: root.connectionLabel
        backgroundFailed: scene.backgroundFailed
        onPanelRequested: function (panel) {
            root.openPanel(panel);
        }
    }
    FocusScope {
        id: arena
        objectName: "arenaPage"
        anchors.fill: parent
        visible: root.appController.currentPage === "arena"
        enabled: visible && !tools.opened && !videoSettings.opened
        onVisibleChanged: {
            if (visible) {
                if (root.isRmul)
                    arena.forceActiveFocus(Qt.TabFocusReason);
                else
                    backButton.forceActiveFocus(Qt.TabFocusReason);
            }
        }
        Rectangle {
            anchors.fill: parent
            color: root.isRmul ? "black" : "#d010293e"
        }
        VideoView {
            objectName: "rmulVideo"
            anchors.fill: parent
            visible: root.isRmul
            receiver: root.videoReceiver
        }
        Column {
            visible: !root.isRmul
            anchors.centerIn: parent
            width: Math.min(600 * root.uiScale, parent.width - 80)
            spacing: 24 * root.uiScale
            Text {
                width: parent.width
                text: root.appController.selectedMode + "\n" + (root.appController.selectedMode === "RMUL" ? "机甲大师高校联盟赛" : "超级对抗赛")
                font.pixelSize: 32 * root.uiScale
                font.bold: true
                color: "white"
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
            }
            Text {
                width: parent.width
                text: "已选择赛事模式\n比赛工作台等待后续设计"
                font.pixelSize: 18 * root.uiScale
                color: "#dcecf5"
                horizontalAlignment: Text.AlignHCenter
            }
            BasicButton {
                id: backButton
                uiScale: root.uiScale
                objectName: "backToWelcomeButton"
                anchors.horizontalCenter: parent.horizontalCenter
                text: "返回欢迎页"
                width: 220 * root.uiScale
                primary: true
                onClicked: {
                    root.appController.returnToWelcome();
                    welcome.focusModes();
                }
            }
            BasicButton {
                uiScale: root.uiScale
                anchors.horizontalCenter: parent.horizontalCenter
                text: "设置"
                onClicked: root.openPanel("设置")
            }
        }
    }
    VideoSettings {
        id: videoSettings
        objectName: "videoSettings"
        parent: Overlay.overlay
        receiver: root.videoReceiver
        uiScale: root.uiScale
        onClosed: arena.forceActiveFocus(Qt.TabFocusReason)
        onWelcomeRequested: root.appController.returnToWelcome()
    }
    AppPanel {
        id: tools
        objectName: "toolsPanel"
        parent: Overlay.overlay
        controller: root.appController
        uiScale: root.uiScale
        connectionLabel: root.connectionLabel
        graphicsBackend: root.graphicsBackend
        backgroundState: root.backgroundState
        onClosed: {
            if (root.appController.currentPage === "welcome")
                welcome.focusModes();
            else if (root.isRmul)
                arena.forceActiveFocus(Qt.TabFocusReason);
            else
                backButton.forceActiveFocus(Qt.TabFocusReason);
        }
    }

    // 只暴露本地工具入口，便于键盘导航和自动化验证；未知入口不打开空面板。
    function openPanel(section) {
        if (["设置", "诊断", "帮助", "关于"].indexOf(section) < 0)
            return;
        tools.section = section;
        tools.open();
    }
    Shortcut {
        sequence: "F11"
        onActivated: root.appController.setFullScreen(!root.appController.fullScreen)
    }
    Shortcut {
        sequence: "Escape"
        enabled: !tools.opened && !videoSettings.opened && (root.isRmul || root.appController.fullScreen)
        onActivated: {
            if (root.isRmul)
                videoSettings.open();
            else
                root.appController.setFullScreen(false);
        }
    }
    // 页面拥有监听会话；离开 RMUL 后立即释放端口，重入则恢复已保存配置。
    onIsRmulChanged: {
        if (isRmul)
            videoReceiver.start();
        else {
            videoSettings.close();
            videoReceiver.stop();
        }
    }
    Connections {
        target: root.appController
        function onFullScreenChanged() {
            root.applyFullScreenPreference();
        }
        function onStateChanged() {
            if (root.appController.startupState === "ready" && root.appController.currentPage === "welcome")
                welcome.focusModes();
        }
    }
}
