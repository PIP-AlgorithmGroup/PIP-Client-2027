import QtQuick
import QtQuick.Window

Window {
    // 仅注入通用入口，暂不绑定连接状态或任何比赛字段；由应用持有该对象。
    required property QtObject dataPipeline

    visible: true
    width: 1280
    height: 720
    title: "PIP-Client"
}
