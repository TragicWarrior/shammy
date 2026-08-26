import QtQuick
import QtQuick.Controls

Rectangle {
    id: root
    property string language: ""
    property string code: ""
    property bool replyEnabled: false
    signal replyRequested(string snippet)
    color: Theme.codeBg
    radius: 12
    border.color: Theme.hairline
    clip: true
    implicitHeight: col.height + 14
    height: implicitHeight
    width: parent ? parent.width : 400

    Column {
        id: col
        width: parent.width - 20
        x: 10
        y: 8
        spacing: 4
        Row {
            width: parent.width
            Text {
                text: root.language.length ? root.language : "code"
                color: Theme.muted
                font.pixelSize: 11
            }
            Item { width: parent.width - copyLab.width - 80; height: 1 }
            Text {
                id: copyLab
                text: "Copy"
                color: copyMa.containsMouse ? Theme.text : Theme.muted
                font.pixelSize: 11
                MouseArea {
                    id: copyMa
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        textArea.selectAll()
                        textArea.copy()
                        textArea.deselect()
                        copyLab.text = "Copied"
                    }
                }
            }
        }
        Item {
            width: parent.width
            height: textArea.implicitHeight
            TextArea {
                id: textArea
                width: parent.width
                height: implicitHeight
                text: root.code
                readOnly: true
                wrapMode: root.language === "table" ? TextEdit.NoWrap : TextEdit.Wrap
                color: Theme.text
                selectedTextColor: Theme.text
                selectionColor: Theme.selection
                font.family: "monospace"
                font.pixelSize: 13
                background: Item {}
                selectByMouse: true
                persistentSelection: true
            }
            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.RightButton
                enabled: root.replyEnabled
                onClicked: {
                    const t = textArea.selectedText.trim()
                    if (t.length === 0)
                        return
                    root.replyRequested(t)
                }
            }
        }
    }
}
