import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
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
    // Live tool text belongs only to the assistant message running this round.
    readonly property bool ownsToolActivity: chat.toolActivityMessageId.length > 0
        && root.messageId === chat.toolActivityMessageId
    readonly property bool searchingWeb: !isUser && !isTool
        && ownsToolActivity
        && chat.toolActivity.length > 0
        && toolCalls.indexOf("web_search") !== -1
    readonly property bool fetchingPage: !isUser && !isTool
        && ownsToolActivity
        && chat.toolActivity.length > 0
        && toolCalls.indexOf("web_fetch") !== -1
    readonly property bool waitingOnTools: !isUser && !isTool && !isCompact
        && ownsToolActivity
        && !streaming && chat.streaming && chat.toolActivity.length > 0
        && toolCalls.length > 0
    // imageRevision changes when an upscale is saved, so this bubble lists it.
    readonly property var generatedImage: {
        const rev = chat.imageRevision
        return (rev >= 0 && root.isTool && root.content.length > 0)
                ? chat.imageForMessage(root.content) : ({})
    }
    readonly property bool hasImage: generatedImage.path !== undefined
    readonly property bool upscalingThis: chat.imageUpscaleRunning
        && chat.imageActivityJobId === (root.generatedImage.jobId ?? "")
    readonly property string waitCaption: {
        if (root.fetchingPage || (root.waitingOnTools && toolCalls.indexOf("web_fetch") !== -1))
            return "Fetching page…"
        if (root.searchingWeb || (root.waitingOnTools && toolCalls.indexOf("web_search") !== -1))
            return "Searching web…"
        if (root.waitingOnTools && toolCalls.indexOf("generate_image") !== -1)
            return chat.toolActivity.length > 0 ? chat.toolActivity : "Drawing…"
        if (root.waitingOnTools)
            return "Using tools…"
        if (root.writingHtml)
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
    function looksLikeHtml(s) {
        const body = String(s || "").toLowerCase()
        return body.indexOf("<artifact") !== -1
            || body.indexOf("<!doctype") !== -1
            || body.indexOf("<html") !== -1
            || body.indexOf("<svg") !== -1
    }
    readonly property bool writingHtml: streaming && !isUser && !isTool && !isCompact
        && looksLikeHtml(content)
    width: parent ? parent.width : 600
    height: bubble.height

    function isHeavyMarkup(s, type, language) {
        const lang = String(language || "").toLowerCase()
        if (lang === "html" || lang === "htm" || lang === "svg"
                || lang === "javascript" || lang === "js")
            return true
        if (type === "artifact" || lang === "md" || lang === "markdown")
            return false
        return looksLikeHtml(s)
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
        // A missing list must not throw on .length during delegate teardown.
        const arts = root.storedArtifacts
        if (!arts || arts.length === 0)
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
                visible: root.streaming && root.content.length > 0 && !root.isTool && !root.writingHtml
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
                    // parts is a var, so a dying row can set it to undefined.
                    const src = root.parts ?? []
                    const out = []
                    for (let i = 0; i < src.length; ++i) {
                        const p = src[i]
                        if (!p)
                            continue
                        if (root.isStoredArtifactPart(p.type, p.language))
                            continue
                        out.push(p)
                    }
                    return out
                }
                delegate: Loader {
                    required property var modelData
                    width: col.width
                    // Loader writes this height onto the item, so reading
                    // item.height back is a loop. implicitHeight is the content size.
                    height: item ? item.implicitHeight : 0
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
                // Same teardown: the list binding must not be undefined.
                model: root.storedArtifacts ?? []
                delegate: Loader {
                    required property var modelData
                    width: col.width
                    // Loader writes this height onto the item, so reading
                    // item.height back is a loop. implicitHeight is the content size.
                    height: item ? item.implicitHeight : 0
                    property var part: modelData
                    property bool showOpen: true
                    sourceComponent: artComp
                }
            }

            Column {
                id: imageColumn
                visible: root.isTool && root.hasImage
                width: parent.width
                spacing: 6

                Image {
                    id: chatImage
                    width: Math.min(parent.width, 640)
                    // An empty record omits width, height and url. Assigning
                    // undefined to those typed properties warns on every bubble.
                    height: {
                        const w = root.generatedImage.width ?? 0
                        const h = root.generatedImage.height ?? 0
                        if (w <= 0)
                            return 0
                        return Math.round(width * h / w)
                    }
                    source: root.generatedImage.url ?? ""
                    fillMode: Image.PreserveAspectFit
                    asynchronous: true
                    sourceSize.width: 1280
                    anchors.horizontalCenter: parent.horizontalCenter

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            viewerLoader.active = true
                            viewerLoader.item.open()
                        }
                    }
                }
                Loader {
                    id: viewerLoader
                    active: false
                    sourceComponent: Component {
                        ImageViewer {
                            // Built only when the viewer opens. Same empty-record guard.
                            path: root.generatedImage.path ?? ""
                            source: root.generatedImage.url ?? ""
                            prompt: root.generatedImage.prompt ?? ""
                            imageWidth: root.generatedImage.width ?? 0
                            imageHeight: root.generatedImage.height ?? 0
                        }
                    }
                }

                Text {
                    width: parent.width
                    // Text.text is a string. An empty record has no prompt.
                    text: root.generatedImage.prompt ?? ""
                    color: Theme.muted
                    font.pixelSize: 12
                    wrapMode: Text.Wrap
                }

                Text {
                    // Same progress strings generation shows (queue, then Drawing…).
                    width: parent.width
                    visible: chat.imageActivityJobId === (root.generatedImage.jobId ?? "")
                             && chat.imageActivity.length > 0
                    text: chat.imageActivity
                    color: Theme.muted
                    font.pixelSize: 12
                    wrapMode: Text.Wrap
                }

                Row {
                    spacing: 16
                    Text {
                        text: "Save as…"
                        color: Theme.muted
                        font.pixelSize: 12
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                // Start on a dated file name, not on a bare folder.
                                saveAsDialog.selectedFile = chat.suggestedImageSaveUrl()
                                saveAsDialog.open()
                            }
                        }
                    }
                    Text {
                        text: "Copy"
                        color: Theme.muted
                        font.pixelSize: 12
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                const ok = chat.copyImageToClipboard(root.generatedImage.path ?? "")
                                statusText.text = ok ? "Copied" : "Could not copy"
                                statusText.visible = true
                                statusTimer.start()
                            }
                        }
                    }
                    Text {
                        // Hide this factor once it is saved or currently running.
                        visible: (root.generatedImage.offerScale2 ?? false)
                            && !(root.upscalingThis && chat.imageUpscaleScale === 2)
                        text: "Upscale 2×"
                        color: Theme.muted
                        font.pixelSize: 12
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: chat.upscaleImage(root.generatedImage.jobId ?? "", 2)
                        }
                    }
                    Text {
                        visible: (root.generatedImage.offerScale4 ?? false)
                            && !(root.upscalingThis && chat.imageUpscaleScale === 4)
                        text: "Upscale 4×"
                        color: Theme.muted
                        font.pixelSize: 12
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: chat.upscaleImage(root.generatedImage.jobId ?? "", 4)
                        }
                    }
                    Text {
                        id: statusText
                        visible: false
                        color: Theme.muted
                        font.pixelSize: 12
                    }
                    Timer {
                        id: statusTimer
                        interval: 2000
                        onTriggered: statusText.visible = false
                    }
                }

                FileDialog {
                    id: saveAsDialog
                    fileMode: FileDialog.SaveFile
                    defaultSuffix: "png"
                    nameFilters: ["PNG image (*.png)"]
                    onAccepted: {
                        const ok = chat.saveImageAs(root.generatedImage.path ?? "", selectedFile)
                        statusText.text = ok ? "Saved" : "Could not save"
                        statusText.visible = true
                        statusTimer.start()
                    }
                }

                // Each upscale stays under the original. No upscale buttons here.
                Repeater {
                    model: root.generatedImage.upscales ?? []
                    delegate: Column {
                        id: upscaleRow
                        required property var modelData
                        width: imageColumn.width
                        spacing: 6

                        Text {
                            width: parent.width
                            text: upscaleRow.modelData.label ?? ""
                            color: Theme.muted
                            font.pixelSize: 12
                            wrapMode: Text.Wrap
                        }
                        Image {
                            width: Math.min(parent.width, 640)
                            // A row with no metadata must not divide or assign undefined.
                            height: {
                                const w = upscaleRow.modelData.width ?? 0
                                const h = upscaleRow.modelData.height ?? 0
                                if (w <= 0)
                                    return 0
                                return Math.round(width * h / w)
                            }
                            source: upscaleRow.modelData.url ?? ""
                            fillMode: Image.PreserveAspectFit
                            asynchronous: true
                            sourceSize.width: 1280
                            anchors.horizontalCenter: parent.horizontalCenter

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    rowViewer.active = true
                                    rowViewer.item.open()
                                }
                            }
                        }
                        Loader {
                            id: rowViewer
                            active: false
                            sourceComponent: Component {
                                ImageViewer {
                                    path: upscaleRow.modelData.path ?? ""
                                    source: upscaleRow.modelData.url ?? ""
                                    prompt: upscaleRow.modelData.prompt ?? ""
                                    imageWidth: upscaleRow.modelData.width ?? 0
                                    imageHeight: upscaleRow.modelData.height ?? 0
                                }
                            }
                        }
                        Row {
                            spacing: 16
                            Text {
                                text: "Save as…"
                                color: Theme.muted
                                font.pixelSize: 12
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        // Same dated name the original Save as uses.
                                        rowSave.selectedFile = chat.suggestedImageSaveUrl()
                                        rowSave.open()
                                    }
                                }
                            }
                            Text {
                                text: "Copy"
                                color: Theme.muted
                                font.pixelSize: 12
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        const ok = chat.copyImageToClipboard(
                                            upscaleRow.modelData.path ?? "")
                                        rowStatus.text = ok ? "Copied" : "Could not copy"
                                        rowStatus.visible = true
                                        rowStatusTimer.start()
                                    }
                                }
                            }
                            Text {
                                id: rowStatus
                                visible: false
                                color: Theme.muted
                                font.pixelSize: 12
                            }
                            Timer {
                                id: rowStatusTimer
                                interval: 2000
                                onTriggered: rowStatus.visible = false
                            }
                        }
                        FileDialog {
                            id: rowSave
                            fileMode: FileDialog.SaveFile
                            defaultSuffix: "png"
                            nameFilters: ["PNG image (*.png)"]
                            onAccepted: {
                                const ok = chat.saveImageAs(
                                    upscaleRow.modelData.path ?? "", selectedFile)
                                rowStatus.text = ok ? "Saved" : "Could not save"
                                rowStatus.visible = true
                                rowStatusTimer.start()
                            }
                        }
                    }
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
                    font.family: Theme.mono
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
        Item {
            width: col.width
            // Text.implicitHeight follows the height Loader writes onto the
            // Text when lineHeight is set, which loops. The Loader writes
            // this wrapper's height instead, so the Text keeps its own size.
            implicitHeight: label.implicitHeight
            height: implicitHeight
            Text {
                id: label
                width: parent.width
                text: part && part.text ? part.text : ""
                color: Theme.text
                wrapMode: Text.Wrap
                textFormat: Text.MarkdownText
                font.pixelSize: 15
                lineHeight: 1.45
            }
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
