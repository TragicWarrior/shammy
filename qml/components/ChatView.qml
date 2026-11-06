import QtQuick
import QtQuick.Controls

Item {
    id: root

    ListView {
        id: list
        anchors.fill: parent
        clip: true
        spacing: 28
        model: chat.messages
        boundsBehavior: Flickable.StopAtBounds
        interactive: true
        flickDeceleration: 1500
        header: Item { height: 8; width: 1 }
        footer: Item { height: 16; width: 1 }

        property bool stickToEnd: true
        property real gestureStartY: 0
        // Bumped when a gesture starts, so a follow queued before it does not run.
        // A send or a conversation load bumps it too, which retires that gesture.
        property int stickEpoch: 0
        property int gestureEpoch: 0
        // Jump chips use a wide band. The pin does not.
        readonly property int endSlop: 96

        function atTop() {
            if (contentHeight <= height)
                return true
            return contentY <= endSlop
        }

        function scheduleFollow() {
            const epoch = stickEpoch
            Qt.callLater(function() {
                if (epoch !== list.stickEpoch)
                    return
                if (!list.stickToEnd || list.dragging || list.flicking || list.moving)
                    return
                list.positionViewAtEnd()
            })
        }

        // Sending, or loading another conversation, shows the end again.
        // An upward scroll during a reply does not call this.
        function repin() {
            stickEpoch++
            stickToEnd = true
            scheduleFollow()
        }

        readonly property bool overflow: contentHeight > height + endSlop
        readonly property bool showJumpTop: count > 0 && overflow && contentY > endSlop
        readonly property bool showJumpBottom: count > 0 && overflow
            && (contentY + height < contentHeight - endSlop)

        delegate: Item {
            width: list.width
            height: bubble.height
            MessageBubble {
                id: bubble
                width: Math.min(Theme.chatMaxWidth, list.width - 48)
                anchors.horizontalCenter: parent.horizontalCenter
                role: model.role
                content: model.content
                reasoning: model.reasoning
                streaming: model.streaming
                parts: model.parts
                toolCalls: model.toolCalls
                error: model.error
                messageId: model.messageId
            }
        }

        // A new row follows the end only when the pin was already on.
        onCountChanged: scheduleFollow()
        onContentHeightChanged: scheduleFollow()
        onMovementStarted: {
            // Cancel a follow already queued, and remember where the gesture began.
            stickEpoch++
            gestureEpoch = stickEpoch
            stickToEnd = false
            gestureStartY = contentY
        }
        onMovementEnded: {
            // A send or a chat switch during the gesture already re-pinned.
            if (gestureEpoch !== stickEpoch)
                return
            // Any upward movement stays where the user left it. Re-pin only
            // when a gesture finishes on the end without having moved up.
            const movedUp = contentY < gestureStartY - 0.5
            stickToEnd = atYEnd && !movedUp
            if (stickToEnd)
                scheduleFollow()
        }
        onDraggingChanged: {
            if (dragging)
                stickToEnd = false
        }

        ScrollBar.vertical: ScrollBar {
            id: vbar
            policy: ScrollBar.AlwaysOn
            implicitWidth: 12
            minimumSize: 0.08
            contentItem: Rectangle {
                implicitWidth: 8
                radius: 4
                color: vbar.pressed ? Theme.text : Theme.muted
                opacity: 0.85
            }
            background: Rectangle {
                implicitWidth: 12
                color: Theme.sidebar
                opacity: 0.35
            }
        }
    }

    Connections {
        target: chat
        function onUserMessageSent() { list.repin() }
    }
    Connections {
        target: chat.messages
        function onModelReset() { list.repin() }
    }

    Column {
        anchors.centerIn: parent
        visible: list.count === 0
        spacing: 10
        width: Math.min(Theme.chatMaxWidth, parent.width * 0.8)
        Text {
            width: parent.width
            text: chat.privateSession ? "Private chat" : "What's on your mind?"
            color: Theme.text
            font.pixelSize: 28
            font.weight: Font.DemiBold
            horizontalAlignment: Text.AlignHCenter
        }
        Text {
            width: parent.width
            text: chat.emptyHint
            color: Theme.muted
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignHCenter
            font.pixelSize: 14
        }
    }

    JumpChip {
        anchors.top: parent.top
        anchors.topMargin: 12
        anchors.right: parent.right
        anchors.rightMargin: 28
        visible: list.showJumpTop
        glyph: "↑"
        onClicked: {
            list.stickToEnd = false
            list.positionViewAtBeginning()
        }
    }

    JumpChip {
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 12
        anchors.right: parent.right
        anchors.rightMargin: 28
        visible: list.showJumpBottom
        glyph: "↓"
        onClicked: {
            list.stickToEnd = true
            list.positionViewAtEnd()
        }
    }

    component JumpChip: Rectangle {
        id: chip
        signal clicked()
        property string glyph: "↑"
        width: 36
        height: 36
        radius: 18
        color: Theme.panel
        border.color: Theme.border
        border.width: 1
        opacity: jumpHover.containsMouse ? 0.95 : 0.72
        visible: false

        Text {
            anchors.centerIn: parent
            text: chip.glyph
            color: Theme.text
            font.pixelSize: 16
            font.weight: Font.DemiBold
        }
        MouseArea {
            id: jumpHover
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: chip.clicked()
        }
    }
}
