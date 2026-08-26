import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Popup {
    id: root
    modal: true
    anchors.centerIn: Overlay.overlay
    width: 480
    padding: 16
    visible: chat.permissionOpen
    closePolicy: Popup.NoAutoClose
    background: Rectangle {
        color: Theme.sidebar
        radius: 16
        border.color: Theme.border
    }

    ColumnLayout {
        width: parent.width
        spacing: 10
        Text {
            text: "Allow MCP tool?"
            color: Theme.text
            font.pixelSize: 16
            font.bold: true
        }
        Text {
            text: chat.permissionServer + " / " + chat.permissionTool
            color: Theme.accent
            font.pixelSize: 13
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }
        ScrollView {
            Layout.fillWidth: true
            Layout.preferredHeight: 140
            TextArea {
                readOnly: true
                text: chat.permissionArgs
                wrapMode: TextEdit.Wrap
                color: Theme.text
                font.family: "monospace"
                font.pixelSize: 12
                background: Rectangle { color: Theme.bg; radius: 6 }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Button { text: "Deny"; onClicked: chat.resolvePermission("deny") }
            Item { Layout.fillWidth: true }
            Button { text: "Once"; onClicked: chat.resolvePermission("once") }
            Button { text: "This chat"; onClicked: chat.resolvePermission("chat") }
            Button { text: "Always"; onClicked: chat.resolvePermission("always") }
        }
    }
}
