pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"

// 客户端菜单的图传分区；草稿仅在主动作提交，菜单关闭不改变监听。
SettingsPage {
    id: panel
    // 由宿主注入 GUI 接收器和 UI 档位；面板不拥有 C++ 对象。
    required property var receiver
    required property var session
    // 用设备 key 保存草稿，异步列表刷新不会把选择悄悄改成第一块 GPU。
    property string draftDecoder: receiver.decoderChoice
    // 仅提供同窗可视容器；现有 VideoView 由 Main 挂入，不拥有接收器或场景图资源。
    readonly property alias previewTarget: preview
    // 解码设备用稳定 key 比较；切页不丢草稿，也不能把旧画面视为新参数已通过。
    readonly property bool hasDraftChanges: address.text !== receiver.bindAddress || Number(port.text) !== receiver.port || indexBase.currentIndex !== receiver.indexBase || draftDecoder !== receiver.decoderChoice
    function decoderIndex() {
        const list = receiver.decoderDevices;
        for (let i = 0; i < list.length; ++i) {
            if (list[i].key === draftDecoder)
                return i;
        }
        return -1;
    }
    // 新比赛会话初始化草稿；同会话重开菜单或切换分类保留编辑，不触碰接收状态。
    function resetDraft() {
        address.text = receiver.bindAddress;
        port.text = String(receiver.port);
        indexBase.currentIndex = receiver.indexBase;
        draftDecoder = receiver.decoderChoice;
        resetScroll();
    }

    title: "确认图传画面"
    eyebrow: "VIDEO LINK / 02"
    description: "配置图传接收，确认画面内容与持续更新状态。"
    formObjectName: "videoFormScroll"
    actionData: [
        VideoButton {
            objectName: "videoApplyButton"
            Layout.fillWidth: true
            uiScale: panel.uiScale
            primary: !panel.check.passed
            text: panel.check.passed ? "重新检查" : "开始接收并检查"
            enabled: address.text.trim() !== "" && port.acceptableInput
            onClicked: {
                if (panel.session.configureVideo(address.text, Number(port.text), indexBase.currentIndex, panel.draftDecoder))
                    panel.session.startVideo();
            }
        },
        VideoButton {
            objectName: "videoStopButton"
            Layout.fillWidth: true
            uiScale: panel.uiScale
            quiet: true
            text: "停止接收"
            enabled: panel.receiver.running
            onClicked: panel.session.stopVideo()
        }
    ]
    Rectangle {
        id: preview
        objectName: "videoSettingsPreview"
        Layout.fillWidth: true
        implicitHeight: Math.max(170 * panel.uiScale, width * 9 / 16)
        radius: 5 * panel.uiScale
        color: "#171b20"
        clip: true
        Text {
            anchors.centerIn: parent
            width: parent.width - 24 * panel.uiScale
            horizontalAlignment: Text.AlignHCenter
            text: "实际接收画面\n" + panel.receiver.status
            font.pixelSize: 13 * panel.uiScale
            color: "#c1c6ce"
            wrapMode: Text.WordWrap
        }
    }

    Text {
        text: "接收配置"
        color: "#23272c"
        font.pixelSize: 16 * panel.uiScale
        font.weight: Font.DemiBold
    }
    GridLayout {
        Layout.fillWidth: true
        columns: 2
        columnSpacing: 18 * panel.uiScale
        rowSpacing: 8 * panel.uiScale
        Text {
            text: "本机监听地址"
            color: "#656d77"
            font.pixelSize: 12 * panel.uiScale
        }
        Text {
            objectName: "videoPortLabel"
            text: "UDP 端口 · 默认 3334"
            color: "#656d77"
            font.pixelSize: 12 * panel.uiScale
        }
        VideoField {
            id: address
            objectName: "videoBindAddress"
            Layout.fillWidth: true
            Layout.preferredWidth: 1
            uiScale: panel.uiScale
            placeholderText: "0.0.0.0"
            readOnly: panel.session.active
            Accessible.name: "本机 UDP 监听地址"
        }
        VideoField {
            id: port
            objectName: "videoPort"
            Layout.fillWidth: true
            Layout.preferredWidth: 1
            uiScale: panel.uiScale
            validator: IntValidator {
                bottom: 1
                top: 65535
            }
            Accessible.name: "UDP 监听端口"
        }
    }
    Text {
        Layout.fillWidth: true
        text: "监听地址填写本机 IPv4；0.0.0.0 表示监听所有本机 IPv4 网卡。端口需与发送目标一致。"
        color: "#737780"
        font.pixelSize: 11 * panel.uiScale
        wrapMode: Text.WordWrap
    }
    Text {
        Layout.topMargin: 12 * panel.uiScale
        text: "视频解码设备"
        color: "#24272b"
        font.pixelSize: 14 * panel.uiScale
        font.weight: Font.Medium
    }
    RowLayout {
        Layout.fillWidth: true
        spacing: 10 * panel.uiScale
        VideoChoice {
            id: decoderChoice
            objectName: "videoDecoderChoice"
            Layout.fillWidth: true
            uiScale: panel.uiScale
            model: panel.receiver.decoderDevices
            currentIndex: panel.decoderIndex()
            placeholderText: panel.receiver.probingDevices ? "正在检测设备…" : "已保存设备暂不可用"
            Accessible.name: "视频解码设备"
            onActivated: {
                if (model[currentIndex].available !== false)
                    panel.draftDecoder = model[currentIndex].key;
                currentIndex = Qt.binding(function () {
                    return panel.decoderIndex();
                });
            }
        }
        VideoButton {
            objectName: "videoDecoderRefresh"
            uiScale: panel.uiScale
            text: panel.receiver.probingDevices ? "检测中…" : "重新检测"
            enabled: !panel.receiver.probingDevices
            onClicked: panel.receiver.refreshDecoderDevices()
        }
    }
    Text {
        objectName: "videoDecoderDescription"
        Layout.fillWidth: true
        text: {
            const index = panel.decoderIndex();
            const entry = index >= 0 ? panel.receiver.decoderDevices[index] : null;
            return (entry ? entry.reason || "" : "设备尚未发现；接收时将回退 CPU。") + "\n自动模式按可用后端尝试；手动设备失败回退 CPU 并保留原因。设备初始化通过后，仍需实际 HEVC 画面验证。";
        }
        color: "#737780"
        font.pixelSize: 11 * panel.uiScale
        wrapMode: Text.WrapAnywhere
    }
    Text {
        Layout.fillWidth: true
        text: "当前实际路径  " + (panel.receiver.diagnostics.decoder || "尚未开始解码") + " · " + (panel.receiver.diagnostics.decoderDevice || "—") + (panel.receiver.diagnostics.decoderOverride ? "\n部署环境变量正在覆盖界面选择：" + panel.receiver.diagnostics.decoderOverride : "")
        color: "#62666e"
        font.pixelSize: 12 * panel.uiScale
        wrapMode: Text.WrapAnywhere
    }
    Text {
        objectName: "videoSettingsError"
        Layout.fillWidth: true
        Layout.topMargin: 6 * panel.uiScale
        text: panel.receiver.error !== "" ? panel.receiver.error : panel.session.error
        color: "#a33d1b"
        font.pixelSize: 12 * panel.uiScale
        wrapMode: Text.WrapAnywhere
        visible: text !== ""
    }
    Rectangle {
        Layout.fillWidth: true
        Layout.topMargin: 10 * panel.uiScale
        implicitHeight: 1
        color: "#d9dce0"
    }
    VideoButton {
        id: protocolDetails
        objectName: "videoProtocolDetails"
        Layout.topMargin: 8 * panel.uiScale
        uiScale: panel.uiScale
        quiet: true
        checkable: true
        text: checked ? "收起高级设置与协议说明" : "高级设置与协议说明"
    }
    ColumnLayout {
        Layout.fillWidth: true
        visible: protocolDetails.checked
        ColumnLayout {
            Layout.fillWidth: true
            Layout.preferredWidth: 2
            spacing: 8 * panel.uiScale
            Text {
                text: "首个分片编号"
                color: "#62666e"
                font.pixelSize: 12 * panel.uiScale
            }
            Rectangle {
                id: indexBase
                objectName: "videoIndexBase"
                // 草稿不会影响接收器，直到保存成功。
                property int currentIndex: 0
                Layout.fillWidth: true
                implicitHeight: 46 * panel.uiScale
                color: "#edeef0"
                radius: 3 * panel.uiScale
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
                                color: segment.checked ? "#c94b17" : "#62666e"
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                            background: Rectangle {
                                color: segment.checked ? "white" : "transparent"
                                radius: 3 * panel.uiScale
                                border.color: segment.activeFocus ? "#c94b17" : segment.checked ? "#c9cdd2" : "transparent"
                            }
                        }
                    }
                }
            }
        }
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
            visible: protocolDetails.checked
            spacing: 16 * panel.uiScale
            Text {
                Layout.preferredWidth: 100 * panel.uiScale
                Layout.alignment: Qt.AlignTop
                text: specRow.modelData.label
                color: "#737780"
                font.pixelSize: 12 * panel.uiScale
            }
            Text {
                objectName: "videoSpec_" + specRow.modelData.key
                Layout.fillWidth: true
                text: specRow.modelData.value
                color: "#62666e"
                font.pixelSize: 12 * panel.uiScale
                wrapMode: Text.WordWrap
            }
        }
    }
    Text {
        Layout.fillWidth: true
        text: "以上为协议规定，不代表实时检测值。组帧超时、缓存和解码策略由程序管理。"
        visible: protocolDetails.checked
        color: "#737780"
        font.pixelSize: 11 * panel.uiScale
        wrapMode: Text.WordWrap
    }
    Text {
        objectName: "videoReceptionStatus"
        Layout.fillWidth: true
        text: panel.receiver.status
        color: "#737982"
        font.pixelSize: 12 * panel.uiScale
    }
    Text {
        objectName: "videoSourceEndpoint"
        Layout.fillWidth: true
        text: "当前发送端  " + (panel.receiver.sourceAddress === "" ? "尚未发现" : (panel.receiver.sourceAddress.indexOf(":") >= 0 ? "[" + panel.receiver.sourceAddress + "]" : panel.receiver.sourceAddress) + " : " + panel.receiver.sourcePort)
        color: "#737982"
        font.pixelSize: 11 * panel.uiScale
        wrapMode: Text.WrapAnywhere
    }
}
