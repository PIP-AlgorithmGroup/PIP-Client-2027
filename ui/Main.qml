import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    visible: true
    width: 1280
    height: 720
    minimumWidth: 960
    minimumHeight: 540
    title: "PIP-Client"
    color: "#101419"

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 16

        Label {
            text: "PIP-Client"
            color: "#f2f5f7"
            font.pixelSize: 28
            font.bold: true
        }

        Label {
            text: "Qt 6 / QML 基础工程"
            color: "#9ba7b2"
            font.pixelSize: 16
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: "#171d24"
            border.color: "#2d3945"
            radius: 6

            Label {
                anchors.centerIn: parent
                text: "比赛主界面占位区域"
                color: "#c9d2da"
                font.pixelSize: 22
            }
        }
    }
}
