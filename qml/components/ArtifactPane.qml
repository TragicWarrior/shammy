import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

Rectangle {
    id: root
    color: Theme.bg
    visible: settings.enableArtifacts && chat.artifactPaneOpen && chat.artifacts.rowCount() > 0

    FileDialog {
        id: saveDialog
        fileMode: FileDialog.SaveFile
        nameFilters: {
            if (chat.currentArtifactHtml)
                return ["HTML (*.html *.htm)", "All files (*)"]
            if (chat.currentArtifactType.indexOf("svg") >= 0)
                return ["SVG (*.svg)", "All files (*)"]
            if (chat.currentArtifactType.indexOf("markdown") >= 0)
                return ["Markdown (*.md)", "All files (*)"]
            return ["All files (*)"]
        }
        onAccepted: chat.saveCurrentArtifact(selectedFile)
    }

    FileDialog {
        id: wordDialog
        fileMode: FileDialog.SaveFile
        defaultSuffix: "docx"
        nameFilters: ["Word (*.docx)", "All files (*)"]
        onAccepted: chat.exportCurrentArtifactToWord(selectedFile)
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            Text {
                text: chat.currentArtifactTitle.length ? chat.currentArtifactTitle : "Artifacts"
                color: Theme.text
                font.pixelSize: 14
                font.bold: true
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
            Text {
                text: "Close"
                color: Theme.muted
                font.pixelSize: 12
                MouseArea {
                    anchors.fill: parent
                    onClicked: chat.artifactPaneOpen = false
                }
            }
        }

        ComboBox {
            id: artBox
            Layout.fillWidth: true
            implicitHeight: 32
            model: chat.artifacts
            textRole: "title"
            currentIndex: Math.min(chat.currentArtifactIndex, Math.max(0, count - 1))
            displayText: chat.currentArtifactTitle.length ? chat.currentArtifactTitle : "Select artifact"
            onActivated: chat.currentArtifactIndex = currentIndex
            background: Rectangle {
                implicitHeight: 32
                color: artBox.hovered || artBox.down ? Theme.hover : Theme.panel
                radius: 8
                border.color: Theme.border
            }
            contentItem: Text {
                text: artBox.displayText
                color: Theme.text
                elide: Text.ElideRight
                verticalAlignment: Text.AlignVCenter
                leftPadding: 10
                rightPadding: 22
                font.pixelSize: 13
            }
            indicator: Text {
                text: "▾"
                color: Theme.text
                anchors.right: parent.right
                anchors.rightMargin: 10
                anchors.verticalCenter: parent.verticalCenter
                font.pixelSize: 13
            }
            popup: Popup {
                y: artBox.height + 4
                width: artBox.width
                padding: 6
                background: Rectangle {
                    color: Theme.sidebar
                    radius: 10
                    border.color: Theme.border
                }
                contentItem: ListView {
                    clip: true
                    implicitHeight: Math.min(contentHeight, 240)
                    model: artBox.popup.visible ? artBox.delegateModel : null
                    currentIndex: artBox.highlightedIndex
                    ScrollIndicator.vertical: ScrollIndicator {}
                }
            }
            delegate: ItemDelegate {
                width: artBox.width
                height: 32
                highlighted: artBox.highlightedIndex === index
                background: Rectangle {
                    radius: 6
                    color: highlighted ? Theme.hover : "transparent"
                }
                contentItem: Text {
                    text: title
                    color: Theme.text
                    elide: Text.ElideRight
                    verticalAlignment: Text.AlignVCenter
                    leftPadding: 8
                    font.pixelSize: 13
                }
            }

            Connections {
                target: chat
                function onCurrentArtifactIndexChanged() {
                    if (artBox.currentIndex !== chat.currentArtifactIndex)
                        artBox.currentIndex = chat.currentArtifactIndex
                }
            }
        }

        RowLayout {
            Text {
                text: "v" + chat.currentArtifactVersion
                color: Theme.muted
                font.pixelSize: 12
            }
            Slider {
                Layout.fillWidth: true
                from: 1
                to: Math.max(1, chat.currentArtifactVersion)
                stepSize: 1
                value: chat.currentArtifactVersion
                onMoved: chat.currentArtifactVersion = value
            }
        }

        ArtifactViewer {
            id: viewer
            Layout.fillWidth: true
            Layout.fillHeight: true
        }

        Row {
            spacing: 12
            Text {
                text: "Copy"
                color: Theme.accent
                font.pixelSize: 12
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        hidden.copy()
                    }
                }
            }
            Text {
                text: "Save"
                color: Theme.accent
                font.pixelSize: 12
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: saveDialog.open()
                }
            }
            Text {
                text: popout.visible ? "Focus window" : "Pop out"
                color: Theme.accent
                font.pixelSize: 12
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: popout.popOut()
                }
            }
            Text {
                visible: chat.wordExportAvailable || chat.wordExportBusy
                text: chat.wordExportBusy ? "Exporting…" : "Export to Word"
                color: chat.wordExportBusy ? Theme.muted : Theme.accent
                font.pixelSize: 12
                MouseArea {
                    anchors.fill: parent
                    enabled: !chat.wordExportBusy
                    cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                    onClicked: {
                        wordDialog.selectedFile = chat.suggestedWordExportUrl()
                        wordDialog.open()
                    }
                }
            }
            Text {
                visible: chat.currentArtifactCanPreview && chat.currentArtifactPreviewUrl.toString().length > 0
                text: "Show in browser"
                color: Theme.accent
                font.pixelSize: 12
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: chat.openCurrentArtifactInBrowser()
                }
            }
        }
    }

    ArtifactPopout {
        id: popout
    }

    TextEdit {
        id: hidden
        visible: false
        text: chat.currentArtifactContent
    }
}
