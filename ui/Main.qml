import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import PIPVideo 1.0
import "components"
import "settings"
import "welcome"
import "video"
import "rmul"

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
    property font font: Qt.font({
        pixelSize: 16 * uiScale
    })
    videoSource: root.videoReceiver
    Component.onCompleted: {
        matchSession.inputController.bindWindow(root);
        updateInputContext();
        applyFullScreenPreference();
        // 启动偏好在建窗前已读取；首次 ready 信号早于 Connections，需在组件就绪后恢复键盘入口。
        if (appController.startupState === "ready" && appController.currentPage === "welcome")
            welcome.focusModes();
    }

    // 页面层集中授予资格；新增业务子页必须设置 interactionBlocked，打开弹窗前立即撤销。
    property bool interactionBlocked: false
    readonly property bool inputBlocked: clientMenu.visible || tools.visible || returnWarning.visible || interactionBlocked
    readonly property bool inputTestPage: clientMenu.visible && clientMenu.section === "input" && !returnWarning.visible
    onInputBlockedChanged: updateInputContext()
    onInputTestPageChanged: updateInputContext()
    function updateInputContext() {
        matchSession.inputController.setContext(isRmul, inputBlocked, inputTestPage);
        // 所有设置/交互页面只允许本地串口自检；返回主画面后仅转发新收到的完整帧。
        matchSession.serialController.setForwardingBlocked(inputBlocked);
    }

    // 所有用户返回入口共用保护；取消时不关闭设置、不停止链路或清除草稿。
    function requestWelcome() {
        clientMenu.rememberPassedChecks();
        if (clientMenu.hasPassedCheck)
            returnWarning.open();
        else
            appController.returnToWelcome();
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
        enabled: visible && !tools.opened && !clientMenu.opened && !returnWarning.opened
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
            // 从菜单预览重新挂回时会改变兄弟顺序；显式层级保证视频始终位于HUD下方。
            z: 0
            // 同一窗口内移动现有画面；关闭设置立即恢复比赛层，不重建接收或解码链路。
            parent: clientMenu.visible && clientMenu.section === "video" && clientMenu.videoPreviewTarget.visible ? clientMenu.videoPreviewTarget : arena
            anchors.fill: parent
            visible: root.isRmul
            receiver: root.videoReceiver
        }
        VideoPlaceholder {
            objectName: "videoPlaceholder"
            z: 1
            anchors.fill: parent
            visible: root.isRmul && videoView.contentRect.width <= 0
            receiver: root.videoReceiver
            uiScale: root.uiScale
            onMenuRequested: clientMenu.open()
        }
        // 常驻业务挂载点使用窗口逻辑坐标，不依赖视频矩形、帧状态或占位显隐。
        // 当前仅绘制无数据前端，不持有协议缓存或控制许可，也不根据视频推断遥测状态。
        Item {
            objectName: "businessOverlay"
            z: 2
            anchors.fill: parent
            visible: root.isRmul
            RmulHud {
                objectName: "rmulHud"
                anchors.fill: parent
                visible: !clientMenu.visible && !tools.visible && !returnWarning.visible
                robotId: root.matchSession.configuration.robotId
                numberFont: titleBold.status === FontLoader.Ready ? titleBold.name : root.font.family
            }
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
                    root.requestWelcome();
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
        onWelcomeRequested: root.requestWelcome()
    }
    Popup {
        id: returnWarning
        objectName: "returnWelcomeDialog"
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(460 * root.uiScale, parent.width - 48)
        implicitHeight: contentItem.implicitHeight + topPadding + bottomPadding
        padding: 24 * root.uiScale
        modal: true
        focus: true
        closePolicy: Popup.CloseOnEscape
        onOpened: cancelReturn.forceActiveFocus(Qt.TabFocusReason)
        onClosed: {
            if (clientMenu.opened)
                clientMenu.forceActiveFocus(Qt.TabFocusReason);
            else if (root.appController.currentPage === "arena")
                backButton.forceActiveFocus(Qt.TabFocusReason);
        }
        background: Rectangle {
            color: "white"
            radius: 6 * root.uiScale
            border.color: "#dce0e3"
        }
        Overlay.modal: Rectangle {
            color: "#6624272b"
        }
        contentItem: ColumnLayout {
            spacing: 18 * root.uiScale
            Text {
                Layout.fillWidth: true
                text: "确认返回欢迎页？"
                color: "#23272c"
                font.pixelSize: 21 * root.uiScale
                font.weight: Font.DemiBold
                wrapMode: Text.WordWrap
            }
            Text {
                Layout.fillWidth: true
                text: "已有自检项目通过。返回后将结束当前比赛准备并清除自检进度，再次进入需要重新配置并自检。"
                color: "#626972"
                font.pixelSize: 13 * root.uiScale
                wrapMode: Text.WordWrap
                lineHeight: 1.6
            }
            RowLayout {
                Layout.fillWidth: true
                Item {
                    Layout.fillWidth: true
                }
                VideoButton {
                    id: cancelReturn
                    objectName: "returnWelcomeCancel"
                    uiScale: root.uiScale
                    text: "继续准备"
                    onClicked: returnWarning.close()
                }
                VideoButton {
                    objectName: "returnWelcomeConfirm"
                    uiScale: root.uiScale
                    primary: true
                    text: "确认返回"
                    onClicked: {
                        returnWarning.close();
                        root.appController.returnToWelcome();
                    }
                }
            }
        }
    }
    VideoDebug {
        id: videoDebug
        objectName: "videoDebugOverlay"
        visible: root.isRmul && !clientMenu.visible
        parent: Overlay.overlay
        // 设置工作区有独立诊断入口，比赛 HUD 只在主画面显示，避免遮挡导航和动作。
        z: 100
        // 诊断独立覆盖在右上方，不为其挪动或压缩业务HUD。
        // 帧率在左、键鼠状态在右，上边缘对齐。共同Overlay坐标避免延迟挂载时跨父锚点告警。
        x: inputStatus.x - width - 10 * root.uiScale
        y: inputStatus.y
        width: Math.min(implicitWidth, Math.max(1, parent.width - inputStatus.width - 30 * root.uiScale))
        receiver: root.videoReceiver
        view: videoView
        uiScale: root.uiScale
    }
    Rectangle {
        id: inputStatus
        objectName: "inputHud"
        parent: Overlay.overlay
        anchors.right: parent.right
        y: 10 * root.uiScale
        anchors.margins: 10 * root.uiScale
        visible: root.isRmul && !clientMenu.visible
        z: 90
        // 给左侧帧率保留实际文本宽度；性能对照可移除帧率组件，键鼠状态仍固定在右上。
        width: Math.min(Math.max(1, parent.width - (videoDebug ? videoDebug.implicitWidth : 0) - 30 * root.uiScale), inputHudText.implicitWidth + 20 * root.uiScale)
        height: inputHudText.implicitHeight + 16 * root.uiScale
        color: "#aa171a20"
        radius: 4
        Text {
            id: inputHudText
            objectName: "inputHudText"
            anchors.centerIn: parent
            width: parent.width - 20 * root.uiScale
            wrapMode: Text.Wrap
            readonly property var input: root.matchSession.inputController
            readonly property var stats: input.diagnostics
            text: stats.status + "  ·  " + input.configuration.frequency + " Hz  ·  TX " + Math.round((stats.send || {}).hz || 0) + " Hz\n按键  " + ((stats.pressedKeys || []).join(" + ") || "无") + "   |   鼠标  " + ((stats.pressedMouse || []).join(" + ") || "无") + "  ·  Esc 菜单"
            color: input.captured ? "#a8efba" : "#e4e6e9"
            font.pixelSize: 12 * root.uiScale
        }
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
        enabled: !tools.opened && !clientMenu.opened && !returnWarning.opened && (root.appController.currentPage === "arena" || root.appController.fullScreen)
        onActivated: {
            if (root.appController.currentPage === "arena")
                clientMenu.open();
            else
                root.appController.setFullScreen(false);
        }
    }
    // 仅关闭页面关联菜单；后台资源由 C++ MatchSession 响应导航信号统一管理。
    onIsRmulChanged: {
        updateInputContext();
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
            if (root.appController.currentPage !== "arena") {
                clientMenu.close();
                returnWarning.close();
            }
            if (root.appController.startupState === "ready" && root.appController.currentPage === "welcome")
                welcome.focusModes();
        }
    }
}
