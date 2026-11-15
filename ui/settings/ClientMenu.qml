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
    // 每次打开回到会话首页；分类切换不提交图传草稿。
    property string section: "session"
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
    // 关闭后请求宿主导航；由宿主页面生命周期释放监听端口。
    signal welcomeRequested
    // 中档最大 1120×740；小窗口保留外侧留白，内部滚动，不按窗口比例缩小控件。
    width: Math.min(parent.width - 48, 1120 * uiScale)
    height: Math.min(parent.height - 80, 740 * uiScale)
    anchors.centerIn: parent
    modal: true
    focus: true
    padding: 0
    closePolicy: Popup.CloseOnEscape
    onOpened: {
        section = "session";
        videoSettings.resetDraft();
        sessionSettings.resetDraft();
        serialSettings.resetDraft();
        resume.forceActiveFocus(Qt.TabFocusReason);
    }
    background: Rectangle {
        radius: 5 * menu.uiScale
        color: "#fafafa"
        border.color: "#d9dce0"
    }
    Overlay.modal: Rectangle {
        color: "#6624272b"
    }
    contentItem: RowLayout {
        spacing: 0
        Rectangle {
            Layout.preferredWidth: 206 * menu.uiScale
            Layout.fillHeight: true
            color: "#edeef0"
            radius: 5 * menu.uiScale
            // 遮住侧栏内侧圆角，保留外轮廓圆角。
            Rectangle {
                anchors.right: parent.right
                width: 5 * menu.uiScale
                height: parent.height
                color: parent.color
            }
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 18 * menu.uiScale
                spacing: 8 * menu.uiScale
                Text {
                    Layout.topMargin: 12 * menu.uiScale
                    text: "PIP CLIENT"
                    color: "#24272b"
                    font.pixelSize: 18 * menu.uiScale
                    font.weight: Font.Bold
                    font.letterSpacing: 1
                }
                Text {
                    Layout.bottomMargin: 24 * menu.uiScale
                    text: "赛场菜单  /  " + menu.controller.selectedMode
                    color: "#737780"
                    font.pixelSize: 12 * menu.uiScale
                }
                Repeater {
                    model: [
                        {
                            key: "session",
                            label: "当前会话",
                            icon: "menu_session.svg"
                        },
                        {
                            key: "video",
                            label: "图传设置",
                            icon: "menu_video.svg"
                        },
                        {
                            key: "serial",
                            label: "串口设置",
                            icon: "menu_session.svg"
                        },
                        {
                            key: "display",
                            label: "显示设置",
                            icon: "menu_display.svg"
                        },
                        {
                            key: "diagnostics",
                            label: "运行诊断",
                            icon: "menu_diagnostics.svg"
                        }
                    ]
                    Button {
                        id: nav
                        required property var modelData
                        objectName: "menuSection_" + modelData.key
                        Layout.fillWidth: true
                        implicitHeight: 48 * menu.uiScale
                        text: modelData.label
                        enabled: modelData.key !== "video" || menu.controller.selectedMode === "RMUL"
                        Accessible.name: text
                        hoverEnabled: true
                        onClicked: menu.section = modelData.key
                        contentItem: RowLayout {
                            spacing: 14 * menu.uiScale
                            // 打包线性 SVG 避免依赖平台图标字体；标签保留可访问名称。
                            Image {
                                objectName: "menuIcon_" + nav.modelData.key
                                Layout.preferredWidth: 18 * menu.uiScale
                                Layout.preferredHeight: 18 * menu.uiScale
                                source: "qrc:/resources/icons/" + nav.modelData.icon
                                sourceSize: Qt.size(Math.ceil(width * Screen.devicePixelRatio), Math.ceil(height * Screen.devicePixelRatio))
                                opacity: menu.section === nav.modelData.key ? 1 : 0.65
                            }
                            Text {
                                Layout.fillWidth: true
                                text: nav.text
                                color: menu.section === nav.modelData.key ? "#c94b17" : "#62666e"
                                font.pixelSize: 14 * menu.uiScale
                                font.weight: menu.section === nav.modelData.key ? Font.DemiBold : Font.Normal
                            }
                        }
                        background: Rectangle {
                            radius: 3 * menu.uiScale
                            color: menu.section === nav.modelData.key ? "white" : nav.hovered ? "#e3e4e7" : "transparent"
                            border.color: nav.activeFocus ? "#c94b17" : "transparent"
                        }
                    }
                }
                Item {
                    Layout.fillHeight: true
                }
                VideoButton {
                    id: resume
                    objectName: "menuResumeButton"
                    Layout.fillWidth: true
                    uiScale: menu.uiScale
                    primary: true
                    text: "继续画面"
                    onClicked: menu.close()
                }
                VideoButton {
                    objectName: "videoReturnButton"
                    Layout.fillWidth: true
                    uiScale: menu.uiScale
                    quiet: true
                    text: "返回欢迎页"
                    onClicked: {
                        menu.close();
                        menu.welcomeRequested();
                    }
                }
            }
        }
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            VideoSettings {
                id: videoSettings
                objectName: "videoSettings"
                anchors.fill: parent
                visible: menu.section === "video"
                receiver: menu.receiver
                session: menu.session
                uiScale: menu.uiScale
            }
            SessionSettings {
                id: sessionSettings
                anchors.fill: parent
                visible: menu.section === "session"
                session: menu.session
                receiver: menu.receiver
                pipeline: menu.pipeline
                connectionLabel: menu.connectionLabel
                uiScale: menu.uiScale
            }
            SerialSettings {
                id: serialSettings
                objectName: "serialSettings"
                anchors.fill: parent
                visible: menu.section === "serial"
                controller: menu.session.serialController
                uiScale: menu.uiScale
            }
            // 内容可滚动，侧栏全局动作始终可达；窗口变小时不缩小文字与控件。
            ScrollView {
                id: pages
                anchors.fill: parent
                anchors.margins: 28 * menu.uiScale
                anchors.topMargin: 24 * menu.uiScale
                visible: menu.section !== "video" && menu.section !== "session" && menu.section !== "serial"
                clip: true
                contentWidth: availableWidth
                ColumnLayout {
                    width: pages.availableWidth
                    spacing: 22 * menu.uiScale
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 6 * menu.uiScale
                        Text {
                            text: menu.controller.selectedMode + "  /  " + (menu.section === "display" ? "DISPLAY" : "DIAGNOSTICS")
                            color: "#c94b17"
                            font.pixelSize: 11 * menu.uiScale
                            font.letterSpacing: 2
                        }
                        Text {
                            text: menu.section === "display" ? "显示设置" : "运行诊断"
                            font.pixelSize: 24 * menu.uiScale
                            font.weight: Font.DemiBold
                            color: "#24272b"
                        }
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
                                    label: "解码总耗时（含下载 / 转色）",
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
                                    label: "硬件帧下载",
                                    presentation: false
                                },
                                {
                                    key: "convert",
                                    label: "RGBA 转色",
                                    presentation: false
                                },
                                {
                                    key: "guiWait",
                                    label: "输出至渲染同步",
                                    presentation: true
                                },
                                {
                                    key: "mailboxWait",
                                    label: "输出至 GUI 取帧",
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
                            text: "首包起点是应用读出首个有效分片，未包含内核缓冲等待。Qt 呈交不是显示器扫描完成，真实亮屏时间需外部测量。解码总耗时直接计时，不累加各阶段 P95。CPU 路径不执行硬件下载。编解码器调用包含驱动等待；纹理准备只测 CPU。每阶段最近 1 秒最多 512 样本，小样本 P99 仅作长尾线索。"
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
            VideoButton {
                objectName: "videoCloseButton"
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 18 * menu.uiScale
                uiScale: menu.uiScale
                implicitWidth: 40 * menu.uiScale
                implicitHeight: 40 * menu.uiScale
                quiet: true
                text: "关闭客户端菜单（Esc）"
                iconSource: "qrc:/resources/icons/close_panel.svg"
                onClicked: menu.close()
            }
        }
    }
}
