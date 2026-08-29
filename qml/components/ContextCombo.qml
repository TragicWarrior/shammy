import QtQuick
import QtQuick.Controls

// Editable context-size picker: common presets plus free text for custom sizes.
ComboBox {
    id: ctx

    // Current value shown, e.g. "16K". Set by the owner from the model.
    property string value: ""
    // Emitted when the user picks a preset, presses Enter, or leaves the field.
    signal committed(string text)

    editable: true
    implicitHeight: 28
    implicitWidth: 108
    model: ["8K", "16K", "32K", "48K", "64K", "96K", "112K", "128K", "192K", "224K", "256K", "512K", "1M"]

    Component.onCompleted: editText = value
    // Resync from the model when the value changes externally, but never while
    // the user is mid-edit.
    onValueChanged: if (!activeFocus && editText !== value) editText = value
    onActivated: committed(currentText)
    onAccepted: committed(editText)

    background: Rectangle {
        implicitHeight: 28
        color: Theme.panel
        radius: 8
        border.color: Theme.border
        border.width: 1
    }
    contentItem: TextField {
        leftPadding: 10
        rightPadding: 24
        topPadding: 0
        bottomPadding: 0
        text: ctx.editText
        color: Theme.text
        font.pixelSize: 12
        verticalAlignment: Text.AlignVCenter
        readOnly: ctx.down
        selectionColor: Theme.selected
        selectedTextColor: Theme.text
        background: Item {}
        onEditingFinished: ctx.committed(text)
    }
    indicator: Item {
        implicitWidth: 26
        height: ctx.height
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        z: 2
        Text {
            text: "▾"
            color: Theme.muted
            anchors.centerIn: parent
            font.pixelSize: 11
        }
        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: ctx.popup.opened ? ctx.popup.close() : ctx.popup.open()
        }
    }
    popup: Popup {
        y: ctx.height + 2
        width: ctx.width
        implicitHeight: Math.min(contentHeight + 8, 260)
        padding: 4
        background: Rectangle {
            color: Theme.sidebar
            radius: 8
            border.color: Theme.border
        }
        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: ctx.popup.visible ? ctx.delegateModel : null
            currentIndex: ctx.highlightedIndex
            ScrollIndicator.vertical: ScrollIndicator {}
        }
    }
    delegate: ItemDelegate {
        width: ctx.width
        height: 28
        required property var modelData
        required property int index
        contentItem: Text {
            text: modelData
            color: Theme.text
            font.pixelSize: 12
            verticalAlignment: Text.AlignVCenter
            leftPadding: 8
        }
        background: Rectangle {
            radius: 6
            color: ctx.highlightedIndex === index ? Theme.hover : "transparent"
        }
    }
}
