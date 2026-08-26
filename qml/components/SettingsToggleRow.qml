import QtQuick
import QtQuick.Layouts

RowLayout {
    id: root
    width: parent ? parent.width : 400
    spacing: 12

    property string title: ""
    property string help: ""
    property bool checked: false
    signal toggled()

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 2
        Text {
            text: root.title
            color: Theme.text
            font.pixelSize: 14
        }
        Text {
            visible: root.help.length > 0
            text: root.help
            color: Theme.muted
            font.pixelSize: 12
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }
    }
    Rectangle {
        width: 44
        height: 26
        radius: 13
        color: root.checked ? Theme.text : Theme.hover
        Rectangle {
            width: 22
            height: 22
            radius: 11
            y: 2
            x: root.checked ? parent.width - width - 2 : 2
            color: root.checked ? Theme.bg : Theme.muted
        }
        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: root.toggled()
        }
    }
}
