pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"

// RMUL 客户端菜单拥有导航与模态焦点；分区只编辑各自配置，不拥有后台会话。
Popup {
    id: menu
    // GUI 线程注入对象，菜单不延长其生命周期；uiScale 只来自用户档位。
    required property var controller
    required property var receiver
    // 呈交统计来自实际 VideoView，与接收/解码统计分开；菜单不持有场景图资源。
    required property var view
    required property var session
    required property var pipeline
    required property real uiScale
    required property string graphicsBackend
    required property string connectionLabel
    // 同一比赛会话保留当前项目与草稿；切换分类或开关菜单不会提交配置。
    property string section: "session"
    // 本次比赛会话的可选设备选择；不使用时显式断开，切页/关菜单不会自动连接。
    property bool controllerInUse: session.serialController.configuration.portName !== ""
    // 错误文本相同不触发检查模型重建，避免原生 30Hz 快照持续重建概览按钮。
    readonly property string inputError: session.inputController.diagnostics.error || ""
    // 仅组合后端快照与人工确认用于展示；不存储控制数据、不据此授予发送资格。
    readonly property var checks: {
        const mqttReady = pipeline.connectionState === "ready";
        const mqttError = session.error || pipeline.lastError;
        const videoReady = receiver.running && receiver.status === "正在接收";
        const serial = session.serialController;
        const serialReady = controllerInUse && serial.state === "receiving";
        const inputReady = inputSettings.outputEnabled && inputSettings.confirmed && !menu.inputError;
        return [
            {
                key: "session",
                title: "接入 MQTT",
                available: true,
                passed: mqttReady && !sessionSettings.hasDraftChanges,
                failed: !mqttReady && !!mqttError,
                status: sessionSettings.hasDraftChanges ? "待保存" : mqttReady ? "检查通过" : mqttError ? "需要处理" : menu.connectionLabel,
                detail: sessionSettings.hasDraftChanges ? "有未保存修改，请连接并检查；下列状态仍对应已应用配置。" : mqttError || (mqttReady ? "MQTT 连接与订阅已就绪，当前机器人 ID " + session.configuration.robotId : "确认接入网卡与机器人身份，建立数据链路。"),
                items: [
                    {
                        label: "网卡地址可用",
                        passed: session.networkReady
                    },
                    {
                        label: "MQTT 连接与订阅就绪",
                        passed: mqttReady
                    },
                    {
                        label: "机器人身份已保存 · ID " + session.configuration.robotId,
                        passed: session.configuration.robotId > 0
                    }
                ]
            },
            {
                key: "video",
                title: "图传接收",
                available: controller.selectedMode === "RMUL",
                passed: videoReady && !videoSettings.hasDraftChanges,
                failed: !!receiver.error,
                status: controller.selectedMode !== "RMUL" ? "当前模式不适用" : videoSettings.hasDraftChanges ? "待保存" : receiver.error ? "需要处理" : videoReady ? "检查通过" : receiver.status,
                detail: videoSettings.hasDraftChanges ? "有未保存修改，请接收并检查；预览和下列状态仍对应已应用配置。" : receiver.error || "配置本机 UDP 监听与解码设备；收到实际视频后更新结果。",
                items: [
                    {
                        label: "UDP 监听已启动",
                        passed: receiver.running
                    },
                    {
                        label: "已发现当前发送端",
                        passed: receiver.sourceAddress !== ""
                    },
                    {
                        label: "正在接收并解码画面",
                        passed: videoReady
                    }
                ]
            },
            {
                key: "serial",
                title: "自定义控制器",
                available: true,
                passed: !controllerInUse || (serialReady && !serialSettings.hasDraftChanges),
                failed: controllerInUse && !!serial.error,
                status: !controllerInUse ? "不使用" : serialSettings.hasDraftChanges ? "待保存" : serial.error ? "需要处理" : serialReady ? "检查通过" : "待检查",
                detail: !controllerInUse ? "本次不使用自定义控制器，无需配置串口。" : serialSettings.hasDraftChanges ? "有未保存修改，请连接并检查；下列状态仍对应已应用参数。" : serial.error || "可选设备。连接串口并检查有效数据；设置页内仅本地检查，不转发控制。",
                heading: !controllerInUse ? "按你的设备来配置" : serialReady ? "有效数据已收到" : "确认控制器数据",
                items: !controllerInUse ? [
                    {
                        label: "不影响键鼠检查",
                        detail: "键鼠可以独立测试",
                        passed: true
                    },
                    {
                        label: "已有串口参数保留",
                        detail: "切换选择不会清除填写内容",
                        passed: true
                    }
                ] : [
                    {
                        label: "已选择使用控制器",
                        passed: controllerInUse
                    },
                    {
                        label: "串口已打开",
                        passed: serial.state === "waiting" || serialReady
                    },
                    {
                        label: "正在接收有效协议帧",
                        passed: serialReady
                    }
                ]
            },
            {
                key: "input",
                title: "键鼠检查",
                available: true,
                passed: inputReady,
                failed: !!inputError,
                status: inputError ? "需要处理" : !inputSettings.outputEnabled ? "未启用输出" : inputReady ? "检查通过" : "待检查",
                detail: inputError || (!inputSettings.outputEnabled ? "请先在键鼠检查页顶部启用输出，再完成测试并确认。" : "点击“开始测试”，按清单操作后确认键鼠可用。"),
                heading: !inputSettings.outputEnabled ? "请先启用输出" : inputReady ? "键鼠检查已确认" : "完成四项小检查",
                items: inputSettings.checkItems
            }
        ];
    }
    readonly property int completedChecks: checks.filter(check => check.passed).length
    // 保护本次比赛已取得的自检进度；后来断线/改参也不能消除离页提醒。
    // 默认“不使用控制器”是跳过项，不能把尚未检查的空会话视为已通过。
    property bool hasPassedCheck: false
    function rememberPassedChecks() {
        if (controller.currentPage === "arena" && checks.some(check => check.available && check.passed && (check.key !== "serial" || controllerInUse)))
            hasPassedCheck = true;
    }
    onChecksChanged: rememberPassedChecks()
    readonly property var currentCheck: checks.find(check => check.key === section) || checks[0]
    // 只借用现有 VideoView 的可视挂载点，不建立第二套接收/解码或呈交统计。
    readonly property alias videoPreviewTarget: videoSettings.previewTarget
    Connections {
        target: menu.controller
        function onCurrentPageChanged() {
            if (menu.controller.currentPage !== "arena") {
                inputSettings.confirmed = false;
                // 新比赛必须重新采集操作证据，不能沿用上一场完整清单直接确认。
                menu.session.inputController.resetStatistics();
                menu.draftsInitialized = false;
                menu.controllerInUse = menu.session.serialController.configuration.portName !== "";
                menu.hasPassedCheck = false;
            }
        }
    }
    function outputLow(key) {
        const number = view.presentationDiagnostics[key];
        return typeof number === "number" && isFinite(number) && number >= 0 ? number.toFixed(1) : "—";
    }
    // 无样本用破折号；避免把尚未测得的解码停顿和队列等待显示为零。
    function decodeMetric(key) {
        const number = receiver.diagnostics[key];
        return typeof number === "number" && isFinite(number) && number >= 0 ? number.toFixed(1) : "—";
    }
    // 每个阶段都显示独立窗口统计；没有执行或已过期的阶段不伪造为 0。
    function timingSummary(key, presentation) {
        const stages = (presentation ? view.presentationDiagnostics : receiver.diagnostics).timings || {};
        const stage = stages[key] || {};
        function number(field) {
            const value = stage[field];
            return typeof value === "number" && value >= 0 ? value.toFixed(2) : "—";
        }
        return number("meanMs") + " / " + number("p95Ms") + " / " + number("p99Ms") + " / " + number("maxMs") + " ms  ·  n=" + (stage.samples || 0);
    }
    // 请求宿主确认并导航；取消时保留菜单，由宿主离页生命周期释放监听端口。
    signal welcomeRequested

    // 面板保留明显的四周比赛画面；大屏封顶，避免再次扩展成近全屏工作区。
    width: Math.min(parent.width * 0.88, 1360)
    height: Math.min(parent.height * 0.84, 840)
    anchors.centerIn: parent
    modal: true
    focus: true
    padding: 0
    closePolicy: Popup.CloseOnEscape
    // 同一比赛会话重开设置仍保留当前项目与草稿，离开比赛后再从已保存配置初始化。
    property bool draftsInitialized: false
    readonly property var stepKeys: ["session", "video", "serial", "input"]
    function previousSection() {
        const index = stepKeys.indexOf(section);
        section = section === "overview" ? "session" : section === "serial" && controller.selectedMode !== "RMUL" ? "session" : index > 0 ? stepKeys[index - 1] : "overview";
    }
    function nextSection() {
        const index = stepKeys.indexOf(section);
        if (section === "overview")
            close();
        else if (section === "session" && controller.selectedMode !== "RMUL")
            section = "serial";
        else
            section = index >= 0 && index < 3 ? stepKeys[index + 1] : "overview";
    }
    onOpened: {
        if (!draftsInitialized) {
            section = "session";
            videoSettings.resetDraft();
            sessionSettings.resetDraft();
            serialSettings.resetDraft();
            inputSettings.resetDraft();
            draftsInitialized = true;
        }
        resume.forceActiveFocus(Qt.TabFocusReason);
    }
    background: Rectangle {
        radius: 8 * menu.uiScale
        color: "white"
        border.color: "#dce0e3"
    }
    Overlay.modal: Rectangle {
        color: "#6624272b"
    }
    contentItem: ColumnLayout {
        spacing: 0
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 73 * menu.uiScale
            color: "white"
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 29 * menu.uiScale
                anchors.rightMargin: 20 * menu.uiScale
                spacing: 20 * menu.uiScale
                Image {
                    Layout.preferredWidth: 29 * menu.uiScale
                    Layout.preferredHeight: 29 * menu.uiScale
                    source: "qrc:/resources/icons/preparation_brand.svg"
                }
                ColumnLayout {
                    spacing: 2 * menu.uiScale
                    Text {
                        text: "PIP CLIENT"
                        color: "#23272c"
                        font.pixelSize: 17 * menu.uiScale
                        font.weight: Font.Bold
                        font.letterSpacing: 1.4
                    }
                    Text {
                        text: "MATCH PREPARATION"
                        color: "#737982"
                        font.pixelSize: 9 * menu.uiScale
                        font.letterSpacing: 1.7
                    }
                }
                Rectangle {
                    implicitWidth: 1
                    implicitHeight: 26 * menu.uiScale
                    color: "#e3e5e7"
                }
                Text {
                    text: menu.controller.selectedMode + (menu.width >= 1050 * menu.uiScale ? "   高校联盟赛" : "")
                    color: "#626972"
                    font.pixelSize: 12 * menu.uiScale
                }
                Item {
                    Layout.fillWidth: true
                }
                VideoButton {
                    objectName: "menuSection_overview"
                    uiScale: menu.uiScale
                    quiet: true
                    text: "准备概览"
                    implicitWidth: 84 * menu.uiScale
                    onClicked: menu.section = "overview"
                }
                VideoButton {
                    id: resume
                    objectName: "menuResumeButton"
                    uiScale: menu.uiScale
                    quiet: true
                    text: "返回画面 →"
                    implicitWidth: 100 * menu.uiScale
                    onClicked: menu.close()
                }
                VideoButton {
                    objectName: "videoCloseButton"
                    uiScale: menu.uiScale
                    quiet: true
                    implicitWidth: 30 * menu.uiScale
                    text: "关闭设置（Esc）"
                    iconSource: "qrc:/resources/icons/close_panel.svg"
                    onClicked: menu.close()
                }
            }
            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                color: "#e3e5e7"
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0
            Rectangle {
                Layout.preferredWidth: 232 * menu.uiScale
                Layout.fillHeight: true
                color: "#f7f8f8"
                Rectangle {
                    anchors.right: parent.right
                    width: 1
                    height: parent.height
                    color: "#e3e5e7"
                }
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 17 * menu.uiScale
                    spacing: 0
                    ScrollView {
                        id: navigation
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        contentWidth: availableWidth
                        ColumnLayout {
                            width: navigation.availableWidth
                            spacing: 7 * menu.uiScale
                            Text {
                                Layout.leftMargin: 12 * menu.uiScale
                                Layout.topMargin: 9 * menu.uiScale
                                Layout.bottomMargin: 10 * menu.uiScale
                                text: "设置与自检 / SETTINGS"
                                color: "#91969d"
                                font.pixelSize: 10 * menu.uiScale
                                font.letterSpacing: 1
                            }
                            Repeater {
                                model: [
                                    {
                                        key: "session",
                                        label: "接入 MQTT",
                                        caption: "网络与机器人身份"
                                    },
                                    {
                                        key: "video",
                                        label: "图传接收",
                                        caption: "配置并确认画面"
                                    },
                                    {
                                        key: "serial",
                                        label: "自定义控制器",
                                        caption: "可选 · 串口与有效数据"
                                    },
                                    {
                                        key: "input",
                                        label: "键鼠检查",
                                        caption: "按键、移动与鼠标按钮"
                                    }
                                ]
                                Button {
                                    id: nav
                                    required property var modelData
                                    required property int index
                                    readonly property var check: menu.checks[index]
                                    objectName: "menuSection_" + modelData.key
                                    Layout.fillWidth: true
                                    implicitHeight: (menu.height < 660 * menu.uiScale ? 53 : 70) * menu.uiScale
                                    enabled: modelData.key !== "video" || menu.controller.selectedMode === "RMUL"
                                    Accessible.name: modelData.label + "，" + check.status
                                    hoverEnabled: true
                                    onClicked: menu.section = modelData.key
                                    padding: 12 * menu.uiScale
                                    contentItem: RowLayout {
                                        spacing: 12 * menu.uiScale
                                        Rectangle {
                                            implicitWidth: 28 * menu.uiScale
                                            implicitHeight: implicitWidth
                                            radius: width / 2
                                            color: nav.check.passed ? "#edf5f0" : menu.section === nav.modelData.key ? "#c54d25" : "white"
                                            border.color: nav.check.passed ? "#c8ded0" : menu.section === nav.modelData.key ? "#c54d25" : "#d9dde0"
                                            Text {
                                                anchors.centerIn: parent
                                                text: nav.check.passed ? "✓" : "0" + (nav.index + 1)
                                                color: nav.check.passed ? "#32745a" : menu.section === nav.modelData.key ? "white" : "#737982"
                                                font.pixelSize: 11 * menu.uiScale
                                            }
                                        }
                                        ColumnLayout {
                                            Layout.fillWidth: true
                                            spacing: 4 * menu.uiScale
                                            Text {
                                                Layout.fillWidth: true
                                                text: nav.modelData.label
                                                color: menu.section === nav.modelData.key ? "#c54d25" : "#5a616b"
                                                font.pixelSize: 13 * menu.uiScale
                                                font.weight: Font.Medium
                                            }
                                            Text {
                                                Layout.fillWidth: true
                                                text: nav.check.status === "待检查" || nav.check.status === "尚未连接" ? nav.modelData.caption : nav.check.status
                                                color: "#92979d"
                                                font.pixelSize: 10 * menu.uiScale
                                                elide: Text.ElideRight
                                            }
                                        }
                                    }
                                    background: Rectangle {
                                        radius: 5 * menu.uiScale
                                        color: menu.section === nav.modelData.key ? "#fbefea" : nav.hovered ? "#eeefef" : "transparent"
                                        border.color: nav.activeFocus ? "#c54d25" : menu.section === nav.modelData.key ? "#efd3c8" : "transparent"
                                    }
                                }
                            }
                            Rectangle {
                                Layout.fillWidth: true
                                Layout.margins: 12 * menu.uiScale
                                implicitHeight: 1
                                color: "#e3e5e7"
                            }
                            Repeater {
                                model: [
                                    {
                                        key: "display",
                                        label: "显示与偏好",
                                        icon: "menu_display.svg"
                                    },
                                    {
                                        key: "diagnostics",
                                        label: "运行诊断",
                                        icon: "menu_diagnostics.svg"
                                    },
                                    {
                                        key: "updates",
                                        label: "软件更新",
                                        icon: "menu_display.svg"
                                    }
                                ]
                                Button {
                                    id: utility
                                    required property var modelData
                                    objectName: "menuSection_" + modelData.key
                                    Layout.fillWidth: true
                                    implicitHeight: 36 * menu.uiScale
                                    onClicked: menu.section = modelData.key
                                    padding: 12 * menu.uiScale
                                    contentItem: RowLayout {
                                        spacing: 13 * menu.uiScale
                                        Image {
                                            Layout.preferredWidth: 16 * menu.uiScale
                                            Layout.preferredHeight: 16 * menu.uiScale
                                            source: "qrc:/resources/icons/" + utility.modelData.icon
                                        }
                                        Text {
                                            Layout.fillWidth: true
                                            text: utility.modelData.label
                                            color: "#737a83"
                                            font.pixelSize: 12 * menu.uiScale
                                        }
                                    }
                                    background: Rectangle {
                                        color: menu.section === utility.modelData.key ? "#e9ebec" : "transparent"
                                        radius: 4 * menu.uiScale
                                    }
                                }
                            }
                            VideoButton {
                                objectName: "videoReturnButton"
                                Layout.fillWidth: true
                                uiScale: menu.uiScale
                                implicitHeight: 30 * menu.uiScale
                                quiet: true
                                text: "返回欢迎页"
                                onClicked: menu.welcomeRequested()
                            }
                        }
                    }
                    ColumnLayout {
                        objectName: "preparationProgress"
                        Layout.fillWidth: true
                        Layout.margins: 12 * menu.uiScale
                        spacing: 8 * menu.uiScale
                        Text {
                            text: "准备进度    " + menu.completedChecks + " / 4"
                            color: "#8b9199"
                            font.pixelSize: 11 * menu.uiScale
                        }
                        Rectangle {
                            Layout.fillWidth: true
                            implicitHeight: 3 * menu.uiScale
                            radius: height / 2
                            color: "#e3e6e8"
                            Rectangle {
                                width: parent.width * menu.completedChecks / 4
                                height: parent.height
                                radius: parent.radius
                                color: "#32745a"
                            }
                        }
                        Text {
                            Layout.fillWidth: true
                            visible: menu.height >= 660 * menu.uiScale
                            text: "各项可随时切换与检查。\n检查结果只对当前配置有效。"
                            color: "#8b9199"
                            font.pixelSize: 10 * menu.uiScale
                            lineHeight: 1.7
                        }
                    }
                }
            }
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 0
                Item {
                    id: workspace
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    SessionSettings {
                        id: sessionSettings
                        anchors.fill: parent
                        visible: menu.section === "session"
                        session: menu.session
                        receiver: menu.receiver
                        pipeline: menu.pipeline
                        connectionLabel: menu.connectionLabel
                        uiScale: menu.uiScale
                        check: menu.checks[0]
                    }
                    VideoSettings {
                        id: videoSettings
                        objectName: "videoSettings"
                        anchors.fill: parent
                        visible: menu.section === "video"
                        receiver: menu.receiver
                        session: menu.session
                        uiScale: menu.uiScale
                        check: menu.checks[1]
                    }
                    SerialSettings {
                        id: serialSettings
                        objectName: "serialSettings"
                        anchors.fill: parent
                        visible: menu.section === "serial"
                        controller: menu.session.serialController
                        uiScale: menu.uiScale
                        check: menu.checks[2]
                        inUse: menu.controllerInUse
                        onUsageChanged: function (is_used) {
                            menu.controllerInUse = is_used;
                            if (!is_used)
                                menu.session.serialController.stop();
                        }
                    }
                    InputSettings {
                        id: inputSettings
                        objectName: "inputSettings"
                        anchors.fill: parent
                        visible: menu.section === "input"
                        input: menu.session.inputController
                        uiScale: menu.uiScale
                        check: menu.checks[3]
                    }
                    PreparationOverview {
                        objectName: "preparationOverview"
                        anchors.fill: parent
                        anchors.margins: 30 * menu.uiScale
                        visible: menu.section === "overview"
                        checks: menu.checks
                        uiScale: menu.uiScale
                        onSectionRequested: function (section) {
                            menu.section = section;
                        }
                    }
                    // 内容可滚动，侧栏全局动作始终可达；窗口变小时不缩小文字与控件。
                    ScrollView {
                        id: pages
                        anchors.fill: parent
                        anchors.margins: 30 * menu.uiScale
                        anchors.topMargin: 24 * menu.uiScale
                        visible: menu.section === "display" || menu.section === "diagnostics" || menu.section === "updates"
                        clip: true
                        contentWidth: availableWidth
                        ColumnLayout {
                            width: pages.availableWidth
                            spacing: 22 * menu.uiScale
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 6 * menu.uiScale
                                Text {
                                    text: menu.controller.selectedMode + "  /  " + (menu.section === "display" ? "DISPLAY" : menu.section === "updates" ? "UPDATES" : "DIAGNOSTICS")
                                    color: "#c94b17"
                                    font.pixelSize: 11 * menu.uiScale
                                    font.letterSpacing: 2
                                }
                                Text {
                                    text: menu.section === "display" ? "显示设置" : menu.section === "updates" ? "软件更新" : "运行诊断"
                                    font.pixelSize: 24 * menu.uiScale
                                    font.weight: Font.DemiBold
                                    color: "#24272b"
                                }
                            }
                            UpdateSettings {
                                Layout.fillWidth: true
                                visible: menu.section === "updates"
                                updater: menu.controller.updater
                                currentVersion: menu.controller.version
                                uiScale: menu.uiScale
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                visible: menu.section === "display"
                                spacing: 18 * menu.uiScale
                                Text {
                                    text: "界面大小"
                                    color: "#62666e"
                                    font.pixelSize: 15 * menu.uiScale
                                }
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 10 * menu.uiScale
                                    Repeater {
                                        model: [
                                            {
                                                key: "small",
                                                label: "小"
                                            },
                                            {
                                                key: "medium",
                                                label: "中"
                                            },
                                            {
                                                key: "large",
                                                label: "大"
                                            }
                                        ]
                                        VideoButton {
                                            required property var modelData
                                            objectName: "menuUiSize_" + modelData.key
                                            Layout.fillWidth: true
                                            uiScale: menu.uiScale
                                            text: modelData.label
                                            primary: menu.controller.uiSize === modelData.key
                                            onClicked: menu.controller.setUiSize(modelData.key)
                                        }
                                    }
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: "文字和控件按档位显示，窗口大小只改变布局。"
                                    wrapMode: Text.WordWrap
                                    color: "#737780"
                                    font.pixelSize: 13 * menu.uiScale
                                }
                                VideoButton {
                                    objectName: "menuFullscreenButton"
                                    uiScale: menu.uiScale
                                    text: menu.controller.fullScreen ? "退出全屏 · F11" : "进入全屏 · F11"
                                    onClicked: menu.controller.setFullScreen(!menu.controller.fullScreen)
                                }
                                VerticalSyncSettings {
                                    objectName: "menuVerticalSync"
                                    Layout.fillWidth: true
                                    controller: menu.controller
                                    uiScale: menu.uiScale
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: menu.controller.lastError
                                    visible: text !== ""
                                    color: "#a33d1b"
                                    font.pixelSize: 13 * menu.uiScale
                                    wrapMode: Text.WrapAnywhere
                                }
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                visible: menu.section === "diagnostics"
                                spacing: 16 * menu.uiScale
                                Text {
                                    Layout.fillWidth: true
                                    text: menu.controller.loggingStatus
                                    color: "#62666e"
                                    font.pixelSize: 13 * menu.uiScale
                                    wrapMode: Text.WordWrap
                                }
                                Button {
                                    objectName: "openMenuLogs"
                                    text: "打开本地日志目录"
                                    enabled: menu.controller.logDirectory !== ""
                                    onClicked: menu.controller.openLogDirectory()
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: "视频阶段耗时 · 最近 1 秒\n均值 / P95 / P99 / 最大值"
                                    color: "#24272b"
                                    font.pixelSize: 14 * menu.uiScale
                                    font.weight: Font.Medium
                                    lineHeight: 1.5
                                }
                                Repeater {
                                    model: [
                                        {
                                            key: "receiveLocal",
                                            label: "首包读出至 Qt 呈交",
                                            presentation: true
                                        },
                                        {
                                            key: "decodeTotal",
                                            label: "解码输出耗时（至帧引用就绪）",
                                            presentation: false
                                        },
                                        {
                                            key: "assembly",
                                            label: "首包读出至组帧（已解码样本）",
                                            presentation: false
                                        },
                                        {
                                            key: "queue",
                                            label: "解码排队",
                                            presentation: false
                                        },
                                        {
                                            key: "codec",
                                            label: "编解码器调用",
                                            presentation: false
                                        },
                                        {
                                            key: "download",
                                            label: "解码侧下载（RGBA 参考模式）",
                                            presentation: false
                                        },
                                        {
                                            key: "convert",
                                            label: "解码侧转色（RGBA 参考模式）",
                                            presentation: false
                                        },
                                        {
                                            key: "guiWait",
                                            label: menu.view.presentationDiagnostics.presentationLoop === "independent-rhi" ? "输出至呈交线程取帧（兼容项）" : "输出至渲染同步",
                                            presentation: true
                                        },
                                        {
                                            key: "mailboxWait",
                                            label: menu.view.presentationDiagnostics.presentationLoop === "independent-rhi" ? "输出至呈交线程取帧" : "输出至 GUI 取帧",
                                            presentation: true
                                        },
                                        {
                                            key: "guiPrepare",
                                            label: "GUI 取帧至请求刷新",
                                            presentation: true
                                        },
                                        {
                                            key: "renderWait",
                                            label: "请求刷新至渲染同步",
                                            presentation: true
                                        },
                                        {
                                            key: "texture",
                                            label: "纹理准备（CPU）",
                                            presentation: true
                                        },
                                        {
                                            key: "gpuBridge",
                                            label: "硬件纹理互操作（纹理准备子项）",
                                            presentation: true
                                        },
                                        {
                                            key: "renderDownload",
                                            label: "显示侧下载回退（纹理准备子项）",
                                            presentation: true
                                        },
                                        {
                                            key: "renderConvert",
                                            label: "显示侧 CPU 转色（纹理准备子项）",
                                            presentation: true
                                        },
                                        {
                                            key: "presentWait",
                                            label: "准备至 Qt 呈交",
                                            presentation: true
                                        },
                                        {
                                            key: "local",
                                            label: "组帧至 Qt 呈交",
                                            presentation: true
                                        }
                                    ]
                                    ColumnLayout {
                                        id: timingRow
                                        required property var modelData
                                        Layout.fillWidth: true
                                        spacing: 3 * menu.uiScale
                                        Text {
                                            text: timingRow.modelData.label
                                            color: "#62666e"
                                            font.pixelSize: 12 * menu.uiScale
                                        }
                                        Text {
                                            objectName: "videoTiming_" + timingRow.modelData.key
                                            Layout.fillWidth: true
                                            text: menu.timingSummary(timingRow.modelData.key, timingRow.modelData.presentation)
                                            color: "#24272b"
                                            font.pixelSize: 13 * menu.uiScale
                                            wrapMode: Text.WrapAnywhere
                                        }
                                    }
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: "显示路径：" + (menu.view.presentationDiagnostics.renderPath || "尚无画面") + "\n显示回退：" + (menu.view.presentationDiagnostics.renderFallback || "无") + "\n首包起点是应用读出首个有效分片，未包含内核缓冲等待。Qt 呈交不是显示器扫描完成。正常解码只转交帧引用；共享失败后的下载和转色计入纹理准备子项，不能重复相加。互操作只测 CPU 提交与同步，后续 GPU 工作仍在呈交路径中；未执行阶段显示 —。不累加各阶段 P95，每阶段最近 1 秒最多 512 样本。"
                                    color: "#737780"
                                    font.pixelSize: 11 * menu.uiScale
                                    wrapMode: Text.WordWrap
                                }
                                Text {
                                    objectName: "mqttDiagnostics"
                                    Layout.fillWidth: true
                                    text: "MQTT 本机端点   " + (menu.pipeline.diagnostics.localAddress || "尚未连接") + " : " + (menu.pipeline.diagnostics.localPort || 0) + "\nMQTT 对端   " + (menu.pipeline.diagnostics.peerAddress || "尚未连接") + " : " + (menu.pipeline.diagnostics.peerPort || 0) + "\n接收 / 发送   " + (menu.pipeline.diagnostics.receivedCount || 0) + " / " + (menu.pipeline.diagnostics.sentCount || 0) + "\n待发队列   " + (menu.pipeline.diagnostics.queuedPublications || 0) + " 项 / " + (menu.pipeline.diagnostics.queuedBytes || 0) + " 字节" + "\n事件丢弃   " + ((menu.pipeline.diagnostics.eventOverflow || 0) + (menu.pipeline.diagnostics.guiEventOverflow || 0)) + "\n发送结果丢弃   " + (menu.pipeline.diagnostics.resultOverflow || 0)
                                    color: "#62666e"
                                    font.pixelSize: 13 * menu.uiScale
                                    lineHeight: 1.6
                                    wrapMode: Text.WrapAnywhere
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: "图传统计：右上角 RX 为完整收帧、DEC 为有效解码、OUT 为新视频帧的 Qt 呈交；均按最近 1 秒汇总。\nOUT 5% / 1% low   " + menu.outputLow("low5Fps") + " / " + menu.outputLow("low1Fps") + " FPS（最近 10 秒呈交间隔）\nRX low 单独采用收帧间隔，两者不能互相代替。GAP 是整帧序号缺口率，UDP 包损不可精确测量。\n非法包 / 重复分片 / 未完成帧（最近 1 秒）  " + (menu.receiver.diagnostics.invalidPackets || 0) + " / " + (menu.receiver.diagnostics.duplicatePackets || 0) + " / " + (menu.receiver.diagnostics.incompleteFrames || 0)
                                    color: "#62666e"
                                    font.pixelSize: 12 * menu.uiScale
                                    lineHeight: 1.6
                                    wrapMode: Text.WordWrap
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: menu.pipeline.lastError
                                    visible: text !== ""
                                    color: "#a33d1b"
                                    font.pixelSize: 13 * menu.uiScale
                                    wrapMode: Text.WrapAnywhere
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: "渲染后端   " + menu.graphicsBackend + "\n数据链路   " + menu.connectionLabel + "\n图传接收   " + menu.receiver.status + "\n当前发送端   " + (menu.receiver.sourceAddress === "" ? "尚未发现" : (menu.receiver.sourceAddress.indexOf(":") >= 0 ? "[" + menu.receiver.sourceAddress + "]" : menu.receiver.sourceAddress) + " : " + menu.receiver.sourcePort) + "\n解码路径   FFmpeg / " + (menu.receiver.diagnostics.decoder || "尚未选择") + "\n解码设备   " + (menu.receiver.diagnostics.decoderDevice || "—") + "\n解码平均 / 最大（1 秒）   " + menu.decodeMetric("decodeMs") + " / " + menu.decodeMetric("decodeMaxMs") + " ms\n最近一次解码排队   " + menu.decodeMetric("decodeQueueMs") + " ms\n本地待解码丢弃（累计）   " + (menu.receiver.diagnostics.decodeQueueDropped || 0) + " 帧\n硬件帧输出   " + (menu.receiver.diagnostics.hardwareActive ? "已验证" : "未验证 / 软件路径") + "\n回退记录   " + (menu.receiver.diagnostics.decoderFallback || "无") + "\n客户端版本   " + menu.controller.version
                                    color: "#62666e"
                                    font.pixelSize: 14 * menu.uiScale
                                    lineHeight: 2
                                    wrapMode: Text.WrapAnywhere
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: menu.receiver.error !== "" ? menu.receiver.error : "当前无图传错误"
                                    color: menu.receiver.error !== "" ? "#a33d1b" : "#737780"
                                    font.pixelSize: 13 * menu.uiScale
                                    wrapMode: Text.WrapAnywhere
                                }
                            }
                        }
                    }
                }
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: 75 * menu.uiScale
                    color: "white"
                    Rectangle {
                        width: parent.width
                        height: 1
                        color: "#e3e5e7"
                    }
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 20 * menu.uiScale
                        anchors.rightMargin: 24 * menu.uiScale
                        spacing: 12 * menu.uiScale
                        VideoButton {
                            objectName: "preparationBack"
                            uiScale: menu.uiScale
                            quiet: true
                            text: "‹  " + (menu.section === "overview" ? "返回配置" : menu.stepKeys.indexOf(menu.section) > 0 ? "上一项" : "准备概览")
                            onClicked: menu.previousSection()
                        }
                        Text {
                            Layout.fillWidth: true
                            visible: menu.width >= 1150 * menu.uiScale
                            text: "检查可稍后处理，各项设置随时可切换"
                            color: "#979ca3"
                            font.pixelSize: 10 * menu.uiScale
                            wrapMode: Text.WordWrap
                        }
                        Item {
                            Layout.fillWidth: true
                            visible: menu.width < 1150 * menu.uiScale
                        }
                        VideoButton {
                            objectName: "preparationNext"
                            uiScale: menu.uiScale
                            primary: true
                            text: menu.section === "overview" ? "返回比赛画面 →" : menu.section === "session" ? (menu.controller.selectedMode === "RMUL" ? "查看图传 →" : "查看控制器 →") : menu.section === "video" ? "查看控制器 →" : menu.section === "serial" ? "查看键鼠 →" : "查看准备概览 →"
                            onClicked: menu.nextSection()
                        }
                    }
                }
            }
        }
    }
}
