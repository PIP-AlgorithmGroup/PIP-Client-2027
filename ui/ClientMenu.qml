pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// RMUL 客户端菜单拥有导航与模态焦点；分区只编辑各自配置，不拥有后台会话。
Popup {
    id: menu
    // GUI 线程注入对象，菜单不延长其生命周期；uiScale 只来自用户档位。
    required property var controller
    required property var receiver
    required property real uiScale
    required property string graphicsBackend
    required property string connectionLabel
    // 每次打开回到会话首页；分类切换不提交图传草稿。
    property string section: "session"
    // 关闭后请求宿主导航；由宿主页面生命周期释放监听端口。
    signal welcomeRequested
    // 面板跟随可用窗口扩展，四周留 24–48 逻辑像素；不把窗口比例传给字体或控件。
    readonly property real viewportMargin: Math.max(24, Math.min(48, Math.min(parent.width, parent.height) * 0.04))
    width: parent.width - 2 * viewportMargin
    height: parent.height - 2 * viewportMargin
    anchors.centerIn: parent
    modal: true
    focus: true
    padding: 0
    closePolicy: Popup.CloseOnEscape
    onOpened: {
        section = "session";
        videoSettings.resetDraft();
        resume.forceActiveFocus(Qt.TabFocusReason);
    }
    background: Rectangle {
        radius: 14 * menu.uiScale
        color: "#f7fafc"
        border.color: "#dce5eb"
    }
    Overlay.modal: Rectangle {
        color: "#b8101c26"
    }
    contentItem: RowLayout {
        spacing: 0
        Rectangle {
            Layout.preferredWidth: 206 * menu.uiScale
            Layout.fillHeight: true
            color: "#edf2f6"
            radius: 14 * menu.uiScale
            // 遮住侧栏内侧圆角，保留外轮廓圆角。
            Rectangle {
                anchors.right: parent.right
                width: 14 * menu.uiScale
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
                    color: "#1d4054"
                    font.pixelSize: 18 * menu.uiScale
                    font.weight: Font.Bold
                    font.letterSpacing: 1
                }
                Text {
                    Layout.bottomMargin: 24 * menu.uiScale
                    text: "赛场菜单  /  RMUL"
                    color: "#8a9eab"
                    font.pixelSize: 12 * menu.uiScale
                }
                Repeater {
                    model: [
                        { key: "session", label: "当前会话", icon: "menu_session.svg" },
                        { key: "video", label: "图传设置", icon: "menu_video.svg" },
                        { key: "display", label: "显示设置", icon: "menu_display.svg" },
                        { key: "diagnostics", label: "运行诊断", icon: "menu_diagnostics.svg" }
                    ]
                    Button {
                        id: nav
                        required property var modelData
                        objectName: "menuSection_" + modelData.key
                        Layout.fillWidth: true
                        implicitHeight: 48 * menu.uiScale
                        text: modelData.label
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
                                color: menu.section === nav.modelData.key ? "#176e8c" : "#526f80"
                                font.pixelSize: 14 * menu.uiScale
                                font.weight: menu.section === nav.modelData.key ? Font.DemiBold : Font.Normal
                            }
                        }
                        background: Rectangle {
                            radius: 7 * menu.uiScale
                            color: menu.section === nav.modelData.key ? "white" : nav.hovered ? "#e2ebf0" : "transparent"
                            border.color: nav.activeFocus ? "#268bad" : "transparent"
                        }
                    }
                }
                Item { Layout.fillHeight: true }
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
                anchors.topMargin: 20 * menu.uiScale
                visible: menu.section === "video"
                receiver: menu.receiver
                uiScale: menu.uiScale
            }
            // 内容可滚动，侧栏全局动作始终可达；窗口变小时不缩小文字与控件。
            ScrollView {
                id: pages
                anchors.fill: parent
                anchors.margins: 34 * menu.uiScale
                visible: menu.section !== "video"
                clip: true
                contentWidth: availableWidth
                ColumnLayout {
                    width: pages.availableWidth
                    spacing: 22 * menu.uiScale
                    Text {
                        text: "RMUL  /  " + (menu.section === "session" ? "SESSION" : menu.section === "display" ? "DISPLAY" : "DIAGNOSTICS")
                        color: "#32809b"
                        font.pixelSize: 11 * menu.uiScale
                        font.letterSpacing: 2
                    }
                    Text {
                        text: menu.section === "session" ? "当前会话" : menu.section === "display" ? "显示设置" : "运行诊断"
                        font.pixelSize: 28 * menu.uiScale
                        font.weight: Font.DemiBold
                        color: "#19384c"
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        visible: menu.section === "session"
                        spacing: 22 * menu.uiScale
                        Text {
                            Layout.topMargin: 16 * menu.uiScale
                            text: "RMUL"
                            font.pixelSize: 68 * menu.uiScale
                            font.weight: Font.Bold
                            color: "#244c60"
                        }
                        Text {
                            text: "机甲大师高校联盟赛"
                            font.pixelSize: 18 * menu.uiScale
                            color: "#587787"
                        }
                        Rectangle {
                            Layout.fillWidth: true
                            implicitHeight: 1
                            color: "#dbe5ec"
                        }
                        Text {
                            Layout.fillWidth: true
                            text: "图传 · " + menu.receiver.status + "\nUDP  " + menu.receiver.bindAddress + " : " + menu.receiver.port
                            color: "#526f80"
                            font.pixelSize: 15 * menu.uiScale
                            lineHeight: 1.7
                            wrapMode: Text.WrapAnywhere
                        }
                        Text {
                            Layout.fillWidth: true
                            text: "菜单打开时保持接收，关闭后继续观看。\nEsc 随时返回画面。"
                            color: "#91a4af"
                            font.pixelSize: 13 * menu.uiScale
                            lineHeight: 1.6
                        }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        visible: menu.section === "display"
                        spacing: 18 * menu.uiScale
                        Text {
                            text: "界面大小"
                            color: "#526f80"
                            font.pixelSize: 15 * menu.uiScale
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10 * menu.uiScale
                            Repeater {
                                model: [{ key: "small", label: "小" }, { key: "medium", label: "中" }, { key: "large", label: "大" }]
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
                            color: "#91a4af"
                            font.pixelSize: 13 * menu.uiScale
                        }
                        VideoButton {
                            objectName: "menuFullscreenButton"
                            uiScale: menu.uiScale
                            text: menu.controller.fullScreen ? "退出全屏 · F11" : "进入全屏 · F11"
                            onClicked: menu.controller.setFullScreen(!menu.controller.fullScreen)
                        }
                        Text {
                            Layout.fillWidth: true
                            text: menu.controller.lastError
                            visible: text !== ""
                            color: "#a65d2e"
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
                            text: "渲染后端   " + menu.graphicsBackend + "\n数据链路   " + menu.connectionLabel + "\n图传接收   " + menu.receiver.status + "\n当前发送端   " + (menu.receiver.sourceAddress === "" ? "尚未发现" : (menu.receiver.sourceAddress.indexOf(":") >= 0 ? "[" + menu.receiver.sourceAddress + "]" : menu.receiver.sourceAddress) + " : " + menu.receiver.sourcePort) + "\n解码路径   FFmpeg / CPU\n客户端版本   " + menu.controller.version
                            color: "#526f80"
                            font.pixelSize: 14 * menu.uiScale
                            lineHeight: 2
                            wrapMode: Text.WrapAnywhere
                        }
                        Text {
                            Layout.fillWidth: true
                            text: menu.receiver.error !== "" ? menu.receiver.error : "当前无图传错误"
                            color: menu.receiver.error !== "" ? "#a65d2e" : "#91a4af"
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
