import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    property string role: "assistant"
    property string content: ""
    property string reasoning: ""
    property bool streaming: false
    property var parts: []
    property string toolCalls: ""
    property string error: ""
    property string messageId: ""
    property bool isUser: role === "user"
    property bool isTool: role === "tool"
    property bool isCompact: role === "system"
    readonly property bool searchingWeb: !isUser && !isTool
        && chat.toolActivity.length > 0
        && toolCalls.indexOf("web_search") !== -1
    readonly property bool fetchingPage: !isUser && !isTool
        && chat.toolActivity.length > 0
        && toolCalls.indexOf("web_fetch") !== -1
    readonly property bool waitingOnTools: !isUser && !isTool && !isCompact
        && !streaming && chat.streaming && chat.toolActivity.length > 0
        && toolCalls.length > 0
    readonly property string waitCaption: {
        if (root.fetchingPage || (root.waitingOnTools && toolCalls.indexOf("web_fetch") !== -1))
            return "Fetching page…"
        if (root.searchingWeb || (root.waitingOnTools && toolCalls.indexOf("web_search") !== -1))
            return "Searching web…"
        if (root.waitingOnTools)
            return "Using tools…"
        if (root.writingHeavy || root.writingHtml)
            return "Writing artifact…"
        if (root.streaming && (root.reasoning.length > 0 && root.content.length < 24))
            return "Reasoning…"
        if (root.streaming && root.content.length === 0)
            return "Reasoning…"
        if (root.streaming)
            return "Writing…"
        return "Using tools…"
    }
    property bool thinkExpanded: false
    property bool thinkRevealed: false
    property bool toolExpanded: false
    property bool toolRevealed: false
    readonly property bool replyable: !isUser && !isTool && !isCompact && !streaming
    readonly property bool thinkVisible: !isUser && !isTool && !isCompact && reasoning.length > 0
    readonly property bool showThinkBody: thinkVisible && (settings.showReasoning || thinkRevealed)
    readonly property bool showToolBody: isTool && content.length > 0 && (settings.showToolInsights || toolRevealed)
    readonly property bool writingHtml: {
        if (!streaming || isUser || isTool || isCompact)
            return false
        const head = content.substring(0, 1600).toLowerCase()
        return head.indexOf("<artifact") !== -1
            || head.indexOf("<!doctype") !== -1
            || head.indexOf("<html") !== -1
    }
    readonly property bool writingHeavy: {
        if (!streaming || isUser || isTool || isCompact)
            return false
        return writingHtml || content.length > 2500
    }
    width: parent ? parent.width : 600
    height: bubble.height

    function isHeavyMarkup(s, type, language) {
        const lang = String(language || "").toLowerCase()
        if (lang === "md" || lang === "markdown")
            return true
        if (type === "artifact")
            return false
        if (!s || s.length < 1)
            return false
        if (s.length > 2500)
            return true
        const head = s.substring(0, 800).toLowerCase()
        if (head.indexOf("<!doctype") !== -1
                || head.indexOf("<html") !== -1
                || head.indexOf("<artifact") !== -1)
            return true
        if (s.length > 1200 && (head.indexOf("```") !== -1
                || head.indexOf("# ") === 0
                || head.indexOf("\n# ") !== -1))
            return true
        return false
    }

    // Artifacts for this message (store-backed, or in-memory for private chats).
    readonly property var storedArtifacts: {
        if (chat.artifactsRevision < 0)
            return []
        if (root.isUser || root.isTool || root.isCompact || !settings.enableArtifacts)
            return []
        return chat.artifactsForMessage(root.messageId)
    }

    function isStoredArtifactPart(type, language) {
        if (root.storedArtifacts.length === 0)
            return false
        if (type === "artifact")
            return true
        const lang = String(language || "").toLowerCase()
        return type === "code" && (lang === "html" || lang === "htm" || lang === "svg"
                || lang === "javascript" || lang === "js"
                || lang === "md" || lang === "markdown")
    }

    function openSelMenu(snippet) {
        const t = String(snippet).trim()
        if (t.length === 0)
            return
        selMenu.snippet = t
        selMenu.popup()
    }

    ThemedMenu {
        id: selMenu
        property string snippet: ""
        ThemedMenuItem {
            iconKind: "reply"
            text: "Reply"
            visible: root.replyable
            onTriggered: chat.replyToSelection(selMenu.snippet)
        }
        ThemedMenuItem {
            text: "Copy"
            onTriggered: chat.copyText(selMenu.snippet)
        }
    }

    onMessageIdChanged: {
        thinkExpanded = false
        thinkRevealed = false
        toolExpanded = false
        toolRevealed = false
    }

    readonly property string toolTitle: {
        const c = root.content
        if (c.startsWith("[")) {
            const end = c.indexOf("]")
            if (end > 1)
                return "Tool · " + c.substring(1, end)
        }
        return "Tool insights"
    }

    Rectangle {
        id: bubble
        anchors.right: root.isUser ? parent.right : undefined
        anchors.left: root.isUser ? undefined : parent.left
        width: root.isUser ? Math.min(parent.width * 0.72, parent.width) : parent.width
        height: col.height + (root.isUser ? 20 : 0)
        color: root.isUser ? Theme.userBubble : "transparent"
        radius: root.isUser ? 22 : 0
        border.color: "transparent"
        border.width: 0

        Column {
            id: col
            x: root.isUser ? 16 : 0
            y: root.isUser ? 10 : 0
            width: bubble.width - (root.isUser ? 32 : 0)
            spacing: 8

            Rectangle {
                visible: root.isCompact
                width: parent.width
                height: compactCol.height + 20
                radius: 12
                color: Theme.panel
                border.color: Theme.border
                border.width: 1
                Column {
                    id: compactCol
                    x: 14
                    y: 10
                    width: parent.width - 28
                    spacing: 6
                    Text {
                        text: "Compacted history"
                        color: Theme.muted
                        font.pixelSize: 11
                        font.weight: Font.DemiBold
                    }
                    Text {
                        width: parent.width
                        text: root.content
                        color: Theme.text
                        wrapMode: Text.Wrap
                        font.pixelSize: 13
                    }
                }
            }

            Row {
                visible: !root.isUser && !root.isTool && !root.isCompact
                spacing: 10
                Rectangle {
                    width: 28
                    height: 28
                    radius: 14
                    color: Theme.hover
                    border.color: Theme.border
                    border.width: 1
                    Text {
                        anchors.centerIn: parent
                        text: "☘"
                        color: "#22c55e"
                        font.pixelSize: 14
                    }
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: "Shammy"
                    color: Theme.text
                    font.pixelSize: 13
                    font.weight: Font.DemiBold
                }
            }

            Text {
                visible: root.thinkVisible && !root.showThinkBody
                text: "Show reasoning insights"
                color: Theme.muted
                font.pixelSize: 12
                MouseArea {
                    anchors.fill: parent
                    anchors.margins: -4
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.thinkRevealed = true
                }
            }

            InsightBlock {
                visible: root.showThinkBody
                width: parent.width
                title: "Thought"
                body: root.reasoning
                expanded: root.thinkExpanded
                hideable: !settings.showReasoning
                onExpandToggled: root.thinkExpanded = !root.thinkExpanded
                onHideRequested: root.thinkRevealed = false
            }

            Text {
                visible: root.streaming && root.content.length > 0 && !root.isTool && !root.writingHeavy
                width: parent.width
                text: root.content
                color: Theme.text
                wrapMode: Text.Wrap
                textFormat: Text.PlainText
                font.pixelSize: 15
                lineHeight: 1.45
            }

            Repeater {
                model: {
                    if (root.isCompact || root.isTool)
                        return []
                    const src = root.parts
                    const out = []
                    for (let i = 0; i < src.length; ++i) {
                        const p = src[i]
                        if (root.isStoredArtifactPart(p.type, p.language))
                            continue
                        out.push(p)
                    }
                    return out
                }
                delegate: Loader {
                    required property var modelData
                    width: col.width
                    height: item ? Math.max(item.implicitHeight, item.height) : 0
                    property var part: modelData
                    property bool showOpen: modelData.type === "artifact"
                    sourceComponent: {
                        if (modelData.type === "table")
                            return tableComp
                        if (modelData.type === "artifact" && settings.enableArtifacts)
                            return artComp
                        if (root.isHeavyMarkup(modelData.text, modelData.type, modelData.language))
                            return artComp
                        if (modelData.type === "code")
                            return codeComp
                        if (root.replyable || root.isUser)
                            return selectableComp
                        return textComp
                    }
                }
            }

            Repeater {
                model: root.storedArtifacts
                delegate: Loader {
                    required property var modelData
                    width: col.width
                    height: item ? Math.max(item.implicitHeight, item.height) : 0
                    property var part: modelData
                    property bool showOpen: true
                    sourceComponent: artComp
                }
            }

            Text {
                visible: root.isTool && root.content.length > 0 && !root.showToolBody
                text: "Show tool insights"
                color: Theme.muted
                font.pixelSize: 12
                MouseArea {
                    anchors.fill: parent
                    anchors.margins: -4
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.toolRevealed = true
                }
            }

            InsightBlock {
                visible: root.showToolBody
                width: parent.width
                title: root.toolTitle
                body: root.content
                expanded: root.toolExpanded
                hideable: !settings.showToolInsights
                monospace: true
                onExpandToggled: root.toolExpanded = !root.toolExpanded
                onHideRequested: root.toolRevealed = false
            }

            Text {
                visible: root.toolCalls.length > 0 && !root.isUser && !root.isTool && !root.searchingWeb
                width: parent.width
                text: "Used tools"
                color: Theme.muted
                font.pixelSize: 12
            }

            Text {
                visible: root.error.length > 0
                width: parent.width
                text: root.error
                color: Theme.danger
                wrapMode: Text.Wrap
                font.pixelSize: 13
            }

            Row {
                id: waitRow
                visible: !root.isUser && (root.streaming || root.searchingWeb || root.waitingOnTools)
                spacing: 8
                Text {
                    text: ["⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏"][spinFrame]
                    color: Theme.text
                    font.pixelSize: 14
                    font.family: "monospace"
                    property int spinFrame: 0
                    Timer {
                        interval: 80
                        running: waitRow.visible
                        repeat: true
                        onTriggered: parent.spinFrame = (parent.spinFrame + 1) % 10
                    }
                }
                Text {
                    text: root.waitCaption
                    color: Theme.muted
                    font.pixelSize: 13
                    anchors.verticalCenter: parent.verticalCenter
                }
            }

            Row {
                visible: root.isUser && !root.streaming
                spacing: 10
                anchors.right: parent.right
                Text {
                    text: "Copy"
                    color: Theme.muted
                    font.pixelSize: 12
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: chat.copyText(root.content)
                    }
                }
                Text {
                    text: "Edit"
                    color: Theme.muted
                    font.pixelSize: 12
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: chat.editAndResend(root.messageId, root.content)
                    }
                }
            }
            Row {
                visible: !root.isUser && !root.isTool && !root.isCompact && !root.streaming && root.content.length > 0
                spacing: 10
                Text {
                    text: "Regenerate"
                    color: Theme.muted
                    font.pixelSize: 12
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: chat.regenerate()
                    }
                }
            }
        }
    }

    Component {
        id: textComp
        Text {
            width: col.width
            text: part && part.text ? part.text : ""
            color: Theme.text
            wrapMode: Text.Wrap
            textFormat: Text.MarkdownText
            font.pixelSize: 15
            lineHeight: 1.45
        }
    }
    Component {
        id: selectableComp
        SelectableMarkdown {
            width: col.width
            text: part && part.text ? part.text : ""
            markdown: !root.isUser
            replyEnabled: root.replyable
            onReplyRequested: function(snippet) { root.openSelMenu(snippet) }
        }
    }
    Component {
        id: tableComp
        TableBlock {
            width: col.width
            json: part && part.text ? part.text : ""
        }
    }
    Component {
        id: codeComp
        CodeBlock {
            width: col.width
            language: part && part.language ? part.language : ""
            code: part && part.text ? part.text : ""
            replyEnabled: root.replyable
            onReplyRequested: function(snippet) { root.openSelMenu(snippet) }
        }
    }
    Component {
        id: artComp
        Column {
            width: col.width
            spacing: 6
            property var part: parent && parent.part ? parent.part : null
            property bool showOpen: parent && parent.showOpen !== undefined ? parent.showOpen : true
            property bool revealed: settings.showArtifactInsights
            readonly property string artTitle: (part && part.title) ? part.title : ((part && part.identifier) ? part.identifier : "Artifact")
            Rectangle {
                visible: parent.showOpen
                width: parent.width
                height: visible ? 40 : 0
                radius: 10
                color: Theme.panel
                border.color: Theme.border
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.left: parent.left
                    anchors.leftMargin: 12
                    anchors.right: openHint.left
                    anchors.rightMargin: 8
                    text: artTitle
                    color: Theme.text
                    font.pixelSize: 13
                    elide: Text.ElideRight
                }
                Text {
                    id: openHint
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.right: parent.right
                    anchors.rightMargin: 12
                    text: "Open"
                    color: Theme.muted
                    font.pixelSize: 12
                }
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        const id = (part && part.identifier) ? String(part.identifier) : ""
                        chat.openArtifact(id)
                    }
                }
            }
            Text {
                visible: !parent.revealed
                text: "Show artifact insights"
                color: Theme.muted
                font.pixelSize: 12
                MouseArea {
                    anchors.fill: parent
                    anchors.margins: -4
                    cursorShape: Qt.PointingHandCursor
                    onClicked: parent.parent.revealed = true
                }
            }
            InsightBlock {
                visible: parent.revealed
                width: parent.width
                title: "Artifact"
                body: part && part.text ? part.text : ""
                hideable: !settings.showArtifactInsights
                monospace: true
                onHideRequested: parent.revealed = false
            }
        }
    }
}
