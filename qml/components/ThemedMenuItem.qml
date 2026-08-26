import QtQuick
import QtQuick.Controls

MenuItem {
    id: root
    property string iconKind: ""
    readonly property string shownIcon: {
        if (iconKind.length > 0)
            return iconKind
        const sub = root.subMenu
        if (sub && sub["iconKind"])
            return sub["iconKind"]
        return ""
    }
    property bool destructive: false
    implicitHeight: 34
    implicitWidth: 240
    indicator: Item { implicitWidth: 0; implicitHeight: 0 }
    background: Rectangle {
        radius: 8
        color: root.highlighted ? Theme.hover : "transparent"
    }
    contentItem: Item {
        implicitHeight: 34
        MenuIcon {
            id: glyph
            visible: root.shownIcon.length > 0
            kind: root.shownIcon
            stroke: root.destructive ? Theme.danger : Theme.text
            anchors.verticalCenter: parent.verticalCenter
            anchors.left: parent.left
            anchors.leftMargin: 10
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            anchors.left: parent.left
            anchors.leftMargin: glyph.visible ? 32 : 10
            anchors.right: mark.left
            anchors.rightMargin: 6
            text: root.text
            color: root.destructive ? Theme.danger : Theme.text
            font.pixelSize: 13
            elide: Text.ElideRight
            opacity: root.enabled ? 1 : 0.45
        }
        Text {
            id: mark
            visible: root.subMenu || root.checked
            width: visible ? implicitWidth : 0
            anchors.verticalCenter: parent.verticalCenter
            anchors.right: parent.right
            anchors.rightMargin: 8
            text: root.subMenu ? "▸" : "✓"
            color: root.subMenu ? Theme.muted : Theme.text
            font.pixelSize: root.subMenu ? 12 : 13
        }
    }
}
