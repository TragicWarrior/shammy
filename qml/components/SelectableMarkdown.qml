import QtQuick

TextEdit {
    id: root
    property bool replyEnabled: false
    signal replyRequested(string snippet)

    property bool markdown: true

    readOnly: true
    selectByMouse: true
    persistentSelection: true
    wrapMode: TextEdit.Wrap
    textFormat: markdown ? TextEdit.MarkdownText : TextEdit.PlainText
    color: Theme.text
    selectedTextColor: Theme.text
    selectionColor: Theme.selection
    font.pixelSize: 15
    activeFocusOnPress: true

    TapHandler {
        acceptedButtons: Qt.RightButton
        onTapped: {
            const t = root.selectedText.trim()
            if (t.length === 0)
                return
            root.replyRequested(t)
        }
    }
}
