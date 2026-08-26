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
        readonly property int endSlop: 96

        function atTop() {
            if (contentHeight <= height)
                return true
            return contentY <= endSlop
        }

        function atBottom() {
            if (contentHeight <= height)
                return true
            return contentY + height >= contentHeight - endSlop
        }

        function followIfStuck() {
            if (stickToEnd && !dragging && !flicking)
                positionViewAtEnd()
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

        onCountChanged: {
            stickToEnd = true
            Qt.callLater(followIfStuck)
        }
        onContentHeightChanged: Qt.callLater(followIfStuck)
        onMovementStarted: stickToEnd = false
        onMovementEnded: stickToEnd = atBottom()
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
