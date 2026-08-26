import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

Item {
    id: root

    FileDialog {
        id: fileDlg
        title: "Add project files"
        fileMode: FileDialog.OpenFiles
        nameFilters: ["All files (*)"]
        onAccepted: {
            const list = (selectedFiles && selectedFiles.length) ? selectedFiles : [selectedFile]
            for (let i = 0; i < list.length; ++i) {
                if (list[i] && list[i].toString().length)
                    projects.addFile(list[i])
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 16

        Row {
            spacing: 12
            Rectangle {
                width: homeLab.implicitWidth + 16
                height: 28
                radius: 8
                color: homeHover.containsMouse ? Theme.hover : Theme.panel
                border.color: Theme.border
                Text {
                    id: homeLab
                    anchors.centerIn: parent
                    text: "←  Chats"
                    color: Theme.text
                    font.pixelSize: 12
                }
                MouseArea {
                    id: homeHover
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: projects.goHome()
                }
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: "Projects"
                color: crumbHover.containsMouse ? Theme.text : Theme.muted
                font.pixelSize: 12
                MouseArea {
                    id: crumbHover
                    anchors.fill: parent
                    anchors.margins: -4
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: projects.openOverview()
                }
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: "/"
                color: Theme.muted
                font.pixelSize: 12
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: projects.currentProjectName
                color: Theme.text
                font.pixelSize: 12
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumWidth: 280
                ColumnLayout {
                    anchors.fill: parent
                    anchors.rightMargin: 20
                    spacing: 14

                    TextField {
                        Layout.fillWidth: true
                        text: projects.currentProjectName
                        color: Theme.text
                        font.pixelSize: 26
                        font.weight: Font.DemiBold
                        topPadding: 0
                        bottomPadding: 0
                        leftPadding: 0
                        rightPadding: 0
                        background: Item {}
                        onEditingFinished: projects.renameProject(projects.currentProjectId, text)
                    }

                    TextArea {
                        Layout.fillWidth: true
                        Layout.preferredHeight: implicitHeight < 48 ? 48 : Math.min(88, implicitHeight)
                        text: projects.currentProjectDescription
                        placeholderText: "Add a short description"
                        wrapMode: TextEdit.Wrap
                        color: Theme.text
                        font.pixelSize: 14
                        background: Item {}
                        onEditingFinished: projects.currentProjectDescription = text
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        radius: Theme.radiusLg
                        color: Theme.composer
                        border.color: Theme.border
                        implicitHeight: Math.max(88, promptCol.height + 20)
                        Column {
                            id: promptCol
                            x: 16
                            y: 10
                            width: parent.width - 32
                            spacing: 8
                            TextArea {
                                id: projectPrompt
                                width: parent.width
                                placeholderText: "Write a message…"
                                wrapMode: TextEdit.Wrap
                                color: Theme.text
                                font.pixelSize: 15
                                background: Item {}
                                Keys.onPressed: function(event) {
                                    if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter)
                                            && !(event.modifiers & Qt.ShiftModifier)) {
                                        event.accepted = true
                                        const t = projectPrompt.text
                                        projectPrompt.text = ""
                                        chat.startProjectChat(t)
                                    }
                                }
                            }
                            Row {
                                spacing: 8
                                Rectangle {
                                    width: 72
                                    height: 28
                                    radius: 14
                                    color: chatBtnHover.containsMouse ? Theme.selected : Theme.hover
                                    Text { anchors.centerIn: parent; text: "Chat"; color: Theme.text; font.pixelSize: 12 }
                                    MouseArea {
                                        id: chatBtnHover
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: {
                                            const t = projectPrompt.text
                                            projectPrompt.text = ""
                                            chat.startProjectChat(t)
                                        }
                                    }
                                }
                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: settings.currentModel.length ? settings.currentModel : "Select a model"
                                    color: Theme.muted
                                    font.pixelSize: 12
                                }
                            }
                        }
                    }

                    Text {
                        text: "Recents"
                        color: Theme.muted
                        font.pixelSize: 12
                        font.weight: Font.DemiBold
                    }

                    Flickable {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        contentWidth: width
                        contentHeight: recentsCol.height
                        Column {
                            id: recentsCol
                            width: parent.width
                            Repeater { model: chat.favorites; delegate: recentsDelegate }
                            Repeater { model: chat.conversations; delegate: recentsDelegate }
                        }
                    }

                    Text {
                        visible: chat.conversations.rowCount() === 0 && chat.favorites.rowCount() === 0
                        text: "No chats in this project yet. Send a message above to start one."
                        color: Theme.muted
                        font.pixelSize: 13
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                    }
                }
            }

            Rectangle {
                Layout.preferredWidth: 1
                Layout.fillHeight: true
                color: Theme.hairline
            }

            Item {
                Layout.preferredWidth: 320
                Layout.minimumWidth: 280
                Layout.maximumWidth: 380
                Layout.fillHeight: true
                Flickable {
                    id: rightFlick
                    anchors.fill: parent
                    anchors.leftMargin: 20
                    clip: true
                    contentWidth: width
                    contentHeight: rightCol.height
                    boundsBehavior: Flickable.StopAtBounds
                    Column {
                        id: rightCol
                        width: rightFlick.width
                        spacing: 12

                        Text {
                            text: "Instructions"
                            color: Theme.text
                            font.pixelSize: 14
                            font.weight: Font.DemiBold
                        }
                        Text {
                            width: parent.width
                            text: "Permanent instructions preloaded into every chat in this project."
                            color: Theme.muted
                            font.pixelSize: 12
                            wrapMode: Text.Wrap
                        }
                        TextArea {
                            width: parent.width
                            height: 140
                            text: projects.instructions
                            placeholderText: "Add instructions to tailor responses"
                            wrapMode: TextEdit.Wrap
                            color: Theme.text
                            font.pixelSize: 13
                            background: Rectangle { color: Theme.panel; radius: 10; border.color: Theme.border }
                            onEditingFinished: projects.instructions = text
                        }

                        Item { width: 1; height: 6 }

                        Text {
                            text: "Context"
                            color: Theme.text
                            font.pixelSize: 14
                            font.weight: Font.DemiBold
                        }
                        Text {
                            width: parent.width
                            text: "Files whose contents are included in every chat in this project."
                            color: Theme.muted
                            font.pixelSize: 12
                            wrapMode: Text.Wrap
                        }
                        Rectangle {
                            width: parent.width
                            height: 6
                            radius: 3
                            color: Theme.hover
                            Rectangle {
                                width: parent.width * Math.min(100, projects.fileUsagePercent) / 100
                                height: parent.height
                                radius: 3
                                color: projects.fileOverCapacity ? Theme.danger : Theme.text
                            }
                        }
                        Text {
                            width: parent.width
                            text: projects.fileUsageLabel
                            color: projects.fileOverCapacity ? Theme.danger : Theme.muted
                            font.pixelSize: 11
                            wrapMode: Text.Wrap
                        }
                        Text {
                            width: parent.width
                            visible: projects.fileOverCapacity
                            text: "Too much is preloaded. Everything is kept, but only the first 256 KB of files is sent with each chat."
                            color: Theme.danger
                            font.pixelSize: 12
                            wrapMode: Text.Wrap
                        }

                        Rectangle {
                            width: parent.width
                            height: 36
                            radius: 8
                            color: addFilesHover.containsMouse ? Theme.hover : Theme.text
                            Text {
                                anchors.centerIn: parent
                                text: "+  Add files"
                                color: addFilesHover.containsMouse ? Theme.text : Theme.bg
                                font.pixelSize: 13
                                font.weight: Font.DemiBold
                            }
                            MouseArea {
                                id: addFilesHover
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: fileDlg.open()
                            }
                        }

                        DropArea {
                            id: dropZone
                            width: parent.width
                            height: projects.files.length === 0 ? 168 : Math.max(168, fileFlow.height)
                            onDropped: function(drop) {
                                if (drop.hasUrls) {
                                    for (let i = 0; i < drop.urls.length; ++i)
                                        projects.addFile(drop.urls[i])
                                }
                            }

                            Rectangle {
                                anchors.fill: parent
                                visible: projects.files.length === 0
                                radius: 12
                                color: dropZone.containsDrag ? Theme.hover : Theme.panel
                                border.color: Theme.border
                                border.width: 1
                                Column {
                                    anchors.centerIn: parent
                                    spacing: 8
                                    width: parent.width - 28
                                    Text {
                                        width: parent.width
                                        horizontalAlignment: Text.AlignHCenter
                                        text: "Drop files here"
                                        color: Theme.text
                                        font.pixelSize: 14
                                        font.weight: Font.DemiBold
                                    }
                                    Text {
                                        width: parent.width
                                        horizontalAlignment: Text.AlignHCenter
                                        wrapMode: Text.Wrap
                                        text: "or click Add files to browse. Text is preloaded into chats (256 KB cap)."
                                        color: Theme.muted
                                        font.pixelSize: 12
                                    }
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: fileDlg.open()
                                }
                            }

                            Flow {
                                id: fileFlow
                                visible: projects.files.length > 0
                                width: parent.width
                                spacing: 8
                                Repeater {
                                    model: projects.files
                                    Rectangle {
                                        required property var modelData
                                        width: Math.min(148, rightCol.width)
                                        height: 88
                                        radius: 12
                                        color: Theme.panel
                                        border.color: Theme.border
                                        Column {
                                            anchors.fill: parent
                                            anchors.margins: 10
                                            spacing: 4
                                            Text {
                                                width: parent.width - 12
                                                text: modelData.filename
                                                color: Theme.text
                                                font.pixelSize: 12
                                                font.weight: Font.DemiBold
                                                wrapMode: Text.Wrap
                                                maximumLineCount: 2
                                                elide: Text.ElideRight
                                            }
                                            Text {
                                                text: modelData.size < 1024 ? (modelData.size + " B") : (Math.round(modelData.size / 102.4) / 10 + " KB")
                                                color: Theme.muted
                                                font.pixelSize: 11
                                            }
                                        }
                                        Text {
                                            anchors.top: parent.top
                                            anchors.right: parent.right
                                            anchors.margins: 6
                                            text: "×"
                                            color: Theme.muted
                                            font.pixelSize: 14
                                            MouseArea {
                                                anchors.fill: parent
                                                anchors.margins: -4
                                                cursorShape: Qt.PointingHandCursor
                                                onClicked: projects.removeFile(modelData.id)
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    Component {
        id: recentsDelegate
        Rectangle {
            required property string conversationId
            required property string title
            required property var updatedAt
            width: recentsCol.width
            height: 44
            radius: 8
            color: recHover.containsMouse ? Theme.hover : "transparent"
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: 8
                Text {
                    Layout.fillWidth: true
                    text: title.length ? title : "New chat"
                    color: Theme.text
                    elide: Text.ElideRight
                    font.pixelSize: 14
                }
                Text {
                    text: projects.formatUpdated(updatedAt)
                    color: Theme.muted
                    font.pixelSize: 12
                }
            }
            MouseArea {
                id: recHover
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: {
                    chat.openConversation(conversationId)
                    projects.showChat()
                }
            }
        }
    }
}
