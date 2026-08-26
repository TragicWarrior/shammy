import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 28
        spacing: 18

        RowLayout {
            Layout.fillWidth: true
            spacing: 12
            Rectangle {
                Layout.preferredWidth: overviewHomeLab.implicitWidth + 16
                Layout.preferredHeight: 28
                Layout.alignment: Qt.AlignVCenter
                radius: 8
                color: overviewHomeHover.containsMouse ? Theme.hover : Theme.panel
                border.color: Theme.border
                Text {
                    id: overviewHomeLab
                    anchors.centerIn: parent
                    text: "←  Chats"
                    color: Theme.text
                    font.pixelSize: 12
                }
                MouseArea {
                    id: overviewHomeHover
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: projects.goHome()
                }
            }
            Text {
                text: "Projects"
                color: Theme.text
                font.pixelSize: 28
                font.weight: Font.DemiBold
            }
            Item { Layout.fillWidth: true }
            Rectangle {
                width: importLab.implicitWidth + 28
                height: 34
                radius: 8
                color: importHover.containsMouse ? Theme.hover : Theme.panel
                border.color: Theme.border
                Text {
                    id: importLab
                    anchors.centerIn: parent
                    text: "Import from Claude"
                    color: Theme.text
                    font.pixelSize: 13
                }
                MouseArea {
                    id: importHover
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        claudeImport.refresh()
                        importDlg.open()
                    }
                }
            }
            Rectangle {
                width: newLab.implicitWidth + 28
                height: 34
                radius: 8
                color: Theme.text
                Text {
                    id: newLab
                    anchors.centerIn: parent
                    text: "New project"
                    color: Theme.bg
                    font.pixelSize: 13
                    font.weight: Font.DemiBold
                }
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: createDlg.open()
                }
            }
        }

        GridView {
            id: grid
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            cellWidth: Math.max(280, width / Math.max(1, Math.floor(width / 320)))
            cellHeight: 118
            model: projects.projects
            delegate: Item {
                width: grid.cellWidth
                height: grid.cellHeight
                required property string projectId
                required property string name
                required property string description
                required property var updatedAt
                Rectangle {
                    anchors.fill: parent
                    anchors.margins: 8
                    radius: 14
                    color: cardHover.containsMouse ? Theme.hover : Theme.panel
                    border.color: Theme.border
                    border.width: 1
                    Column {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.leftMargin: 16
                        anchors.rightMargin: 16
                        spacing: 6
                        Text {
                            width: parent.width
                            text: name
                            color: Theme.text
                            font.pixelSize: 15
                            font.weight: Font.DemiBold
                            elide: Text.ElideRight
                        }
                        Text {
                            width: parent.width
                            visible: description.length > 0
                            text: description
                            color: Theme.muted
                            font.pixelSize: 13
                            wrapMode: Text.Wrap
                            maximumLineCount: 2
                            elide: Text.ElideRight
                        }
                        Text {
                            text: projects.formatUpdated(updatedAt)
                            color: Theme.muted
                            font.pixelSize: 12
                        }
                    }
                    MouseArea {
                        id: cardHover
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: projects.openProject(projectId)
                    }
                }
            }
        }

        Text {
            visible: grid.count === 0
            Layout.alignment: Qt.AlignHCenter
            text: "No projects yet. Create one to keep instructions and files with a set of chats."
            color: Theme.muted
            font.pixelSize: 14
        }
    }

    Popup {
        id: createDlg
        modal: true
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: 420
        padding: 20
        background: Rectangle {
            color: Theme.bg
            radius: 16
            border.color: Theme.border
        }
        ColumnLayout {
            width: parent.width
            spacing: 10
            Text {
                text: "New project"
                color: Theme.text
                font.pixelSize: 16
                font.weight: Font.DemiBold
            }
            TextField {
                id: nameField
                Layout.fillWidth: true
                placeholderText: "Name"
                color: Theme.text
                background: Rectangle { color: Theme.panel; radius: 8 }
            }
            TextArea {
                id: descField
                Layout.fillWidth: true
                Layout.preferredHeight: 72
                placeholderText: "Description (optional)"
                wrapMode: TextEdit.Wrap
                color: Theme.text
                background: Rectangle { color: Theme.panel; radius: 8 }
            }
            Row {
                Layout.alignment: Qt.AlignRight
                spacing: 8
                Rectangle {
                    width: 88
                    height: 34
                    radius: 8
                    border.color: Theme.border
                    color: "transparent"
                    Text { anchors.centerIn: parent; text: "Cancel"; color: Theme.text; font.pixelSize: 13 }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: createDlg.close()
                    }
                }
                Rectangle {
                    width: 88
                    height: 34
                    radius: 8
                    color: Theme.text
                    Text { anchors.centerIn: parent; text: "Create"; color: Theme.bg; font.pixelSize: 13; font.weight: Font.DemiBold }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            projects.createProject(nameField.text, descField.text)
                            nameField.text = ""
                            descField.text = ""
                            createDlg.close()
                        }
                    }
                }
            }
        }
        onOpened: nameField.forceActiveFocus()
    }

    Popup {
        id: importDlg
        modal: true
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: 460
        height: 480
        padding: 20
        background: Rectangle {
            color: Theme.bg
            radius: 16
            border.color: Theme.border
        }
        ColumnLayout {
            anchors.fill: parent
            spacing: 10
            Text {
                text: "Import from Claude"
                color: Theme.text
                font.pixelSize: 16
                font.weight: Font.DemiBold
            }
            Text {
                Layout.fillWidth: true
                text: "Reads projects from the signed-in Claude Desktop session (name, description, instructions, files, and chats). Oversize context is imported anyway; the meter turns red."
                color: Theme.muted
                wrapMode: Text.Wrap
                font.pixelSize: 12
            }
            Text {
                Layout.fillWidth: true
                visible: claudeImport.error.length > 0
                text: claudeImport.error
                color: Theme.danger
                wrapMode: Text.Wrap
                font.pixelSize: 12
            }
            Text {
                Layout.fillWidth: true
                visible: claudeImport.status.length > 0
                text: claudeImport.status
                color: Theme.muted
                wrapMode: Text.Wrap
                font.pixelSize: 12
            }
            ListView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: claudeImport.projects
                spacing: 6
                delegate: Rectangle {
                    required property var modelData
                    width: ListView.view.width
                    height: Math.max(56, importCol.height + 16)
                    radius: 10
                    color: Theme.panel
                    border.color: Theme.border
                    Column {
                        id: importCol
                        anchors.left: parent.left
                        anchors.right: importTrail.left
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.leftMargin: 12
                        anchors.rightMargin: 8
                        spacing: 4
                        Text {
                            width: parent.width
                            text: modelData.name
                            color: Theme.text
                            font.pixelSize: 14
                            font.weight: Font.DemiBold
                            elide: Text.ElideRight
                        }
                        Text {
                            width: parent.width
                            visible: modelData.description && modelData.description.length > 0
                            text: modelData.description
                            color: Theme.muted
                            font.pixelSize: 12
                            wrapMode: Text.Wrap
                            maximumLineCount: 2
                            elide: Text.ElideRight
                        }
                    }
                    Row {
                        id: importTrail
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.rightMargin: 10
                        spacing: 8
                        Text {
                            visible: claudeImport.importingId === modelData.uuid
                            anchors.verticalCenter: parent.verticalCenter
                            text: ["⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏"][importSpin]
                            color: Theme.text
                            font.pixelSize: 14
                            font.family: "monospace"
                            property int importSpin: 0
                            Timer {
                                interval: 80
                                running: parent.visible
                                repeat: true
                                onTriggered: parent.importSpin = (parent.importSpin + 1) % 10
                            }
                        }
                        Rectangle {
                            id: importBtn
                            width: importBtnLab.implicitWidth + 16
                            height: 28
                            radius: 8
                            color: Theme.text
                            opacity: claudeImport.busy && claudeImport.importingId !== modelData.uuid ? 0.45 : 1
                            Text {
                                id: importBtnLab
                                anchors.centerIn: parent
                                text: claudeImport.importingId === modelData.uuid ? "Importing" : "Import"
                                color: Theme.bg
                                font.pixelSize: 12
                                font.weight: Font.DemiBold
                            }
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                enabled: !claudeImport.busy
                                onClicked: claudeImport.importProject(modelData.uuid)
                            }
                        }
                    }
                }
            }
            Text {
                visible: !claudeImport.busy && claudeImport.projects.length === 0 && claudeImport.error.length === 0
                text: "No projects listed yet."
                color: Theme.muted
                font.pixelSize: 13
            }
            Row {
                Layout.alignment: Qt.AlignRight
                Rectangle {
                    width: 88
                    height: 34
                    radius: 8
                    border.color: Theme.border
                    color: "transparent"
                    Text { anchors.centerIn: parent; text: "Close"; color: Theme.text; font.pixelSize: 13 }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: importDlg.close()
                    }
                }
            }
        }
    }
}
