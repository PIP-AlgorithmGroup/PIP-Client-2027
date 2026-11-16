import QtQuick
import QtQuick.Controls
import QtQuick.Window
import PIPVideo 1.0
import "components"
import "settings"
import "welcome"
import "video"

PresentationWindow {
    id: root

    // 注入对象的动态属性契约由 C++ 头文件定义，交互测试验证；QML 不接管所有权。
    required property var dataPipeline
    required property var appController
    required property var videoReceiver
    required property var matchSession
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
    // 独立窗口保留界面字体值；控件默认字体仍来自应用的打包字体配置。
    property font font: Qt.font({ pixelSize: 16 * uiScale })
    videoSource: root.videoReceiver
    Component.onCompleted: {
        applyFullScreenPreference();
        // 启动偏好在建窗前已读取；首次 ready 信号早于 Connections，需在组件就绪后恢复键盘入口。
        if (appController.startupState === "ready" && appController.currentPage === "welcome")
            welcome.focusModes();
    }

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
        enabled: visible && !tools.opened && !clientMenu.opened
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
            id: videoView
            objectName: "rmulVideo"
            anchors.fill: parent
            visible: root.isRmul
            receiver: root.videoReceiver
        }
        VideoPlaceholder {
            objectName: "videoPlaceholder"
            anchors.fill: parent
            visible: root.isRmul && videoView.contentRect.width <= 0
            receiver: root.videoReceiver
            uiScale: root.uiScale
            onMenuRequested: clientMenu.open()
        }
        // 常驻业务挂载点使用窗口逻辑坐标，不依赖视频矩形、帧状态或占位显隐。
        // 后续展示模型独立保存最后有效值和新鲜度；此空层不持有协议缓存或控制许可。
        Item {
            objectName: "businessOverlay"
            anchors.fill: parent
            visible: root.isRmul
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
    ClientMenu {
        id: clientMenu
        objectName: "clientMenu"
        parent: Overlay.overlay
        controller: root.appController
        receiver: root.videoReceiver
        view: videoView
        session: root.matchSession
        pipeline: root.dataPipeline
        uiScale: root.uiScale
        graphicsBackend: root.graphicsBackend
        connectionLabel: root.connectionLabel
        onClosed: {
            if (root.isRmul)
                arena.forceActiveFocus(Qt.TabFocusReason);
            else
                backButton.forceActiveFocus(Qt.TabFocusReason);
        }
        onWelcomeRequested: root.appController.returnToWelcome()
    }
    VideoDebug {
        objectName: "videoDebugOverlay"
        parent: Overlay.overlay
        // 菜单上方仍可观察统计；不接收点击，不阻挡底层业务交互。
        z: 100
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.margins: 10 * root.uiScale
        visible: root.isRmul
        receiver: root.videoReceiver
        view: videoView
        uiScale: root.uiScale
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
        enabled: !tools.opened && !clientMenu.opened && (root.appController.currentPage === "arena" || root.appController.fullScreen)
        onActivated: {
            if (root.appController.currentPage === "arena")
                clientMenu.open();
            else
                root.appController.setFullScreen(false);
        }
    }
    // 仅关闭页面关联菜单；后台资源由 C++ MatchSession 响应导航信号统一管理。
    onIsRmulChanged: {
        if (!isRmul) {
            clientMenu.close();
        }
    }
    Connections {
        target: root.appController
        function onFullScreenChanged() {
            root.applyFullScreenPreference();
        }
        function onStateChanged() {
            if (root.appController.currentPage !== "arena")
                clientMenu.close();
            if (root.appController.startupState === "ready" && root.appController.currentPage === "welcome")
                welcome.focusModes();
        }
    }
}
