import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

Rectangle {
    id: root
    z: 10
    signal settingsRequested()
    color: Theme.composer
    radius: Theme.radiusLg
    border.color: Theme.border
    border.width: 1
    height: Math.max(52, col.height + 16)

    property int slashIndex: 0
    property var slashMatches: []

    function refreshSlash() {
        const matches = chat.matchingSlashCommands(input.text)
        slashMatches = matches
        if (slashIndex >= matches.length)
            slashIndex = 0
        if (matches.length > 0 && input.activeFocus)
            slashMenu.open()
        else
            slashMenu.close()
    }

    function applySlash(cmd, sendNow) {
        if (!cmd)
            return
        const needsSpace = !!(cmd.args && cmd.args.length) && !sendNow
        input.text = "/" + cmd.name + (needsSpace ? " " : "")
        chat.composerText = input.text
        input.cursorPosition = input.text.length
        if (sendNow) {
            chat.send()
            input.text = ""
        }
        slashMenu.close()
    }

    function placeSlashMenu() {
        const overlay = Overlay.overlay
        if (!overlay)
            return
        const p = input.mapToItem(overlay, 0, 0)
        slashMenu.x = p.x
        slashMenu.y = p.y - slashMenu.implicitHeight - 8
    }

    Popup {
        id: slashMenu
        parent: Overlay.overlay
        padding: 6
        modal: false
        focus: false
        closePolicy: Popup.CloseOnEscape
        background: Rectangle {
            color: Theme.sidebar
            radius: 12
            border.color: Theme.border
            border.width: 1
        }
        onAboutToShow: root.placeSlashMenu()
        onOpened: root.placeSlashMenu()
        onImplicitHeightChanged: root.placeSlashMenu()
        Column {
            spacing: 2
            Repeater {
                model: root.slashMatches
                Rectangle {
                    required property int index
                    required property var modelData
                    width: 280
                    height: 44
                    radius: 8
                    color: {
                        if (index === root.slashIndex)
                            return Theme.selected
                        return rowHover.containsMouse ? Theme.hover : "transparent"
                    }
                    Column {
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.left: parent.left
                        anchors.leftMargin: 10
                        anchors.right: parent.right
                        anchors.rightMargin: 10
                        spacing: 1
                        Text {
                            text: "/" + modelData.name + (modelData.args && modelData.args.length ? (" " + modelData.args) : "")
                            color: Theme.text
                            font.pixelSize: 13
                        }
                        Text {
                            width: parent.width
                            text: modelData.help
                            color: Theme.muted
                            font.pixelSize: 11
                            elide: Text.ElideRight
                        }
                    }
                    MouseArea {
                        id: rowHover
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onEntered: root.slashIndex = index
                        onClicked: root.applySlash(modelData, false)
                    }
                }
            }
        }
    }

    FileDialog {
        id: fileDialog
        fileMode: FileDialog.OpenFiles
        onAccepted: {
            for (let i = 0; i < selectedFiles.length; ++i)
                chat.attachFile(selectedFiles[i])
        }
    }

    Column {
        id: col
        x: 12
        y: 8
        width: parent.width - 24
        spacing: 6

        Rectangle {
            visible: chat.replyQuote.length > 0
            width: parent.width
            radius: 10
            color: Theme.panel
            border.color: Theme.border
            border.width: 1
            height: quoteInner.height + 16
            Rectangle {
                width: 3
                radius: 1.5
                color: Theme.muted
                anchors.left: parent.left
                anchors.leftMargin: 8
                anchors.top: parent.top
                anchors.topMargin: 8
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 8
            }
            Column {
                id: quoteInner
                x: 18
                y: 8
                width: parent.width - 44
                spacing: 2
                Text {
                    text: "Reply"
                    color: Theme.muted
                    font.pixelSize: 11
                    font.weight: Font.DemiBold
                }
                Text {
                    width: parent.width
                    text: chat.replyQuote
                    color: Theme.text
                    font.pixelSize: 12
                    wrapMode: Text.Wrap
                    maximumLineCount: 3
                    elide: Text.ElideRight
                }
            }
            Text {
                anchors.right: parent.right
                anchors.rightMargin: 8
                anchors.top: parent.top
                anchors.topMargin: 6
                text: "×"
                color: quoteClose.containsMouse ? Theme.text : Theme.muted
                font.pixelSize: 16
                MouseArea {
                    id: quoteClose
                    anchors.fill: parent
                    anchors.margins: -6
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: chat.clearReplyQuote()
                }
            }
        }

        Flow {
            width: parent.width
            spacing: 6
            visible: chat.pendingAttachments.length > 0
            Repeater {
                model: chat.pendingAttachments
                Rectangle {
                    required property int index
                    required property string modelData
                    radius: 8
                    color: Theme.panel
                    height: 24
                    width: lab.width + 18
                    Text {
                        id: lab
                        anchors.centerIn: parent
                        text: modelData.split(" — ")[0]
                        color: Theme.muted
                        font.pixelSize: 11
                    }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: chat.removeAttachment(index)
                    }
                }
            }
        }

        RowLayout {
            width: parent.width
            spacing: 8

            Rectangle {
                Layout.preferredWidth: 32
                Layout.preferredHeight: 32
                Layout.alignment: Qt.AlignVCenter
                implicitWidth: 32
                implicitHeight: 32
                radius: 16
                color: plusHover.containsMouse || plusMenu.visible ? Theme.hover : "transparent"
                border.color: Theme.border
                Text {
                    anchors.centerIn: parent
                    text: "+"
                    color: Theme.text
                    font.pixelSize: 18
                }
                MouseArea {
                    id: plusHover
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: plusMenu.open()
                }
                ThemedMenu {
                    id: plusMenu
                    y: -implicitHeight - 8
                    x: 0
                    ThemedMenuItem {
                        iconKind: "clip"
                        text: "Add files"
                        onTriggered: fileDialog.open()
                    }
                    MenuSeparator {
                        padding: 6
                        contentItem: Rectangle {
                            implicitHeight: 1
                            color: Theme.hairline
                        }
                    }
                    ThemedMenuItem {
                        iconKind: "globe"
                        text: chat.webSearchAvailable ? "Web search" : "Web search (set API key in Settings)"
                        checkable: chat.webSearchAvailable
                        checked: chat.webSearchAvailable && chat.webSearch
                        onTriggered: {
                            if (chat.webSearchAvailable)
                                chat.webSearch = !chat.webSearch
                            else
                                root.settingsRequested()
                        }
                    }
                }
            }

            TextArea {
                id: input
                Layout.fillWidth: true
                Layout.minimumWidth: 64
                Layout.preferredHeight: Math.min(160, Math.max(32, contentHeight + 8))
                Layout.alignment: Qt.AlignVCenter
                placeholderText: settings.modelVision ? "Ask anything, or paste an image" : "Ask anything"
                wrapMode: TextEdit.Wrap
                color: Theme.text
                placeholderTextColor: Theme.muted
                font.pixelSize: 15
                background: Item {}
                selectByMouse: true
                verticalAlignment: TextEdit.AlignVCenter
                Keys.priority: Keys.BeforeItem
                Keys.onPressed: function(event) {
                    const pasteKey = event.matches(StandardKey.Paste)
                        || (event.key === Qt.Key_V && (event.modifiers & Qt.ControlModifier)
                            && !(event.modifiers & Qt.ShiftModifier)
                            && !(event.modifiers & Qt.AltModifier))
                    if (pasteKey && chat.pasteClipboardImage()) {
                        event.accepted = true
                        return
                    }
                    if (slashMenu.visible && root.slashMatches.length > 0) {
                        if (event.key === Qt.Key_Down) {
                            root.slashIndex = Math.min(root.slashIndex + 1, root.slashMatches.length - 1)
                            event.accepted = true
                            return
                        }
                        if (event.key === Qt.Key_Up) {
                            root.slashIndex = Math.max(root.slashIndex - 1, 0)
                            event.accepted = true
                            return
                        }
                        if (event.key === Qt.Key_Tab && !(event.modifiers & Qt.ShiftModifier)) {
                            root.applySlash(root.slashMatches[root.slashIndex], false)
                            event.accepted = true
                            return
                        }
                        if (event.key === Qt.Key_Escape) {
                            slashMenu.close()
                            event.accepted = true
                            return
                        }
                        if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter)
                                && !(event.modifiers & Qt.ShiftModifier)) {
                            root.applySlash(root.slashMatches[root.slashIndex], true)
                            event.accepted = true
                            return
                        }
                    }
                    if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                        if (event.modifiers & Qt.ShiftModifier)
                            return
                        event.accepted = true
                        if (chat.compacting)
                            return
                        chat.composerText = input.text
                        chat.send()
                        input.text = ""
                    }
                }
                onTextChanged: {
                    chat.composerText = text
                    root.refreshSlash()
                }
                onActiveFocusChanged: root.refreshSlash()
            }

            Text {
                visible: settings.modelTools
                Layout.alignment: Qt.AlignVCenter
                Layout.preferredWidth: implicitWidth
                text: (mcp.toolCount + (settings.modelTools ? 1 : 0)
                       + (chat.webSearchAvailable && chat.webSearch ? 1 : 0)) + " tools"
                color: Theme.muted
                font.pixelSize: 11
            }

            Rectangle {
                visible: chat.webSearchAvailable
                Layout.preferredWidth: webLab.implicitWidth + 20
                Layout.preferredHeight: 32
                Layout.alignment: Qt.AlignVCenter
                implicitWidth: webLab.implicitWidth + 20
                implicitHeight: 32
                radius: 16
                color: chat.webSearch ? Theme.hover : "transparent"
                border.color: Theme.border
                border.width: 1
                Text {
                    id: webLab
                    anchors.centerIn: parent
                    text: "Web"
                    color: chat.webSearch ? Theme.text : Theme.muted
                    font.pixelSize: 12
                }
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: chat.webSearch = !chat.webSearch
                }
            }

            Rectangle {
                id: thinkBtn
                visible: settings.modelThinking
                Layout.preferredWidth: thinkLab.implicitWidth + 20
                Layout.preferredHeight: 32
                Layout.alignment: Qt.AlignVCenter
                implicitWidth: thinkLab.implicitWidth + 20
                implicitHeight: 32
                radius: 16
                color: chat.thinkingMode.length > 0 ? Theme.hover : "transparent"
                border.color: Theme.border
                border.width: 1
                Text {
                    id: thinkLab
                    anchors.centerIn: parent
                    text: chat.thinkingMode.length > 0 ? ("Think · " + chat.thinkingModeLabel) : "Think"
                    color: chat.thinkingMode.length > 0 ? Theme.text : Theme.muted
                    font.pixelSize: 12
                }
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: thinkMenu.open()
                }
                Popup {
                    id: thinkMenu
                    y: -implicitHeight - 8
                    x: thinkBtn.width - width
                    padding: 6
                    modal: false
                    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
                    background: Rectangle {
                        color: Theme.sidebar
                        radius: 12
                        border.color: Theme.border
                    }
                    Column {
                        spacing: 2
                        Repeater {
                            model: [
                                { value: "", label: "Off" },
                                { value: "low", label: "Low" },
                                { value: "medium", label: "Medium" },
                                { value: "high", label: "High" }
                            ]
                            Rectangle {
                                required property var modelData
                                width: 140
                                height: 32
                                radius: 8
                                color: {
                                    const cur = chat.thinkingMode
                                    const on = (modelData.value === cur) || (modelData.value === "" && cur.length === 0)
                                    if (on)
                                        return Theme.selected
                                    return modeHover.containsMouse ? Theme.hover : "transparent"
                                }
                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    anchors.left: parent.left
                                    anchors.leftMargin: 10
                                    text: modelData.label
                                    color: Theme.text
                                    font.pixelSize: 13
                                }
                                MouseArea {
                                    id: modeHover
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        chat.thinkingMode = modelData.value
                                        thinkMenu.close()
                                    }
                                }
                            }
                        }
                    }
                }
            }

            Rectangle {
                Layout.preferredWidth: 32
                Layout.preferredHeight: 32
                Layout.alignment: Qt.AlignVCenter
                implicitWidth: 32
                implicitHeight: 32
                radius: 16
                color: {
                    if (chat.streaming || chat.compacting)
                        return Theme.sendBg
                    return (input.text.trim().length > 0 || chat.pendingAttachments.length > 0)
                           ? Theme.sendBg : Theme.sendDisabled
                }
                Text {
                    anchors.centerIn: parent
                    text: (chat.streaming || chat.compacting) ? "■" : "↑"
                    color: Theme.sendFg
                    font.pixelSize: (chat.streaming || chat.compacting) ? 10 : 16
                    font.bold: true
                }
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        if (chat.streaming || chat.compacting)
                            chat.stop()
                        else {
                            chat.composerText = input.text
                            chat.send()
                            input.text = ""
                        }
                    }
                }
            }
        }
    }

    Connections {
        target: chat
        function onComposerTextChanged() {
            if (input.text !== chat.composerText && !input.activeFocus)
                input.text = chat.composerText
        }
        function onReplyQuoteChanged() {
            if (chat.replyQuote.length > 0)
                input.forceActiveFocus()
        }
    }
}
