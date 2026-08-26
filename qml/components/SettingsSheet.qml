import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

Popup {
    id: root
    modal: true
    width: Math.min(860, parent.width - 32)
    height: Math.min(640, parent.height - 32)
    anchors.centerIn: Overlay.overlay
    padding: 0
    background: Rectangle {
        color: Theme.bg
        radius: 16
        border.color: Theme.border
    }

    property int category: 0
    readonly property var categories: ["General", "Models", "MCP", "Advanced"]

    ListModel { id: draftBackends }
    ListModel { id: draftMcp }
    QtObject {
        id: draft
        property bool darkTheme: true
        property double temperature: 0.7
        property double topP: 1.0
        property int maxTokens: 0
        property string thinkDefault: ""
        property string contextSizeText: "16K"
        property bool showReasoning: true
        property bool showToolInsights: false
        property bool showArtifactInsights: false
        property bool rememberNavigatorState: false
        property bool enableArtifacts: true
        property string officeBinaryPath: ""
        property int compactionThreshold: 80
        property bool vision: false
        property bool tools: false
        property bool thinking: false
        property bool audio: false
        property bool capsTouched: false
        property string activeBackendId: ""
        property bool webSearchEnabled: false
        property string webSearchProvider: "brave"
        property string webSearchApiKey: ""
    }

    function loadDraft() {
        draft.darkTheme = settings.darkTheme
        draft.temperature = settings.temperature
        draft.topP = settings.topP
        draft.maxTokens = settings.maxTokens
        draft.thinkDefault = settings.defaultThinkingMode
        draft.contextSizeText = settings.contextSizeLabel
        draft.showReasoning = settings.showReasoning
        draft.showToolInsights = settings.showToolInsights
        draft.showArtifactInsights = settings.showArtifactInsights
        draft.rememberNavigatorState = settings.rememberNavigatorState
        draft.enableArtifacts = settings.enableArtifacts
        draft.officeBinaryPath = settings.officeBinaryPath
        if (officePathField)
            officePathField.text = draft.officeBinaryPath
        draft.compactionThreshold = settings.compactionThreshold
        draft.activeBackendId = settings.currentBackendId
        draft.webSearchEnabled = settings.webSearchEnabled
        draft.webSearchProvider = settings.webSearchProvider
        draft.webSearchApiKey = settings.webSearchApiKey
        draftBackends.clear()
        const backends = settings.backendSnapshot()
        for (let i = 0; i < backends.length; ++i)
            draftBackends.append(backends[i])
        draftMcp.clear()
        const servers = mcp.serverSnapshot()
        for (let i = 0; i < servers.length; ++i)
            draftMcp.append(servers[i])
        if (ctxCombo)
            ctxCombo.editText = draft.contextSizeText
        loadCapDraft()
    }

    function loadCapDraft() {
        draft.vision = settings.modelVision
        draft.tools = settings.modelTools
        draft.thinking = settings.modelThinking
        draft.audio = settings.modelAudio
        draft.capsTouched = false
    }

    function saveDraft() {
        settings.darkTheme = draft.darkTheme
        settings.temperature = draft.temperature
        settings.topP = draft.topP
        settings.maxTokens = draft.maxTokens
        settings.defaultThinkingMode = draft.thinkDefault
        settings.setContextSizeFromText(ctxCombo.editText.length ? ctxCombo.editText : draft.contextSizeText)
        settings.showReasoning = draft.showReasoning
        settings.showToolInsights = draft.showToolInsights
        settings.showArtifactInsights = draft.showArtifactInsights
        settings.rememberNavigatorState = draft.rememberNavigatorState
        settings.enableArtifacts = draft.enableArtifacts
        settings.officeBinaryPath = draft.officeBinaryPath
        settings.compactionThreshold = draft.compactionThreshold
        settings.webSearchEnabled = draft.webSearchEnabled
        settings.webSearchProvider = draft.webSearchProvider
        settings.webSearchApiKey = draft.webSearchApiKey
        if (draft.capsTouched) {
            settings.modelVision = draft.vision
            settings.modelTools = draft.tools
            settings.modelThinking = draft.thinking
            settings.modelAudio = draft.audio
        }
        const backends = []
        for (let i = 0; i < draftBackends.count; ++i) {
            const r = draftBackends.get(i)
            backends.push({
                              backendId: r.backendId,
                              name: r.name,
                              baseUrl: r.baseUrl,
                              apiKey: r.apiKey
                          })
        }
        settings.applyBackendSnapshot(backends, draft.activeBackendId)
        const servers = []
        for (let i = 0; i < draftMcp.count; ++i) {
            const s = draftMcp.get(i)
            servers.push({
                             name: s.name,
                             command: s.command,
                             args: s.args,
                             enabled: s.enabled
                         })
        }
        mcp.applyServerSnapshot(servers)
        root.close()
    }

    onOpened: loadDraft()

    FileDialog {
        id: officeDialog
        fileMode: FileDialog.OpenFile
        title: "LibreOffice or OpenOffice binary"
        onAccepted: {
            let p = selectedFile.toString()
            if (p.indexOf("file://") === 0) {
                p = decodeURIComponent(p.substring(7))
                if (p.length >= 3 && p.charAt(0) === "/" && p.charAt(2) === ":")
                    p = p.substring(1)
            }
            draft.officeBinaryPath = p
            if (officePathField)
                officePathField.text = p
        }
    }

    Connections {
        target: settings
        function onModelCapsChanged() {
            if (!draft.capsTouched)
                loadCapDraft()
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: 52
            Text {
                anchors.verticalCenter: parent.verticalCenter
                anchors.left: parent.left
                anchors.leftMargin: 20
                text: "Settings"
                color: Theme.text
                font.pixelSize: 16
                font.weight: Font.DemiBold
            }
            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                color: Theme.hairline
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            Rectangle {
                Layout.preferredWidth: 200
                Layout.fillHeight: true
                color: Theme.sidebar
                Column {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 4
                    Repeater {
                        model: root.categories
                        Rectangle {
                            required property int index
                            required property string modelData
                            width: parent.width
                            height: 36
                            radius: 8
                            color: root.category === index ? Theme.selected : (catHover.containsMouse ? Theme.hover : "transparent")
                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.left: parent.left
                                anchors.leftMargin: 12
                                text: modelData
                                color: Theme.text
                                font.pixelSize: 14
                            }
                            MouseArea {
                                id: catHover
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.category = index
                            }
                        }
                    }
                }
            }

            Rectangle {
                Layout.preferredWidth: 1
                Layout.fillHeight: true
                color: Theme.hairline
            }

            StackLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                currentIndex: root.category

                // 0 — General (theme + sampling)
                ScrollView {
                    clip: true
                    contentWidth: availableWidth
                    ColumnLayout {
                        width: Math.max(240, root.width - 250)
                        spacing: 16

                        Item { Layout.preferredHeight: 4 }

                        Text {
                            text: "Appearance"
                            color: Theme.text
                            font.pixelSize: 15
                            font.weight: Font.DemiBold
                            Layout.leftMargin: 24
                        }
                        Text {
                            text: "Theme"
                            color: Theme.muted
                            font.pixelSize: 12
                            Layout.leftMargin: 24
                        }
                        Row {
                            Layout.leftMargin: 24
                            spacing: 8
                            Repeater {
                                model: [
                                    { label: "Dark", dark: true },
                                    { label: "Light", dark: false }
                                ]
                                Rectangle {
                                    required property var modelData
                                    width: modeLab.implicitWidth + 24
                                    height: 32
                                    radius: 16
                                    color: draft.darkTheme === modelData.dark ? Theme.text : Theme.hover
                                    Text {
                                        id: modeLab
                                        anchors.centerIn: parent
                                        text: modelData.label
                                        color: draft.darkTheme === modelData.dark ? Theme.bg : Theme.text
                                        font.pixelSize: 13
                                    }
                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: draft.darkTheme = modelData.dark
                                    }
                                }
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.leftMargin: 24
                            Layout.rightMargin: 24
                            Layout.topMargin: 8
                            radius: 12
                            color: Theme.panel
                            implicitHeight: navRow.height + 24
                            SettingsToggleRow {
                                id: navRow
                                x: 16
                                y: 12
                                width: parent.width - 32
                                title: "Remember navigator state"
                                help: "Keep Projects, Favorites, and Chats expanded or collapsed across restarts. Off always opens them collapsed."
                                checked: draft.rememberNavigatorState
                                onToggled: draft.rememberNavigatorState = !draft.rememberNavigatorState
                            }
                        }

                        Rectangle { Layout.fillWidth: true; Layout.leftMargin: 24; Layout.rightMargin: 24; height: 1; color: Theme.hairline }

                        Text {
                            text: "Insights"
                            color: Theme.text
                            font.pixelSize: 15
                            font.weight: Font.DemiBold
                            Layout.leftMargin: 24
                        }
                        Text {
                            text: "Reasoning, tool, and artifact details in the transcript. Off still leaves a click to reveal them on a message."
                            color: Theme.muted
                            font.pixelSize: 12
                            wrapMode: Text.Wrap
                            Layout.fillWidth: true
                            Layout.leftMargin: 24
                            Layout.rightMargin: 24
                        }
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.leftMargin: 24
                            Layout.rightMargin: 24
                            radius: 12
                            color: Theme.panel
                            implicitHeight: insightCol.height + 24
                            Column {
                                id: insightCol
                                x: 16
                                y: 12
                                width: parent.width - 32
                                spacing: 12
                                SettingsToggleRow {
                                    width: parent.width
                                    title: "Show reasoning insights"
                                    help: "Open thought blocks by default, with a short preview and Show more."
                                    checked: draft.showReasoning
                                    onToggled: draft.showReasoning = !draft.showReasoning
                                }
                                Rectangle { width: parent.width; height: 1; color: Theme.hairline }
                                SettingsToggleRow {
                                    width: parent.width
                                    title: "Show tool insights"
                                    help: "Open tool results by default. Off keeps them behind a click."
                                    checked: draft.showToolInsights
                                    onToggled: draft.showToolInsights = !draft.showToolInsights
                                }
                                Rectangle { width: parent.width; height: 1; color: Theme.hairline }
                                SettingsToggleRow {
                                    width: parent.width
                                    title: "Show artifact insights"
                                    help: "Show a short source preview in the chat. Off keeps HTML, markdown, and large dumps behind a click so scrolling stays smooth."
                                    checked: draft.showArtifactInsights
                                    onToggled: draft.showArtifactInsights = !draft.showArtifactInsights
                                }
                            }
                        }

                        Rectangle { Layout.fillWidth: true; Layout.leftMargin: 24; Layout.rightMargin: 24; height: 1; color: Theme.hairline }

                        Text {
                            text: "Sampling"
                            color: Theme.text
                            font.pixelSize: 15
                            font.weight: Font.DemiBold
                            Layout.leftMargin: 24
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            Layout.leftMargin: 24
                            Layout.rightMargin: 24
                            Text { text: "Temperature " + draft.temperature.toFixed(2); color: Theme.text; Layout.preferredWidth: 160 }
                            Slider {
                                Layout.fillWidth: true
                                from: 0; to: 2; value: draft.temperature
                                onMoved: draft.temperature = value
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            Layout.leftMargin: 24
                            Layout.rightMargin: 24
                            Text { text: "Top-p " + draft.topP.toFixed(2); color: Theme.text; Layout.preferredWidth: 160 }
                            Slider {
                                Layout.fillWidth: true
                                from: 0; to: 1; value: draft.topP
                                onMoved: draft.topP = value
                            }
                        }
                        RowLayout {
                            Layout.leftMargin: 24
                            Layout.rightMargin: 24
                            Text { text: "Max tokens"; color: Theme.text }
                            SpinBox {
                                from: 0; to: 128000; stepSize: 256
                                value: draft.maxTokens
                                onValueModified: draft.maxTokens = value
                            }
                            Text { text: "0 = server default"; color: Theme.muted; font.pixelSize: 12 }
                        }

                        Rectangle { Layout.fillWidth: true; Layout.leftMargin: 24; Layout.rightMargin: 24; height: 1; color: Theme.hairline }

                        Text {
                            text: "Web search"
                            color: Theme.text
                            font.pixelSize: 15
                            font.weight: Font.DemiBold
                            Layout.leftMargin: 24
                        }
                        Text {
                            text: "The model can call a search tool when Tools is on for that model. A Web toggle appears in the composer."
                            color: Theme.muted
                            wrapMode: Text.Wrap
                            font.pixelSize: 12
                            Layout.fillWidth: true
                            Layout.leftMargin: 24
                            Layout.rightMargin: 24
                        }
                        Rectangle {
                            Layout.leftMargin: 24
                            width: webEnLab.implicitWidth + 24
                            height: 32
                            radius: 16
                            color: draft.webSearchEnabled ? Theme.text : Theme.hover
                            Text {
                                id: webEnLab
                                anchors.centerIn: parent
                                text: draft.webSearchEnabled ? "Enabled" : "Disabled"
                                color: draft.webSearchEnabled ? Theme.bg : Theme.text
                                font.pixelSize: 13
                            }
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: draft.webSearchEnabled = !draft.webSearchEnabled
                            }
                        }
                        Text {
                            text: "Provider"
                            color: Theme.muted
                            font.pixelSize: 12
                            Layout.leftMargin: 24
                        }
                        Row {
                            Layout.leftMargin: 24
                            spacing: 8
                            Repeater {
                                model: [
                                    { id: "brave", label: "Brave" },
                                    { id: "tavily", label: "Tavily" },
                                    { id: "exa", label: "Exa" }
                                ]
                                Rectangle {
                                    required property var modelData
                                    width: webProvLab.implicitWidth + 24
                                    height: 32
                                    radius: 16
                                    color: draft.webSearchProvider === modelData.id ? Theme.text : Theme.hover
                                    Text {
                                        id: webProvLab
                                        anchors.centerIn: parent
                                        text: modelData.label
                                        color: draft.webSearchProvider === modelData.id ? Theme.bg : Theme.text
                                        font.pixelSize: 13
                                    }
                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: draft.webSearchProvider = modelData.id
                                    }
                                }
                            }
                        }
                        Text {
                            text: "API key"
                            color: Theme.muted
                            font.pixelSize: 12
                            Layout.leftMargin: 24
                        }
                        TextField {
                            Layout.fillWidth: true
                            Layout.leftMargin: 24
                            Layout.rightMargin: 24
                            echoMode: TextInput.Password
                            text: draft.webSearchApiKey
                            placeholderText: draft.webSearchProvider === "brave"
                                             ? "Brave Search API key"
                                             : (draft.webSearchProvider === "tavily" ? "Tavily API key" : "Exa API key")
                            color: Theme.text
                            onTextChanged: draft.webSearchApiKey = text
                            background: Rectangle { color: Theme.panel; radius: 8; border.color: Theme.border }
                        }

                        Item { Layout.preferredHeight: 20 }
                    }
                }

                // 1 — Models
                ScrollView {
                    clip: true
                    contentWidth: availableWidth
                    ColumnLayout {
                        width: Math.max(240, root.width - 250)
                        spacing: 14

                        Item { Layout.preferredHeight: 4 }

                        Text {
                            text: "Current model"
                            color: Theme.text
                            font.pixelSize: 15
                            font.weight: Font.DemiBold
                            Layout.leftMargin: 24
                        }
                        Text {
                            text: settings.currentModel.length
                                  ? settings.currentModel
                                  : "No model selected"
                            color: Theme.muted
                            font.pixelSize: 13
                            Layout.leftMargin: 24
                        }
                        Text {
                            text: "Context size"
                            color: Theme.muted
                            font.pixelSize: 12
                            Layout.leftMargin: 24
                            Layout.topMargin: 8
                        }
                        ComboBox {
                            id: ctxCombo
                            Layout.leftMargin: 24
                            Layout.preferredWidth: 180
                            implicitHeight: 36
                            padding: 0
                            editable: true
                            model: ["2K", "4K", "8K", "16K", "32K", "64K", "128K", "256K"]
                            enabled: settings.currentModel.length > 0
                            Component.onCompleted: editText = draft.contextSizeText
                            onActivated: draft.contextSizeText = currentText
                            onAccepted: draft.contextSizeText = editText
                            background: Rectangle {
                                implicitHeight: 36
                                color: Theme.panel
                                radius: 8
                                border.color: Theme.border
                                border.width: 1
                            }
                            contentItem: TextField {
                                leftPadding: 12
                                rightPadding: 40
                                topPadding: 0
                                bottomPadding: 0
                                text: ctxCombo.editText
                                color: Theme.text
                                font.pixelSize: 13
                                verticalAlignment: Text.AlignVCenter
                                enabled: ctxCombo.editable
                                autoScroll: ctxCombo.editable
                                readOnly: ctxCombo.down
                                selectionColor: Theme.selected
                                selectedTextColor: Theme.text
                                background: Item {}
                            }
                            indicator: Item {
                                implicitWidth: 36
                                implicitHeight: 36
                                x: ctxCombo.width - width
                                y: 0
                                z: 10
                                clip: true
                                Rectangle {
                                    anchors.fill: parent
                                    anchors.leftMargin: -8
                                    radius: 8
                                    color: Theme.text
                                }
                                Canvas {
                                    id: ctxChevron
                                    width: 12
                                    height: 7
                                    anchors.centerIn: parent
                                    onPaint: {
                                        const c = getContext("2d")
                                        c.reset()
                                        c.fillStyle = Theme.bg
                                        c.beginPath()
                                        c.moveTo(0, 0)
                                        c.lineTo(12, 0)
                                        c.lineTo(6, 7)
                                        c.closePath()
                                        c.fill()
                                    }
                                    Component.onCompleted: requestPaint()
                                    Connections {
                                        target: Theme
                                        function onDarkChanged() { ctxChevron.requestPaint() }
                                    }
                                }
                            }
                        }
                        Text {
                            text: "Tokens in the context window (powers of two, or type any size like 24576)."
                            color: Theme.muted
                            font.pixelSize: 12
                            wrapMode: Text.Wrap
                            Layout.fillWidth: true
                            Layout.leftMargin: 24
                            Layout.rightMargin: 24
                        }

                        Text {
                            text: "Capabilities"
                            color: Theme.text
                            font.pixelSize: 15
                            font.weight: Font.DemiBold
                            Layout.leftMargin: 24
                            Layout.topMargin: 8
                        }
                        Text {
                            text: settings.modelCapsSource
                            color: Theme.muted
                            font.pixelSize: 12
                            wrapMode: Text.Wrap
                            Layout.fillWidth: true
                            Layout.leftMargin: 24
                            Layout.rightMargin: 24
                        }
                        Text {
                            visible: settings.modelCapsOverridden
                            text: "Reset to hint"
                            color: Theme.text
                            font.pixelSize: 12
                            Layout.leftMargin: 24
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    settings.resetModelCaps()
                                    loadCapDraft()
                                }
                            }
                        }
                        Repeater {
                            model: [
                                { key: "vision", label: "Vision", help: "Images in the composer. Turn off if this model cannot see." },
                                { key: "tools", label: "Tools", help: "Send MCP tools with chat requests." },
                                { key: "audio", label: "Audio", help: "This model can take audio. No microphone in the composer yet." }
                            ]
                            Rectangle {
                                required property var modelData
                                Layout.fillWidth: true
                                Layout.leftMargin: 24
                                Layout.rightMargin: 24
                                radius: 12
                                color: Theme.panel
                                implicitHeight: capRow.height + 24
                                enabled: settings.currentModel.length > 0
                                opacity: enabled ? 1 : 0.5
                                readonly property bool capOn: modelData.key === "vision" ? draft.vision
                                    : modelData.key === "tools" ? draft.tools
                                    : modelData.key === "thinking" ? draft.thinking
                                    : draft.audio
                                SettingsToggleRow {
                                    id: capRow
                                    x: 16
                                    y: 12
                                    width: parent.width - 32
                                    title: modelData.label
                                    help: modelData.help
                                    checked: capOn
                                    onToggled: {
                                        const v = !capOn
                                        if (modelData.key === "vision") draft.vision = v
                                        else if (modelData.key === "tools") draft.tools = v
                                        else if (modelData.key === "thinking") draft.thinking = v
                                        else draft.audio = v
                                        draft.capsTouched = true
                                    }
                                }
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.leftMargin: 24
                            Layout.rightMargin: 24
                            radius: 12
                            color: Theme.panel
                            implicitHeight: thinkCapCol.height + 24
                            enabled: settings.currentModel.length > 0
                            opacity: enabled ? 1 : 0.5
                            ColumnLayout {
                                id: thinkCapCol
                                x: 16
                                y: 12
                                width: parent.width - 32
                                spacing: 8
                                SettingsToggleRow {
                                    Layout.fillWidth: true
                                    title: "Thinking"
                                    help: "Show Think on the prompt. The default below applies to new chats; you can still change it per message."
                                    checked: draft.thinking
                                    onToggled: {
                                        draft.thinking = !draft.thinking
                                        draft.capsTouched = true
                                    }
                                }
                                Row {
                                    visible: draft.thinking
                                    spacing: 8
                                    Repeater {
                                        model: [
                                            { value: "", label: "Off" },
                                            { value: "low", label: "Low" },
                                            { value: "medium", label: "Medium" },
                                            { value: "high", label: "High" }
                                        ]
                                        Rectangle {
                                            required property var modelData
                                            width: modeLab.implicitWidth + 24
                                            height: 32
                                            radius: 16
                                            color: draft.thinkDefault === modelData.value ? Theme.text : Theme.hover
                                            Text {
                                                id: modeLab
                                                anchors.centerIn: parent
                                                text: modelData.label
                                                color: draft.thinkDefault === modelData.value ? Theme.bg : Theme.text
                                                font.pixelSize: 13
                                            }
                                            MouseArea {
                                                anchors.fill: parent
                                                cursorShape: Qt.PointingHandCursor
                                                onClicked: draft.thinkDefault = modelData.value
                                            }
                                        }
                                    }
                                }
                            }
                        }

                        Rectangle { Layout.fillWidth: true; Layout.leftMargin: 24; Layout.rightMargin: 24; height: 1; color: Theme.hairline }

                        Text {
                            text: "Backends"
                            color: Theme.text
                            font.pixelSize: 15
                            font.weight: Font.DemiBold
                            Layout.leftMargin: 24
                        }
                        Text {
                            text: "OpenAI-compatible endpoints. The selected backend supplies the model list."
                            color: Theme.muted
                            font.pixelSize: 12
                            wrapMode: Text.Wrap
                            Layout.fillWidth: true
                            Layout.leftMargin: 24
                            Layout.rightMargin: 24
                        }

                        Repeater {
                            model: draftBackends
                            Rectangle {
                                required property int index
                                required property string backendId
                                required property string name
                                required property string baseUrl
                                required property string apiKey
                                Layout.fillWidth: true
                                Layout.leftMargin: 24
                                Layout.rightMargin: 24
                                radius: 12
                                color: Theme.panel
                                border.color: draft.activeBackendId === backendId ? Theme.border : "transparent"
                                implicitHeight: beCol.height + 20
                                ColumnLayout {
                                    id: beCol
                                    x: 12
                                    y: 10
                                    width: parent.width - 24
                                    spacing: 6
                                    RowLayout {
                                        Layout.fillWidth: true
                                        TextField {
                                            Layout.fillWidth: true
                                            text: name
                                            onEditingFinished: draftBackends.setProperty(index, "name", text)
                                        }
                                        RadioButton {
                                            checked: draft.activeBackendId === backendId
                                            onClicked: draft.activeBackendId = backendId
                                            text: "Active"
                                        }
                                        Text {
                                            text: "Remove"
                                            color: Theme.danger
                                            visible: draftBackends.count > 1
                                            MouseArea {
                                                anchors.fill: parent
                                                cursorShape: Qt.PointingHandCursor
                                                onClicked: {
                                                    if (draft.activeBackendId === backendId && index + 1 < draftBackends.count)
                                                        draft.activeBackendId = draftBackends.get(index === 0 ? 1 : 0).backendId
                                                    draftBackends.remove(index)
                                                }
                                            }
                                        }
                                    }
                                    TextField {
                                        Layout.fillWidth: true
                                        text: baseUrl
                                        placeholderText: "http://127.0.0.1:11434/v1"
                                        onEditingFinished: draftBackends.setProperty(index, "baseUrl", text)
                                    }
                                    TextField {
                                        Layout.fillWidth: true
                                        text: apiKey
                                        placeholderText: "API key (optional, default ollama)"
                                        echoMode: TextInput.Password
                                        onEditingFinished: draftBackends.setProperty(index, "apiKey", text)
                                    }
                                }
                            }
                        }
                        Button {
                            Layout.leftMargin: 24
                            text: "Add backend"
                            onClicked: {
                                const id = settings.makeId()
                                draftBackends.append({
                                                         backendId: id,
                                                         name: "Custom",
                                                         baseUrl: "http://127.0.0.1:11434/v1",
                                                         apiKey: ""
                                                     })
                                if (!draft.activeBackendId.length)
                                    draft.activeBackendId = id
                            }
                        }
                        Item { Layout.preferredHeight: 20 }
                    }
                }

                // 2 — MCP
                ScrollView {
                    clip: true
                    contentWidth: availableWidth
                    ColumnLayout {
                        width: Math.max(240, root.width - 250)
                        spacing: 12

                        Item { Layout.preferredHeight: 4 }

                        Text {
                            text: "MCP servers"
                            color: Theme.text
                            font.pixelSize: 15
                            font.weight: Font.DemiBold
                            Layout.leftMargin: 24
                        }
                        Text {
                            text: "Claude Desktop–compatible mcpServers JSON.\n" + mcp.configPath
                            color: Theme.muted
                            font.pixelSize: 11
                            wrapMode: Text.Wrap
                            Layout.fillWidth: true
                            Layout.leftMargin: 24
                            Layout.rightMargin: 24
                        }

                        Repeater {
                            model: draftMcp
                            Rectangle {
                                required property int index
                                required property string name
                                required property string command
                                required property string args
                                required property bool enabled
                                required property string state
                                required property int toolCount
                                required property string error
                                Layout.fillWidth: true
                                Layout.leftMargin: 24
                                Layout.rightMargin: 24
                                radius: 12
                                color: Theme.panel
                                implicitHeight: mcpCol.height + 16
                                ColumnLayout {
                                    id: mcpCol
                                    x: 12
                                    y: 8
                                    width: parent.width - 24
                                    RowLayout {
                                        Layout.fillWidth: true
                                        Switch {
                                            checked: enabled
                                            onToggled: draftMcp.setProperty(index, "enabled", checked)
                                        }
                                        ColumnLayout {
                                            Layout.fillWidth: true
                                            Text { text: name + (state.length ? ("  ·  " + state) : ""); color: Theme.text }
                                            Text { text: command + (args.length ? (" " + args) : ""); color: Theme.muted; font.pixelSize: 11; wrapMode: Text.Wrap; Layout.fillWidth: true }
                                            Text { visible: error.length > 0; text: error; color: Theme.danger; font.pixelSize: 11; wrapMode: Text.Wrap; Layout.fillWidth: true }
                                        }
                                        Text { text: toolCount + " tools"; color: Theme.muted }
                                        Button { text: "Restart"; onClicked: mcp.restart(name) }
                                        Button { text: "Remove"; onClicked: draftMcp.remove(index) }
                                    }
                                }
                            }
                        }

                        Text {
                            text: "Add server"
                            color: Theme.muted
                            font.pixelSize: 12
                            Layout.leftMargin: 24
                            Layout.topMargin: 8
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            Layout.leftMargin: 24
                            Layout.rightMargin: 24
                            TextField { id: mcpName; placeholderText: "name"; Layout.preferredWidth: 110 }
                            TextField { id: mcpCmd; placeholderText: "command"; Layout.preferredWidth: 130 }
                            TextField { id: mcpArgs; placeholderText: "args"; Layout.fillWidth: true }
                            Button {
                                text: "Add"
                                onClicked: {
                                    draftMcp.append({
                                                        name: mcpName.text.length ? mcpName.text : "server",
                                                        command: mcpCmd.text,
                                                        args: mcpArgs.text,
                                                        enabled: true,
                                                        state: "",
                                                        toolCount: 0,
                                                        error: ""
                                                    })
                                    mcpName.text = ""
                                    mcpCmd.text = ""
                                    mcpArgs.text = ""
                                }
                            }
                        }

                        Text {
                            text: "Server log"
                            color: Theme.muted
                            font.pixelSize: 12
                            Layout.leftMargin: 24
                        }
                        TextArea {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 120
                            Layout.leftMargin: 24
                            Layout.rightMargin: 24
                            readOnly: true
                            text: mcp.logText
                            wrapMode: TextEdit.Wrap
                            color: Theme.muted
                            font.family: "monospace"
                            font.pixelSize: 11
                            background: Rectangle { color: Theme.panel; radius: 8 }
                        }
                        Item { Layout.preferredHeight: 20 }
                    }
                }

                // 3 — Advanced
                ScrollView {
                    clip: true
                    contentWidth: availableWidth
                    ColumnLayout {
                        width: Math.max(240, root.width - 250)
                        spacing: 8

                        Item { Layout.preferredHeight: 8 }

                        Text {
                            text: "Advanced"
                            color: Theme.text
                            font.pixelSize: 15
                            font.weight: Font.DemiBold
                            Layout.leftMargin: 24
                        }
                        Text {
                            text: "How Shammy manages a long conversation, and extra document features."
                            color: Theme.muted
                            font.pixelSize: 12
                            wrapMode: Text.Wrap
                            Layout.fillWidth: true
                            Layout.leftMargin: 24
                            Layout.rightMargin: 24
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.leftMargin: 24
                            Layout.rightMargin: 24
                            radius: 12
                            color: Theme.panel
                            implicitHeight: artRow.height + 24
                            SettingsToggleRow {
                                id: artRow
                                x: 16
                                y: 12
                                width: parent.width - 32
                                title: "Enable artifacts"
                                help: "Extract standalone documents and code into a side pane."
                                checked: draft.enableArtifacts
                                onToggled: draft.enableArtifacts = !draft.enableArtifacts
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.leftMargin: 24
                            Layout.rightMargin: 24
                            radius: 12
                            color: Theme.panel
                            implicitHeight: compactCol.height + 24
                            ColumnLayout {
                                id: compactCol
                                x: 16
                                y: 12
                                width: parent.width - 32
                                spacing: 8
                                Text { text: "Compaction threshold"; color: Theme.text; font.pixelSize: 14 }
                                Text {
                                    text: "When context use reaches this percent, older turns are summarized so the chat can continue. Type /compact to run it now."
                                    color: Theme.muted
                                    font.pixelSize: 12
                                    wrapMode: Text.Wrap
                                    Layout.fillWidth: true
                                }
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 12
                                    Text {
                                        text: Math.round(draft.compactionThreshold) + "%"
                                        color: {
                                            const p = draft.compactionThreshold
                                            if (p < 80)
                                                return Theme.text
                                            const t = Math.min(1, Math.max(0, (p - 80) / 10))
                                            return Qt.rgba(
                                                (245 + (239 - 245) * t) / 255,
                                                (158 + (68 - 158) * t) / 255,
                                                (11 + (68 - 11) * t) / 255,
                                                1)
                                        }
                                        font.pixelSize: 18
                                        font.weight: Font.DemiBold
                                        font.family: "monospace"
                                        Layout.preferredWidth: 56
                                    }
                                    Slider {
                                        id: compactSlider
                                        Layout.fillWidth: true
                                        from: 20
                                        to: 90
                                        stepSize: 1
                                        value: draft.compactionThreshold
                                        onMoved: draft.compactionThreshold = Math.min(90, Math.round(value))
                                        handle: Rectangle {
                                            x: compactSlider.leftPadding + compactSlider.visualPosition * (compactSlider.availableWidth - width)
                                            y: compactSlider.topPadding + (compactSlider.availableHeight - height) / 2
                                            implicitWidth: 18
                                            implicitHeight: 18
                                            radius: 9
                                            color: {
                                                const p = draft.compactionThreshold
                                                if (p < 80)
                                                    return Theme.text
                                                const t = Math.min(1, Math.max(0, (p - 80) / 10))
                                                return Qt.rgba(
                                                    (245 + (239 - 245) * t) / 255,
                                                    (158 + (68 - 158) * t) / 255,
                                                    (11 + (68 - 11) * t) / 255,
                                                    1)
                                            }
                                        }
                                    }
                                }
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.leftMargin: 24
                            Layout.rightMargin: 24
                            radius: 12
                            color: Theme.panel
                            implicitHeight: officeCol.height + 24
                            ColumnLayout {
                                id: officeCol
                                x: 16
                                y: 12
                                width: parent.width - 32
                                spacing: 8
                                readonly property string resolved: settings.resolveOfficeBinary(draft.officeBinaryPath)
                                readonly property bool hasOverride: draft.officeBinaryPath.trim().length > 0
                                readonly property bool autodetected: !hasOverride && resolved.length > 0
                                readonly property bool customOk: hasOverride && resolved.length > 0
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 8
                                    Text {
                                        text: "LibreOffice / OpenOffice"
                                        color: Theme.text
                                        font.pixelSize: 14
                                    }
                                    Rectangle {
                                        visible: officeCol.autodetected
                                        width: officeAutoLab.implicitWidth + 16
                                        height: 20
                                        radius: 10
                                        color: Theme.text
                                        Text {
                                            id: officeAutoLab
                                            anchors.centerIn: parent
                                            text: "Autodetected"
                                            color: Theme.bg
                                            font.pixelSize: 11
                                            font.weight: Font.DemiBold
                                        }
                                    }
                                    Rectangle {
                                        visible: officeCol.customOk
                                        width: officeCustomLab.implicitWidth + 16
                                        height: 20
                                        radius: 10
                                        color: "transparent"
                                        border.color: Theme.border
                                        Text {
                                            id: officeCustomLab
                                            anchors.centerIn: parent
                                            text: "Custom"
                                            color: Theme.muted
                                            font.pixelSize: 11
                                            font.weight: Font.DemiBold
                                        }
                                    }
                                    Item { Layout.fillWidth: true }
                                }
                                Text {
                                    text: "Used to export HTML and markdown artifacts to Word (.docx). Leave blank to use a detected install."
                                    color: Theme.muted
                                    font.pixelSize: 12
                                    wrapMode: Text.Wrap
                                    Layout.fillWidth: true
                                }
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 8
                                    TextField {
                                        id: officePathField
                                        Layout.fillWidth: true
                                        text: draft.officeBinaryPath
                                        placeholderText: settings.officeDetectedPath.length
                                                         ? settings.officeDetectedPath
                                                         : "Path to soffice"
                                        color: Theme.text
                                        onTextChanged: draft.officeBinaryPath = text
                                        background: Rectangle { color: Theme.bg; radius: 8; border.color: Theme.border }
                                    }
                                    Rectangle {
                                        width: officeBrowseLab.implicitWidth + 24
                                        height: 32
                                        radius: 8
                                        color: officeBrowseHover.containsMouse ? Theme.hover : "transparent"
                                        border.color: Theme.border
                                        Text {
                                            id: officeBrowseLab
                                            anchors.centerIn: parent
                                            text: "Browse"
                                            color: Theme.text
                                            font.pixelSize: 13
                                        }
                                        MouseArea {
                                            id: officeBrowseHover
                                            anchors.fill: parent
                                            hoverEnabled: true
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: officeDialog.open()
                                        }
                                    }
                                }
                                Text {
                                    text: {
                                        if (officeCol.autodetected)
                                            return "Autodetected — Word export will use " + officeCol.resolved
                                        if (officeCol.customOk)
                                            return "Word export will use " + officeCol.resolved
                                        if (draft.officeBinaryPath.trim().length)
                                            return "That path is not a usable LibreOffice or OpenOffice binary."
                                        return "Not found. Install LibreOffice or OpenOffice, or set the path to soffice."
                                    }
                                    color: officeCol.resolved.length ? Theme.muted : Theme.danger
                                    font.pixelSize: 12
                                    wrapMode: Text.Wrap
                                    Layout.fillWidth: true
                                }
                            }
                        }

                        Item { Layout.preferredHeight: 20 }
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Theme.hairline
        }
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: 56
            Row {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.rightMargin: 20
                spacing: 10
                Rectangle {
                    width: closeLab.implicitWidth + 28
                    height: 34
                    radius: 8
                    color: closeHover.containsMouse ? Theme.hover : "transparent"
                    border.color: Theme.border
                    Text {
                        id: closeLab
                        anchors.centerIn: parent
                        text: "Close"
                        color: Theme.text
                        font.pixelSize: 13
                    }
                    MouseArea {
                        id: closeHover
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.close()
                    }
                }
                Rectangle {
                    width: saveLab.implicitWidth + 28
                    height: 34
                    radius: 8
                    color: Theme.text
                    Text {
                        id: saveLab
                        anchors.centerIn: parent
                        text: "Save"
                        color: Theme.bg
                        font.pixelSize: 13
                        font.weight: Font.DemiBold
                    }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: saveDraft()
                    }
                }
            }
        }
    }
}
