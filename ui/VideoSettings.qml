pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 客户端菜单的图传分区；草稿仅在主动作提交，菜单关闭不改变监听。
Item {
    id: panel
    // 由宿主注入 GUI 接收器和 UI 档位；面板不拥有 C++ 对象。
    required property var receiver
    required property var session
    required property real uiScale
    // 宿主每次打开菜单重置草稿；切换分类时保留未保存编辑，不触碰会话。
    function resetDraft() {
        address.text = receiver.bindAddress;
        port.text = String(receiver.port);
        indexBase.currentIndex = receiver.indexBase;
        // 重新打开从可编辑配置开始；同次菜单内切换分类仍保留滚动位置和草稿。
        formScroll.contentItem.contentY = 0;
    }
    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        ColumnLayout {
            Layout.fillWidth: true
            Layout.margins: 16 * panel.uiScale
            spacing: 6 * panel.uiScale
            RowLayout {
                Layout.fillWidth: true
                ColumnLayout {
                    spacing: 4 * panel.uiScale
                    Text {
                        text: "VIDEO LINK"
                        color: "#32809b"
                        font.pixelSize: 11 * panel.uiScale
                        font.weight: Font.DemiBold
                        font.letterSpacing: 2
                    }
                    Text {
                        text: "图传设置"
                        color: "#19384c"
                        font.pixelSize: 28 * panel.uiScale
                        font.weight: Font.DemiBold
                    }
                }
                Item {
                    Layout.fillWidth: true
                }
            }
            Text {
                text: "配置本机 UDP 监听，接收赛场实时画面。"
                color: "#7a8e9c"
                font.pixelSize: 13 * panel.uiScale
            }
        }
        Rectangle {
            Layout.fillWidth: true
            Layout.leftMargin: 28 * panel.uiScale
            Layout.rightMargin: 28 * panel.uiScale
            implicitHeight: summary.implicitHeight + 24 * panel.uiScale
            color: panel.receiver.error !== "" ? "#fff3e9" : "#eaf3f6"
            radius: 8 * panel.uiScale
            ColumnLayout {
                id: summary
                anchors.fill: parent
                anchors.margins: 12 * panel.uiScale
                spacing: 5 * panel.uiScale
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10 * panel.uiScale
                    Rectangle {
                        implicitWidth: 7 * panel.uiScale
                        implicitHeight: implicitWidth
                        radius: width / 2
                        color: panel.receiver.error !== "" ? "#cc8950" : panel.receiver.running ? "#378aa0" : "#92a4af"
                    }
                    Text {
                        objectName: "videoReceptionStatus"
                        Layout.fillWidth: true
                        text: panel.receiver.status
                        color: "#294d61"
                        font.pixelSize: 14 * panel.uiScale
                        font.weight: Font.Medium
                    }
                    Text {
                        text: "UDP / HEVC"
                        color: "#7a98a7"
                        font.pixelSize: 11 * panel.uiScale
                        font.letterSpacing: 1
                    }
                }
                Text {
                    Layout.fillWidth: true
                    text: "本机监听  " + panel.receiver.bindAddress + " : " + panel.receiver.port
                    color: "#78919f"
                    font.pixelSize: 12 * panel.uiScale
                    wrapMode: Text.WrapAnywhere
                }
                Text {
                    objectName: "videoSourceEndpoint"
                    Layout.fillWidth: true
                    // 实际来源由后端会话锁定；未知/超时不能把 MQTT 地址或旧来源冒充当前来源。
                    text: "当前发送端  " + (panel.receiver.sourceAddress === "" ? "尚未发现" : (panel.receiver.sourceAddress.indexOf(":") >= 0 ? "[" + panel.receiver.sourceAddress + "]" : panel.receiver.sourceAddress) + " : " + panel.receiver.sourcePort)
                    color: "#78919f"
                    font.pixelSize: 12 * panel.uiScale
                    wrapMode: Text.WrapAnywhere
                }
            }
        }
        // 小窗口只滚动表单，底部动作始终可达；不通过窗口尺寸缩小字号。
        ScrollView {
            id: formScroll
            objectName: "videoFormScroll"
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: 28 * panel.uiScale
            Layout.rightMargin: 28 * panel.uiScale
            Layout.topMargin: 12 * panel.uiScale
            Layout.bottomMargin: 12 * panel.uiScale
            contentWidth: availableWidth
            clip: true
            ColumnLayout {
                width: formScroll.availableWidth
                spacing: 9 * panel.uiScale
                Text {
                    text: "本机监听地址"
                    color: "#627e8f"
                    font.pixelSize: 12 * panel.uiScale
                }
                VideoField {
                    id: address
                    objectName: "videoBindAddress"
                    uiScale: panel.uiScale
                    Layout.fillWidth: true
                    placeholderText: "0.0.0.0"
                    readOnly: panel.session.active
                    Accessible.name: "本机 UDP 监听地址"
                }
                Text {
                    text: "官方终端网卡 IP：192.168.12.2。此处填本机地址；0.0.0.0 监听所有本机 IPv4 网卡。"
                    color: "#8b9faa"
                    font.pixelSize: 11 * panel.uiScale
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                }
                RowLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: 8 * panel.uiScale
                    spacing: 20 * panel.uiScale
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.preferredWidth: 3
                        spacing: 8 * panel.uiScale
                        Text {
                            objectName: "videoPortLabel"
                            text: "UDP 端口 · 默认 3334"
                            color: "#627e8f"
                            font.pixelSize: 12 * panel.uiScale
                        }
                        VideoField {
                            id: port
                            objectName: "videoPort"
                            uiScale: panel.uiScale
                            Layout.fillWidth: true
                            Accessible.name: "UDP 监听端口"
                            validator: IntValidator {
                                bottom: 1
                                top: 65535
                            }
                        }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.preferredWidth: 2
                        spacing: 8 * panel.uiScale
                        Text {
                            text: "首个分片编号"
                            color: "#627e8f"
                            font.pixelSize: 12 * panel.uiScale
                        }
                        Rectangle {
                            id: indexBase
                            objectName: "videoIndexBase"
                            // 草稿不会影响接收器，直到保存成功。
                            property int currentIndex: 0
                            Layout.fillWidth: true
                            implicitHeight: 46 * panel.uiScale
                            color: "#edf2f6"
                            radius: 7 * panel.uiScale
                            RowLayout {
                                anchors.fill: parent
                                anchors.margins: 4 * panel.uiScale
                                spacing: 4 * panel.uiScale
                                Repeater {
                                    model: 2
                                    RadioButton {
                                        id: segment
                                        required property int index
                                        objectName: "videoIndex_" + index
                                        Layout.fillWidth: true
                                        Layout.fillHeight: true
                                        text: String(index)
                                        checked: indexBase.currentIndex === index
                                        autoExclusive: false
                                        Accessible.name: "分片起始序号 " + text
                                        indicator: null
                                        onClicked: {
                                            indexBase.currentIndex = index;
                                            checked = Qt.binding(function () {
                                                return indexBase.currentIndex === segment.index;
                                            });
                                        }
                                        contentItem: Text {
                                            text: segment.text
                                            font.pixelSize: 15 * panel.uiScale
                                            font.weight: segment.checked ? Font.DemiBold : Font.Normal
                                            color: segment.checked ? "#176e8c" : "#8197a5"
                                            horizontalAlignment: Text.AlignHCenter
                                            verticalAlignment: Text.AlignVCenter
                                        }
                                        background: Rectangle {
                                            color: segment.checked ? "white" : "transparent"
                                            radius: 5 * panel.uiScale
                                            border.color: segment.activeFocus ? "#268bad" : segment.checked ? "#d6e3eb" : "transparent"
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                Text {
                    Layout.fillWidth: true
                    text: "端口可修改，需与发送目标端口一致。分片编号 0 表示 0、1、2…，1 表示 1、2、3…；协议未注明起点，需与发送端一致。"
                    color: "#8b9faa"
                    font.pixelSize: 11 * panel.uiScale
                    wrapMode: Text.WordWrap
                }
                Text {
                    objectName: "videoSettingsError"
                    Layout.fillWidth: true
                    Layout.topMargin: 6 * panel.uiScale
                    text: panel.receiver.error !== "" ? panel.receiver.error : panel.session.error
                    color: "#a65d2e"
                    font.pixelSize: 12 * panel.uiScale
                    wrapMode: Text.WrapAnywhere
                    visible: text !== ""
                }
                Rectangle {
                    Layout.fillWidth: true
                    Layout.topMargin: 10 * panel.uiScale
                    implicitHeight: 1
                    color: "#e0e9ef"
                }
                Text {
                    Layout.topMargin: 8 * panel.uiScale
                    text: "协议规格 · 只读"
                    color: "#294d61"
                    font.pixelSize: 14 * panel.uiScale
                    font.weight: Font.Medium
                }
                // 官方约定不是本次接收测量结果，使用文字而非可编辑/禁用的表单控件。
                Repeater {
                    model: [
                        {
                            key: "transport",
                            label: "传输 / 编码",
                            value: "UDP · H.265 / HEVC"
                        },
                        {
                            key: "format",
                            label: "官方视频规格",
                            value: "1920 × 1080 · 60 帧/秒"
                        },
                        {
                            key: "header",
                            label: "包头格式",
                            value: "8 字节 · 大端"
                        },
                        {
                            key: "packet",
                            label: "UDP 包上限",
                            value: "1400 字节（负载 ≤ 1392 字节）"
                        },
                        {
                            key: "retry",
                            label: "重传机制",
                            value: "不支持"
                        }
                    ]
                    RowLayout {
                        id: specRow
                        required property var modelData
                        Layout.fillWidth: true
                        spacing: 16 * panel.uiScale
                        Text {
                            Layout.preferredWidth: 100 * panel.uiScale
                            Layout.alignment: Qt.AlignTop
                            text: specRow.modelData.label
                            color: "#8b9faa"
                            font.pixelSize: 12 * panel.uiScale
                        }
                        Text {
                            objectName: "videoSpec_" + specRow.modelData.key
                            Layout.fillWidth: true
                            text: specRow.modelData.value
                            color: "#526f80"
                            font.pixelSize: 12 * panel.uiScale
                            wrapMode: Text.WordWrap
                        }
                    }
                }
                Text {
                    Layout.fillWidth: true
                    text: "以上为协议规定，不代表实时检测值。组帧超时、缓存和解码策略由程序管理。"
                    color: "#8b9faa"
                    font.pixelSize: 11 * panel.uiScale
                    wrapMode: Text.WordWrap
                }
            }
        }
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 1
            color: "#e0e9ef"
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 16 * panel.uiScale
            spacing: 10 * panel.uiScale
            Item {
                Layout.fillWidth: true
            }
            VideoButton {
                objectName: "videoStopButton"
                uiScale: panel.uiScale
                text: "停止接收"
                enabled: panel.receiver.running
                onClicked: panel.session.stopVideo()
            }
            VideoButton {
                objectName: "videoApplyButton"
                uiScale: panel.uiScale
                primary: true
                text: "保存并接收"
                enabled: address.text.trim() !== "" && port.acceptableInput
                onClicked: {
                    if (panel.session.configureVideo(address.text, Number(port.text), indexBase.currentIndex))
                        panel.session.startVideo();
                }
            }
        }
    }
}
