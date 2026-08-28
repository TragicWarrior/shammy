import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQml

Rectangle {
    id: root
    color: Theme.sidebar
    property bool projectsOpen: false
    property bool favoritesOpen: false
    property bool chatsOpen: false
    property bool navReady: false
    readonly property int favoritesListHeight: {
        if (!favoritesOpen || favList.count <= 0)
            return 0
        return Math.min(favList.count * 38, Math.max(76, mid.height * 0.22))
    }

    function persistNav() {
        if (!navReady || !settings.rememberNavigatorState)
            return
        settings.navProjectsOpen = projectsOpen
        settings.navFavoritesOpen = favoritesOpen
        settings.navChatsOpen = chatsOpen
    }

    Component.onCompleted: {
        if (settings.rememberNavigatorState) {
            projectsOpen = settings.navProjectsOpen
            favoritesOpen = settings.navFavoritesOpen
            chatsOpen = settings.navChatsOpen
        }
        navReady = true
    }

    onProjectsOpenChanged: persistNav()
    onFavoritesOpenChanged: persistNav()
    onChatsOpenChanged: persistNav()

    Connections {
        target: settings
        function onRememberNavigatorStateChanged() {
            persistNav()
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 10

        Text {
            textFormat: Text.StyledText
            text: "<font color=\"#22c55e\">☘</font> Shammy"
            color: Theme.text
            font.pixelSize: 16
            font.weight: Font.DemiBold
            leftPadding: 6
            topPadding: 4
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: projects.goHome()
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Rectangle {
                id: newBtn
                Layout.fillWidth: true
                Layout.preferredHeight: 40
                Layout.minimumWidth: 0
                radius: 10
                color: newHover.containsMouse ? Theme.hover : "transparent"
                border.color: Theme.border
                border.width: 1
                Row {
                    anchors.centerIn: parent
                    spacing: 6
                    Text {
                        text: "+"
                        color: Theme.text
                        font.pixelSize: 16
                        font.weight: Font.DemiBold
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Text {
                        text: "New chat"
                        color: Theme.text
                        font.pixelSize: 13
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
                MouseArea {
                    id: newHover
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: { chat.newChat(); projects.showChat() }
                }
            }
            Rectangle {
                id: privBtn
                Layout.fillWidth: true
                Layout.preferredHeight: 40
                Layout.minimumWidth: 0
                radius: 10
                color: {
                    if (chat.privateSession)
                        return Theme.selected
                    return privHover.containsMouse ? Theme.hover : "transparent"
                }
                border.color: Theme.border
                border.width: 1
                Row {
                    anchors.centerIn: parent
                    spacing: 6
                    MenuIcon {
                        kind: "ghost"
                        stroke: Theme.text
                        width: 16
                        height: 16
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Text {
                        text: "Private"
                        color: Theme.text
                        font.pixelSize: 13
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
                MouseArea {
                    id: privHover
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: { chat.newPrivateChat(); projects.showChat() }
                }
            }
        }

        TextField {
            Layout.fillWidth: true
            placeholderText: "Search"
            color: Theme.text
            placeholderTextColor: Theme.muted
            font.pixelSize: 13
            leftPadding: 10
            background: Rectangle {
                color: Theme.hover
                radius: 10
                border.color: parent.activeFocus ? Theme.border : "transparent"
            }
            onTextChanged: chat.searchQuery = text
        }

        Item {
            id: mid
            Layout.fillWidth: true
            Layout.fillHeight: true
            ColumnLayout {
                anchors.fill: parent
                spacing: 6

                Item {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 22
                    Layout.topMargin: 6
                    Row {
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.left: parent.left
                        anchors.right: projPlus.left
                        spacing: 6
                        leftPadding: 4
                        Text {
                            text: root.projectsOpen ? "▾" : "▸"
                            color: Theme.muted
                            font.pixelSize: 11
                            anchors.verticalCenter: parent.verticalCenter
                            MouseArea {
                                anchors.fill: parent
                                anchors.margins: -6
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.projectsOpen = !root.projectsOpen
                            }
                        }
                        Text {
                            text: "Projects"
                            color: Theme.muted
                            font.pixelSize: 12
                            font.weight: Font.DemiBold
                            anchors.verticalCenter: parent.verticalCenter
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: projects.openOverview()
                            }
                        }
                        Text {
                            visible: projList.count > 0
                            text: String(projList.count)
                            color: Theme.muted
                            font.pixelSize: 11
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }
                    Text {
                        id: projPlus
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.right: parent.right
                        text: "+"
                        color: Theme.muted
                        font.pixelSize: 18
                        rightPadding: 6
                        MouseArea {
                            anchors.fill: parent
                            anchors.margins: -6
                            cursorShape: Qt.PointingHandCursor
                            onClicked: projects.openOverview()
                        }
                    }
                }

                ListView {
                    id: projList
                    visible: root.projectsOpen
                    Layout.fillWidth: true
                    Layout.fillHeight: false
                    Layout.alignment: Qt.AlignTop
                    Layout.preferredHeight: root.projectsOpen ? count * 32 : 0
                    Layout.maximumHeight: {
                        if (!root.projectsOpen)
                            return 0
                        let leave = 28 + 28
                        if (root.favoritesOpen)
                            leave += root.favoritesListHeight
                        if (root.chatsOpen)
                            leave += 80
                        else
                            leave += 28
                        return Math.max(count > 0 ? 32 : 0, mid.height - leave)
                    }
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    model: projects.projects
                    spacing: 2
                    delegate: Rectangle {
                        required property string projectId
                        required property string name
                        width: ListView.view.width
                        height: 30
                        radius: 8
                        color: {
                            if (projects.currentProjectId === projectId)
                                return Theme.selected
                            return projHover.containsMouse ? Theme.hover : "transparent"
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.left: parent.left
                            anchors.leftMargin: 10
                            text: name
                            color: Theme.text
                            elide: Text.ElideRight
                            width: parent.width - 16
                            font.pixelSize: 13
                        }
                        MouseArea {
                            id: projHover
                            anchors.fill: parent
                            hoverEnabled: true
                            acceptedButtons: Qt.LeftButton | Qt.RightButton
                            cursorShape: Qt.PointingHandCursor
                            onClicked: function(mouse) {
                                if (mouse.button === Qt.RightButton)
                                    projMenu.popup()
                                else
                                    projects.openProject(projectId)
                            }
                        }
                        ThemedMenu {
                            id: projMenu
                            ThemedMenuItem { text: "Clear filter"; onTriggered: projects.clearCurrent() }
                            ThemedMenuItem { text: "Rename"; onTriggered: projects.renameProject(projectId, name) }
                            ThemedMenuItem {
                                iconKind: "trash"
                                text: "Delete"
                                destructive: true
                                onTriggered: projects.deleteProject(projectId)
                            }
                        }
                    }
                }

                Item {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 22
                    Layout.topMargin: 4
                    Row {
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 6
                        leftPadding: 4
                        Text {
                            text: root.favoritesOpen ? "▾" : "▸"
                            color: Theme.muted
                            font.pixelSize: 11
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        Text {
                            text: "Favorites"
                            color: Theme.muted
                            font.pixelSize: 12
                            font.weight: Font.DemiBold
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        Text {
                            visible: favList.count > 0
                            text: String(favList.count)
                            color: Theme.muted
                            font.pixelSize: 11
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.favoritesOpen = !root.favoritesOpen
                    }
                }

                ListView {
                    id: favList
                    visible: root.favoritesOpen && count > 0
                    Layout.fillWidth: true
                    Layout.fillHeight: false
                    Layout.alignment: Qt.AlignTop
                    Layout.preferredHeight: root.favoritesListHeight
                    Layout.maximumHeight: root.favoritesListHeight
                    Layout.minimumHeight: 0
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    model: chat.favorites
                    spacing: 1
                    delegate: chatRow
                }

                Item {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 22
                    Layout.topMargin: 4
                    Row {
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.left: parent.left
                        anchors.right: parent.right
                        spacing: 6
                        leftPadding: 4
                        Text {
                            text: root.chatsOpen ? "▾" : "▸"
                            color: Theme.muted
                            font.pixelSize: 11
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        Text {
                            width: parent.width - 24
                            text: projects.currentProjectId.length ? projects.currentProjectName : "Chats"
                            color: Theme.muted
                            font.pixelSize: 12
                            font.weight: Font.DemiBold
                            elide: Text.ElideRight
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.chatsOpen = !root.chatsOpen
                    }
                }

                ListView {
                    visible: root.chatsOpen
                    Layout.fillWidth: true
                    Layout.fillHeight: root.chatsOpen
                    Layout.alignment: Qt.AlignTop
                    Layout.preferredHeight: root.chatsOpen ? 80 : 0
                    Layout.minimumHeight: 0
                    Layout.maximumHeight: root.chatsOpen ? 100000 : 0
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    model: chat.conversations
                    spacing: 1
                    delegate: chatRow
                }

                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: !root.chatsOpen
                    Layout.preferredHeight: 0
                    Layout.minimumHeight: 0
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            height: 40
            radius: 10
            color: setHover.containsMouse ? Theme.hover : "transparent"
            Row {
                anchors.verticalCenter: parent.verticalCenter
                anchors.left: parent.left
                anchors.leftMargin: 10
                spacing: 8
                Text {
                    text: "⚙"
                    color: Theme.text
                    font.pixelSize: 15
                }
                Text {
                    text: "Settings"
                    color: Theme.text
                    font.pixelSize: 13
                }
            }
            MouseArea {
                id: setHover
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: root.settingsRequested()
            }
        }
    }

    signal settingsRequested()

    function confirmDeleteChat(id, title) {
        deleteDialog.convId = id
        deleteDialog.convTitle = title && title.length ? title : "New chat"
        deleteDialog.open()
    }

    function renameChat(id, title) {
        renameDialog.convId = id
        renameDialog.convTitle = title && title.length ? title : "New chat"
        renameDialog.open()
    }

    Popup {
        id: deleteDialog
        property string convId: ""
        property string convTitle: ""
        modal: true
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: 360
        padding: 20
        background: Rectangle {
            color: Theme.bg
            radius: 16
            border.color: Theme.border
        }
        Column {
            width: parent.width
            spacing: 12
            Text {
                text: "Delete chat?"
                color: Theme.text
                font.pixelSize: 16
                font.weight: Font.DemiBold
            }
            Text {
                width: parent.width
                text: "Delete “" + deleteDialog.convTitle + "”? This cannot be undone."
                color: Theme.muted
                wrapMode: Text.Wrap
                font.pixelSize: 13
            }
            Row {
                anchors.right: parent.right
                spacing: 8
                Rectangle {
                    width: cancelLab.implicitWidth + 24
                    height: 34
                    radius: 8
                    color: cancelHover.containsMouse ? Theme.hover : "transparent"
                    border.color: Theme.border
                    Text {
                        id: cancelLab
                        anchors.centerIn: parent
                        text: "Cancel"
                        color: Theme.text
                        font.pixelSize: 13
                    }
                    MouseArea {
                        id: cancelHover
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: deleteDialog.close()
                    }
                }
                Rectangle {
                    width: delLab.implicitWidth + 24
                    height: 34
                    radius: 8
                    color: delConfirmHover.containsMouse ? "#dc2626" : Theme.danger
                    Text {
                        id: delLab
                        anchors.centerIn: parent
                        text: "Delete"
                        color: "#ffffff"
                        font.pixelSize: 13
                    }
                    MouseArea {
                        id: delConfirmHover
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            chat.deleteConversation(deleteDialog.convId)
                            deleteDialog.close()
                        }
                    }
                }
            }
        }
    }

    Popup {
        id: renameDialog
        property string convId: ""
        property string convTitle: ""
        modal: true
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: 360
        padding: 20
        background: Rectangle {
            color: Theme.bg
            radius: 16
            border.color: Theme.border
        }
        Column {
            width: parent.width
            spacing: 12
            Text {
                text: "Rename chat"
                color: Theme.text
                font.pixelSize: 16
                font.weight: Font.DemiBold
            }
            TextField {
                id: renameField
                width: parent.width
                text: renameDialog.convTitle
                color: Theme.text
                selectByMouse: true
                font.pixelSize: 14
                leftPadding: 10
                rightPadding: 10
                background: Rectangle {
                    color: Theme.panel
                    radius: 8
                    border.color: renameField.activeFocus ? Theme.border : "transparent"
                }
                Keys.onReturnPressed: renameDialog.apply()
                Keys.onEnterPressed: renameDialog.apply()
            }
            Row {
                anchors.right: parent.right
                spacing: 8
                Rectangle {
                    width: renameCancelLab.implicitWidth + 24
                    height: 34
                    radius: 8
                    color: renameCancelHover.containsMouse ? Theme.hover : "transparent"
                    border.color: Theme.border
                    Text {
                        id: renameCancelLab
                        anchors.centerIn: parent
                        text: "Cancel"
                        color: Theme.text
                        font.pixelSize: 13
                    }
                    MouseArea {
                        id: renameCancelHover
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: renameDialog.close()
                    }
                }
                Rectangle {
                    width: renameSaveLab.implicitWidth + 24
                    height: 34
                    radius: 8
                    color: Theme.text
                    Text {
                        id: renameSaveLab
                        anchors.centerIn: parent
                        text: "Save"
                        color: Theme.bg
                        font.pixelSize: 13
                        font.weight: Font.DemiBold
                    }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: renameDialog.apply()
                    }
                }
            }
        }
        function apply() {
            chat.renameConversation(convId, renameField.text)
            close()
        }
        onOpened: {
            renameField.text = convTitle
            renameField.forceActiveFocus()
            renameField.selectAll()
        }
    }

    Component {
        id: chatRow
        Rectangle {
            required property string conversationId
            required property string title
            required property bool pinned
            required property string modelName
            required property string projectId
            width: ListView.view ? ListView.view.width : 200
            height: 38
            radius: 8
            color: {
                if (chat.conversationId === conversationId)
                    return Theme.selected
                return convHover.containsMouse ? Theme.hover : "transparent"
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                anchors.left: parent.left
                anchors.leftMargin: 10
                anchors.right: favBtn.left
                anchors.rightMargin: 8
                text: title.length ? title : "New chat"
                color: Theme.text
                elide: Text.ElideRight
                font.pixelSize: 13
            }
            Text {
                id: favBtn
                z: 1
                anchors.verticalCenter: parent.verticalCenter
                anchors.right: genSpinner.visible ? genSpinner.left : delBtn.left
                anchors.rightMargin: 6
                visible: convHover.containsMouse || pinned
                width: visible ? 16 : 0
                horizontalAlignment: Text.AlignHCenter
                text: pinned ? "★" : "☆"
                color: pinned ? Theme.text : Theme.muted
                font.pixelSize: 14
                MouseArea {
                    anchors.fill: parent
                    anchors.margins: -6
                    cursorShape: Qt.PointingHandCursor
                    onClicked: chat.togglePin(conversationId)
                }
            }
            // Background-generation spinner sits in the trailing controls slot so
            // the title's left edge stays flush; hovering reveals the × instead.
            Text {
                id: genSpinner
                visible: chat.generatingConversationId === conversationId && !convHover.containsMouse
                anchors.verticalCenter: parent.verticalCenter
                anchors.right: parent.right
                anchors.rightMargin: 8
                width: visible ? 14 : 0
                horizontalAlignment: Text.AlignHCenter
                text: ["⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏"][spin]
                color: Theme.text
                font.pixelSize: 12
                font.family: "monospace"
                property int spin: 0
                Timer {
                    interval: 80
                    running: genSpinner.visible
                    repeat: true
                    onTriggered: genSpinner.spin = (genSpinner.spin + 1) % 10
                }
            }
            Text {
                id: delBtn
                z: 1
                anchors.verticalCenter: parent.verticalCenter
                anchors.right: parent.right
                anchors.rightMargin: 8
                visible: convHover.containsMouse
                width: visible ? implicitWidth : 0
                text: "×"
                color: delHover.containsMouse ? Theme.danger : Theme.muted
                font.pixelSize: 16
                MouseArea {
                    id: delHover
                    anchors.fill: parent
                    anchors.margins: -6
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.confirmDeleteChat(conversationId, title)
                }
            }
            MouseArea {
                id: convHover
                anchors.fill: parent
                hoverEnabled: true
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                cursorShape: Qt.PointingHandCursor
                onClicked: function(mouse) {
                    if (mouse.button === Qt.RightButton)
                        convMenu.popup()
                    else {
                        chat.openConversation(conversationId)
                        projects.showChat()
                    }
                }
            }
            ThemedMenu {
                id: convMenu
                ThemedMenuItem {
                    iconKind: "pencil"
                    text: "Rename"
                    onTriggered: root.renameChat(conversationId, title)
                }
                ThemedMenuItem {
                    iconKind: "pin"
                    text: pinned ? "Remove from favorites" : "Add to favorites"
                    onTriggered: chat.togglePin(conversationId)
                }
                ThemedMenu {
                    id: moveMenu
                    title: "Move to project"
                    iconKind: "folder"
                    ThemedMenuItem {
                        text: "No project"
                        onTriggered: chat.moveToProject(conversationId, "")
                    }
                    Instantiator {
                        model: projects.projects
                        delegate: ThemedMenuItem {
                            required property string projectId
                            required property string name
                            text: name
                            onTriggered: chat.moveToProject(conversationId, projectId)
                        }
                        onObjectAdded: function(index, object) { moveMenu.insertItem(index + 1, object) }
                        onObjectRemoved: function(index, object) { moveMenu.removeItem(object) }
                    }
                }
                MenuSeparator {
                    padding: 6
                    contentItem: Rectangle {
                        implicitHeight: 1
                        color: Theme.hairline
                    }
                }
                ThemedMenuItem {
                    iconKind: "trash"
                    text: "Delete"
                    destructive: true
                    onTriggered: root.confirmDeleteChat(conversationId, title)
                }
            }
        }
    }
}
