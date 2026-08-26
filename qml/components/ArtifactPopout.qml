import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs

Window {
    id: root
    width: 960
    height: 720
    minimumWidth: 480
    minimumHeight: 360
    title: chat.currentArtifactTitle.length ? chat.currentArtifactTitle : "Artifact"
    color: Theme.bg
    visible: false

    palette.window: Theme.bg
    palette.windowText: Theme.text
    palette.base: Theme.panel
    palette.text: Theme.text

    FileDialog {
        id: wordDialog
        fileMode: FileDialog.SaveFile
        defaultSuffix: "docx"
        nameFilters: ["Word (*.docx)", "All files (*)"]
        onAccepted: chat.exportCurrentArtifactToWord(selectedFile)
    }

    function popOut() {
        visible = true
        raise()
        requestActivate()
        Qt.callLater(function() {
            viewer.reload()
        })
    }

    Item {
        anchors.fill: parent
        anchors.margins: 12

        Text {
            id: heading
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: wordLab.visible ? wordLab.left : (browserLab.visible ? browserLab.left : closeLab.left)
            anchors.rightMargin: 12
            height: 22
            text: chat.currentArtifactTitle.length ? chat.currentArtifactTitle : "Artifact"
            color: Theme.text
            font.pixelSize: 15
            font.weight: Font.DemiBold
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
        }

        Text {
            id: closeLab
            anchors.top: parent.top
            anchors.right: parent.right
            height: 22
            verticalAlignment: Text.AlignVCenter
            text: "Close"
            color: Theme.muted
            font.pixelSize: 12
            MouseArea {
                anchors.fill: parent
                anchors.margins: -6
                cursorShape: Qt.PointingHandCursor
                onClicked: root.visible = false
            }
        }
        Text {
            id: browserLab
            visible: chat.currentArtifactCanPreview && chat.currentArtifactPreviewUrl.toString().length > 0
            anchors.top: parent.top
            anchors.right: closeLab.left
            anchors.rightMargin: 14
            height: 22
            verticalAlignment: Text.AlignVCenter
            text: "Show in browser"
            color: Theme.text
            font.pixelSize: 12
            MouseArea {
                anchors.fill: parent
                anchors.margins: -6
                cursorShape: Qt.PointingHandCursor
                onClicked: chat.openCurrentArtifactInBrowser()
            }
        }
        Text {
            id: wordLab
            visible: chat.wordExportAvailable || chat.wordExportBusy
            anchors.top: parent.top
            anchors.right: browserLab.visible ? browserLab.left : closeLab.left
            anchors.rightMargin: 14
            height: 22
            verticalAlignment: Text.AlignVCenter
            text: chat.wordExportBusy ? "Exporting…" : "Export to Word"
            color: chat.wordExportBusy ? Theme.muted : Theme.text
            font.pixelSize: 12
            MouseArea {
                anchors.fill: parent
                anchors.margins: -6
                enabled: !chat.wordExportBusy
                cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                onClicked: {
                    wordDialog.selectedFile = chat.suggestedWordExportUrl()
                    wordDialog.open()
                }
            }
        }

        ArtifactViewer {
            id: viewer
            anchors.top: heading.bottom
            anchors.topMargin: 10
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            previewEnabled: root.visible
        }
    }

    Shortcut {
        sequences: ["Esc"]
        onActivated: root.visible = false
    }
}
