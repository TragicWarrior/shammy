import QtQuick
import QtQuick.Controls

Item {
    id: root
    property int mode: 0
    property bool previewEnabled: true
    readonly property bool previewActive: previewEnabled && settings.hasWebEngine
        && chat.currentArtifactCanPreview

    function reload() {
        if (previewLoader.item)
            previewLoader.item.reload()
    }

    onModeChanged: {
        if (mode === 0)
            Qt.callLater(reload)
    }

    Row {
        id: modeRow
        spacing: 6
        Repeater {
            model: ["Preview", "Source"]
            Rectangle {
                required property int index
                required property string modelData
                height: 32
                width: modeLab.implicitWidth + 24
                radius: 16
                color: root.mode === index ? Theme.text : "transparent"
                border.color: root.mode === index ? Theme.text : Theme.border
                border.width: 1
                Text {
                    id: modeLab
                    anchors.centerIn: parent
                    text: modelData
                    color: root.mode === index ? Theme.bg : Theme.muted
                    font.pixelSize: 13
                    font.weight: root.mode === index ? Font.DemiBold : Font.Normal
                }
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.mode = index
                }
            }
        }
    }

    Item {
        anchors.top: modeRow.bottom
        anchors.topMargin: 10
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom

        Loader {
            id: previewLoader
            anchors.fill: parent
            active: root.previewActive
            visible: true
            opacity: root.mode === 0 && status === Loader.Ready ? 1 : 0
            source: active ? Qt.resolvedUrl("ArtifactWebPreview.qml") : ""
        }
        Text {
            visible: root.mode === 0 && !previewLoader.active
            anchors.centerIn: parent
            width: parent.width - 32
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignHCenter
            color: Theme.muted
            font.pixelSize: 13
            text: {
                if (!settings.hasWebEngine)
                    return "HTML preview needs Qt WebEngine."
                return "Preview is for HTML, SVG, and markdown. Open Source to read this file."
            }
        }
        Column {
            visible: root.mode === 0 && previewLoader.status === Loader.Error
            anchors.centerIn: parent
            width: parent.width - 40
            spacing: 10
            z: 3
            Text {
                width: parent.width
                wrapMode: Text.Wrap
                horizontalAlignment: Text.AlignHCenter
                color: Theme.text
                font.pixelSize: 14
                font.weight: Font.DemiBold
                text: "HTML preview could not start."
            }
            Text {
                width: parent.width
                wrapMode: Text.Wrap
                horizontalAlignment: Text.AlignHCenter
                color: Theme.muted
                font.pixelSize: 13
                text: {
                    const err = previewLoader.errorString
                    if (err.indexOf("QtWebEngine") >= 0)
                        return "The Qt 6 WebEngine QML module is missing. On Debian/Ubuntu:\n\nsudo apt-get install qml6-module-qtwebengine\n\nThen restart Shammy."
                    return err
                }
            }
        }
        Text {
            visible: root.mode === 0 && previewLoader.item
                && previewLoader.item.statusText
                && previewLoader.item.statusText.length > 0
            anchors.centerIn: parent
            width: parent.width - 32
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignHCenter
            color: Theme.muted
            font.pixelSize: 13
            z: 2
            text: previewLoader.item ? previewLoader.item.statusText : ""
        }

        ScrollView {
            anchors.fill: parent
            visible: root.mode === 1
            clip: true
            z: 1
            TextArea {
                readOnly: true
                wrapMode: TextEdit.Wrap
                text: chat.currentArtifactContent
                color: Theme.text
                font.family: "monospace"
                font.pixelSize: 12
                background: Rectangle { color: Theme.bg }
                selectByMouse: true
            }
        }
    }
}
