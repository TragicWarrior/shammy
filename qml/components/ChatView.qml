import QtQuick
import QtQuick.Controls

Item {
    id: root

    ListView {
        id: list
        objectName: "chatList"
        anchors.fill: parent
        clip: true
        spacing: 28
        model: chat.messages
        boundsBehavior: Flickable.StopAtBounds
        interactive: true
        flickDeceleration: 1500
        // Page keys scroll only when the list itself has focus. The composer
        // keeps focus while typing, so this does not steal those keys.
        Keys.onPressed: (event) => {
            if (event.key === Qt.Key_PageUp) {
                own = true
                contentY = Math.max(originY, contentY - height)
                own = false
                pinned = false
                capture()
                event.accepted = true
            } else if (event.key === Qt.Key_PageDown) {
                const end = Math.max(originY, originY + contentHeight - height)
                own = true
                contentY = Math.min(end, contentY + height)
                own = false
                pinned = atYEnd
                capture()
                event.accepted = true
            } else if (event.key === Qt.Key_Home) {
                own = true
                contentY = originY
                own = false
                pinned = false
                capture()
                event.accepted = true
            } else if (event.key === Qt.Key_End) {
                repin()
                event.accepted = true
            }
        }
        // Pixels of delegates kept on either side of the viewport, not a
        // memory allocation. Past the real content, ListView stops. Inside
        // it, every row is built, so the scrollbar maps real heights instead
        // of the average it uses for rows it has not created.
        cacheBuffer: 10000000
        header: Item { height: 8; width: 1 }
        footer: Item { height: 16; width: 1 }

        // True when new rows and growth should keep the end in view.
        // User motion updates it from atYEnd. Nothing here is per input device.
        property bool pinned: true
        // Our own contentY writes. Those are not user scrolls.
        property bool own: false
        // True while the list is being built from the top, before the pin.
        property bool filling: false
        // How many times a content-size change actually pulled the view.
        property int followRuns: 0
        property int anchorIndex: 0
        property real anchorOffset: 0
        // modelReset can fire before the view has dropped the old rows.
        property int fillAttempts: 0
        // Jump chips use a wide band. The pin uses Flickable.atYEnd.
        readonly property int endSlop: 96

        function atTop() {
            if (contentHeight <= height)
                return true
            return contentY <= endSlop
        }

        // The rows the reader can see. Null until that index has been built.
        function rowItem(i) {
            if (i < 0 || i >= count)
                return null
            return itemAtIndex(i)
        }

        function rowsMissing() {
            if (count <= 0 || height <= 0)
                return false
            if (!rowItem(0) || !rowItem(count - 1))
                return true
            return rowItem(Math.floor((count - 1) / 2)) === null
        }

        // Highest index built so far, counting from the start.
        function builtFromStart() {
            let n = 0
            while (n < count && rowItem(n))
                n++
            return n
        }

        // The row's y in content coordinates. mapToItem(list) is the viewport
        // y, so it changes when contentY changes even if the row did not move.
        function contentTop(item) {
            return item.mapToItem(list, 0, 0).y + contentY
        }

        // Remember the row at the top of the viewport and its content y.
        function capture() {
            // indexAt takes content coordinates. The top of the viewport is contentY.
            let i = indexAt(Math.min(24, Math.max(1, width - 24)), contentY + 4)
            if (i < 0)
                i = anchorIndex
            const item = rowItem(i)
            if (!item)
                return
            anchorIndex = i
            anchorOffset = contentTop(item)
        }

        // A row above the viewport grew. Move contentY with it so the
        // rows on screen stay put. A row below does not move this anchor.
        // A contentY change by itself leaves contentTop unchanged.
        function holdAnchor() {
            const item = rowItem(anchorIndex)
            if (!item)
                return
            const delta = contentTop(item) - anchorOffset
            if (Math.abs(delta) < 0.5)
                return
            // Record the correction before the write. A second follow queued
            // for this same growth must not apply it again.
            anchorOffset += delta
            own = true
            contentY += delta
            own = false
        }

        // True while a person is moving the list. A contentY change without
        // this is ListView laying out, and must not clear the pin or the anchor.
        function userMoving() {
            return (vbar && vbar.pressed) || dragging || moving || flicking
        }

        function confirmPin() {
            if (own || filling || userMoving())
                return
            // Only latch on. A layout pass that is not at the end yet must
            // not clear a pin that a jump to the end is about to restore.
            if (atYEnd)
                pinned = true
        }

        // The resting row is not known until the layout after the gesture.
        // Capturing in the press handler stores a y the view then settles
        // away from, and the next hold yanks the page back there.
        function gestureEnded() {
            if (own || filling || userMoving())
                return
            pinned = atYEnd
            // The scrollbar release lays out once more. Capture after that.
            Qt.callLater(function() { Qt.callLater(capture) })
        }

        // positionViewAtEnd() drops every row when the last one is not
        // already on screen, and the cache does not build them again.
        // Writing contentY keeps the rows that fillFromStart created.
        function goEnd() {
            const target = Math.max(originY, originY + contentHeight - height)
            own = true
            contentY = target
            own = false
            pinned = true
            capture()
        }

        function follow() {
            if (filling || own)
                return
            // A pointer is on the bar, or the list itself is in a flick.
            if (!vbar || vbar.pressed || dragging || moving || flicking)
                return
            if (pinned) {
                if (atYEnd)
                    return
                if (rowsMissing()) {
                    fillFromStart()
                    if (rowsMissing())
                        return
                }
                followRuns++
                goEnd()
                return
            }
            holdAnchor()
        }

        function queueFollow() {
            if (filling)
                return
            // The first pass can run before ListView has moved the rows.
            // The second runs after that polish.
            Qt.callLater(follow)
            Qt.callLater(function() { Qt.callLater(follow) })
        }

        // Build every row from the top. Positioning at the end first leaves
        // the rows above as estimates, which is what makes the grip jump.
        function fillFromStart() {
            if (!rowsMissing())
                return
            filling = true
            own = true
            let spins = 0
            let built = -1
            // Each forceLayout creates a few rows and only in the forward
            // direction. Stay at the top until the last row exists. Jumping
            // to the end first leaves the middle as an estimate forever.
            while (rowsMissing() && spins < 4000) {
                contentY = originY
                forceLayout()
                const now = builtFromStart()
                spins++
                if (now === built) {
                    break
                }
                built = now
            }
            own = false
            filling = false
        }

        // Sending, or loading another conversation, shows the end again.
        // An upward scroll during a reply does not call this.
        function repin() {
            pinned = true
            fillFromStart()
            if (rowsMissing()) {
                // The model reset is still tearing the rows down.
                fillAttempts++
                if (fillAttempts < 8)
                    Qt.callLater(repin)
                return
            }
            fillAttempts = 0
            followRuns++
            goEnd()
            Qt.callLater(confirmPin)
        }

        readonly property bool overflow: contentHeight > height + endSlop
        readonly property bool showJumpTop: count > 0 && overflow && contentY > endSlop
        readonly property bool showJumpBottom: count > 0 && overflow
            && (contentY + height < contentHeight - endSlop)

        delegate: Item {
            objectName: "row-" + index
            width: list.width
            height: bubble.height
            // A reset that shrinks the model leaves the extra row in place
            // until the view drops it. That row's roles are already undefined.
            MessageBubble {
                id: bubble
                width: Math.min(Theme.chatMaxWidth, list.width - 48)
                anchors.horizontalCenter: parent.horizontalCenter
                role: model.role ?? "assistant"
                content: model.content ?? ""
                reasoning: model.reasoning ?? ""
                streaming: model.streaming ?? false
                parts: model.parts ?? []
                toolCalls: model.toolCalls ?? ""
                error: model.error ?? ""
                messageId: model.messageId ?? ""
            }
        }

        // A new row or a resized row. The pin decides; the input device does not.
        onCountChanged: queueFollow()
        onContentHeightChanged: queueFollow()
        onHeightChanged: {
            if (height <= 0)
                return
            if (rowsMissing()) {
                repin()
                return
            }
            queueFollow()
        }
        onContentYChanged: {
            if (own || filling || !userMoving())
                return
            pinned = atYEnd
            capture()
        }
        // A flick, a wheel, or a key lands here. The flags are already false
        // inside onMovementEnded, so the edges are these three.
        onMovingChanged: if (!moving) gestureEnded()
        onDraggingChanged: if (!dragging) gestureEnded()
        onFlickingChanged: if (!flicking) gestureEnded()

        ScrollBar.vertical: ScrollBar {
            id: vbar
            objectName: "chatBar"
            policy: ScrollBar.AlwaysOn
            implicitWidth: 12
            minimumSize: 0.08
            // Press does not set Flickable.moving. When it ends, the pin
            // matches the position the pointer left behind.
            onPressedChanged: {
                if (pressed || list.filling || list.own)
                    return
                list.gestureEnded()
            }
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

        Component.onCompleted: repin()
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
        objectName: "jumpTop"
        anchors.top: parent.top
        anchors.topMargin: 12
        anchors.right: parent.right
        anchors.rightMargin: 28
        visible: list.showJumpTop
        glyph: "↑"
        onClicked: {
            // Leave the end and stay there. Growth below must not pull back.
            list.pinned = false
            list.own = true
            list.contentY = list.originY
            list.own = false
            list.capture()
        }
    }

    JumpChip {
        objectName: "jumpBottom"
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 12
        anchors.right: parent.right
        anchors.rightMargin: 28
        visible: list.showJumpBottom
        glyph: "↓"
        onClicked: list.repin()
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
