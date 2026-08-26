import QtQuick

Rectangle {
    id: root
    property string title: "Thought"
    property string body: ""
    property bool expanded: false
    property bool hideable: false
    property bool monospace: false
    property bool headerOnly: false
    property int previewChars: 3000
    readonly property string shown: {
        if (root.headerOnly)
            return ""
        const s = root.body
        if (s.length <= root.previewChars)
            return s
        return s.substring(0, root.previewChars) + "\n…"
    }
    signal hideRequested()
    signal expandToggled()

    readonly property int linePx: 18
    readonly property int collapsedPx: 10 * linePx
    width: parent ? parent.width : 400
    height: header.height + flick.height + 16
    color: "transparent"
    border.color: Theme.border
    border.width: 1
    radius: 10

    Row {
        id: header
        x: 10
        y: 6
        width: parent.width - 20
        spacing: 8
        Text {
            text: root.title
            color: Theme.muted
            font.pixelSize: 12
            anchors.verticalCenter: parent.verticalCenter
        }
        Text {
            visible: !root.headerOnly && metrics.implicitHeight > root.collapsedPx
            text: root.expanded ? "Show less" : "Show more"
            color: Theme.text
            font.pixelSize: 12
            anchors.verticalCenter: parent.verticalCenter
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: root.expandToggled()
            }
        }
        Text {
            visible: root.hideable
            text: "Hide"
            color: Theme.muted
            font.pixelSize: 12
            anchors.verticalCenter: parent.verticalCenter
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: root.hideRequested()
            }
        }
    }

    Text {
        id: metrics
        x: 10
        width: root.width - 20
        text: root.shown
        wrapMode: Text.Wrap
        font.pixelSize: 13
        font.family: root.monospace ? "monospace" : metrics.font.family
        opacity: 0
        enabled: false
        visible: !root.headerOnly
    }

    Flickable {
        id: flick
        x: 10
        y: header.height + 8
        width: parent.width - 20
        height: {
            if (root.headerOnly)
                return 0
            const cap = root.expanded ? 480 : root.collapsedPx
            const h = metrics.implicitHeight
            if (h <= 0 && root.shown.length > 0)
                return cap
            return Math.min(h, cap)
        }
        clip: true
        contentWidth: width
        contentHeight: bodyText.implicitHeight
        boundsBehavior: Flickable.StopAtBounds
        flickableDirection: Flickable.VerticalFlick
        interactive: contentHeight > height
        property bool stickToEnd: true
        Text {
            id: bodyText
            width: flick.width
            text: root.shown
            color: Theme.muted
            wrapMode: Text.Wrap
            font.pixelSize: 13
            font.family: root.monospace ? "monospace" : bodyText.font.family
        }
        onMovementEnded: stickToEnd = (contentHeight <= height) || (contentY + height >= contentHeight - 24)
        onDraggingChanged: {
            if (dragging && contentY + height < contentHeight - 24)
                stickToEnd = false
        }
        onContentHeightChanged: {
            if (stickToEnd && contentHeight > height)
                contentY = Math.max(0, contentHeight - height)
        }
    }
}
