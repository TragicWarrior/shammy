import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: win
    visible: false
    width: 1280
    height: 800
    minimumWidth: 900
    minimumHeight: 560
    title: "Shammy"
    color: Theme.bg

    palette.window: Theme.bg
    palette.windowText: Theme.text
    palette.base: Theme.panel
    palette.text: Theme.text
    palette.button: Theme.panel
    palette.buttonText: Theme.text
    palette.highlight: Theme.selected
    palette.highlightedText: Theme.text
    palette.placeholderText: Theme.muted
    palette.mid: Theme.border
    palette.dark: Theme.text
    palette.light: Theme.hover

    Binding { target: Theme; property: "dark"; value: settings.darkTheme }

    Shortcut { sequences: ["Ctrl+N"]; onActivated: { chat.newChat(); projects.showChat() } }
    Shortcut { sequences: ["Ctrl+Shift+N"]; onActivated: { chat.newPrivateChat(); projects.showChat() } }
    Shortcut { sequences: ["Ctrl+,"]; onActivated: settingsSheet.open() }
    Shortcut {
        sequences: ["Esc"]
        onActivated: {
            if (chat.streaming)
                chat.stop()
            else if (settingsSheet.opened)
                settingsSheet.close()
            else if (projects.pane === "project")
                projects.openOverview()
            else if (projects.pane === "overview")
                projects.goHome()
        }
    }

    SplitView {
        anchors.fill: parent
        orientation: Qt.Horizontal
        handle: Rectangle {
            implicitWidth: 1
            color: Theme.hairline
        }

        Sidebar {
            SplitView.preferredWidth: Theme.sidebarWidth
            SplitView.minimumWidth: 200
            SplitView.maximumWidth: 360
            onSettingsRequested: settingsSheet.open()
        }

        Item {
            SplitView.fillWidth: true

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                Item {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 52
                    ComboBox {
                        id: modelBox
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.left: parent.left
                        anchors.leftMargin: 16
                        implicitWidth: Math.min(320, modelLabel.implicitWidth + 36)
                        width: implicitWidth
                        model: settings.models
                        textRole: "name"
                        displayText: settings.currentModel.length ? settings.currentModel : "Select model"
                        onActivated: settings.currentModel = currentText
                        background: Rectangle {
                            implicitHeight: 32
                            color: modelBox.hovered || modelBox.down ? Theme.hover : "transparent"
                            radius: 8
                        }
                        contentItem: Text {
                            id: modelLabel
                            text: modelBox.displayText
                            color: Theme.text
                            font.pixelSize: 14
                            font.weight: Font.DemiBold
                            verticalAlignment: Text.AlignVCenter
                            leftPadding: 10
                            rightPadding: 22
                        }
                        indicator: Text {
                            text: "▾"
                            color: Theme.text
                            anchors.right: parent.right
                            anchors.rightMargin: 8
                            anchors.verticalCenter: parent.verticalCenter
                            font.pixelSize: 12
                        }
                    }
                    Item {
                        id: privateTag
                        visible: chat.privateSession
                        width: visible ? privPill.width : 0
                        height: 22
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.left: modelBox.right
                        anchors.leftMargin: visible ? 12 : 0
                        Rectangle {
                            id: privPill
                            width: privLab.implicitWidth + 16
                            height: 22
                            radius: 11
                            color: Theme.privacy
                            Text {
                                id: privLab
                                anchors.centerIn: parent
                                text: "Private"
                                color: Theme.privacyFg
                                font.pixelSize: 12
                            }
                        }
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.left: privateTag.right
                        anchors.leftMargin: 12
                        visible: settings.loadingModels
                        text: "loading models…"
                        color: Theme.muted
                        font.pixelSize: 12
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.right: usageLab.left
                        anchors.rightMargin: 10
                        visible: chat.compacting
                        text: ["⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏"][compactSpin]
                        color: Theme.warning
                        font.pixelSize: 13
                        font.family: "monospace"
                        property int compactSpin: 0
                        Timer {
                            interval: 80
                            running: chat.compacting
                            repeat: true
                            onTriggered: parent.compactSpin = (parent.compactSpin + 1) % 10
                        }
                    }
                    Text {
                        id: usageLab
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.right: backendLab.left
                        anchors.rightMargin: 16
                        text: chat.compacting ? "compacting…" : chat.contextUsageLabel
                        color: {
                            if (chat.compacting)
                                return Theme.warning
                            const lim = settings.contextSize
                            if (lim > 0 && chat.contextUsed > lim * 0.9)
                                return Theme.danger
                            if (lim > 0 && chat.contextUsed > lim * 0.75)
                                return Theme.text
                            return Theme.muted
                        }
                        font.pixelSize: 12
                        font.family: "monospace"
                    }
                    Text {
                        id: backendLab
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.right: parent.right
                        anchors.rightMargin: 20
                        text: settings.currentBackendName
                        color: Theme.muted
                        font.pixelSize: 12
                    }
                }

                StackLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    currentIndex: projects.pane === "overview" ? 1 : (projects.pane === "project" ? 2 : 0)

                    ChatView { }

                    ProjectsOverview { }

                    ProjectWorkspace { }
                }

                Text {
                    visible: projects.pane === "chat" && chat.compactStatus.length > 0
                    text: chat.compactStatus
                    color: chat.compacting ? Theme.warning : Theme.muted
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                    Layout.leftMargin: 24
                    Layout.rightMargin: 24
                    Layout.bottomMargin: 4
                    horizontalAlignment: Text.AlignHCenter
                    font.pixelSize: 13
                }

                Text {
                    visible: projects.pane === "chat" && chat.errorBanner.length > 0
                    text: chat.errorBanner
                    color: Theme.danger
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                    Layout.leftMargin: 24
                    Layout.rightMargin: 24
                    Layout.bottomMargin: 8
                    horizontalAlignment: Text.AlignHCenter
                    font.pixelSize: 13
                }

                Item {
                    visible: projects.pane === "chat"
                    Layout.fillWidth: true
                    Layout.preferredHeight: visible ? composer.height + 20 : 0
                    Composer {
                        id: composer
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.bottom: parent.bottom
                        anchors.bottomMargin: 12
                        width: Math.min(Theme.chatMaxWidth, parent.width - 32)
                        onSettingsRequested: settingsSheet.open()
                    }
                }
            }
        }

        ArtifactPane {
            visible: settings.enableArtifacts && chat.artifactPaneOpen && chat.artifacts.rowCount() > 0
            SplitView.preferredWidth: Theme.artifactWidth
            SplitView.minimumWidth: 280
            SplitView.fillWidth: false
        }
    }

    SettingsSheet { id: settingsSheet }
    PermissionDialog {}
}
