import QtQuick
import QtQuick.Controls

Item {
    id: root
    property string json: ""
    width: parent ? parent.width : 400
    implicitHeight: flick.height
    height: implicitHeight

    property var table: ({ "headers": [], "rows": [] })
    property var headers: table.headers ? table.headers : []
    property var rows: table.rows ? table.rows : []
    property int cols: headers.length
    property int cellW: {
        if (cols <= 0)
            return 120
        const fitted = Math.floor((root.width - 2) / cols)
        return Math.max(110, Math.min(200, fitted))
    }

    onJsonChanged: load()
    Component.onCompleted: load()

    function load() {
        try {
            table = JSON.parse(json)
        } catch (e) {
            table = { "headers": [], "rows": [] }
        }
    }

    Flickable {
        id: flick
        width: root.width
        height: gridCol.implicitHeight
        clip: true
        contentWidth: Math.max(width, cols * cellW)
        contentHeight: gridCol.implicitHeight
        boundsBehavior: Flickable.StopAtBounds
        flickableDirection: contentWidth > width ? Flickable.HorizontalFlick : Flickable.AutoFlickIfNeeded
        interactive: contentWidth > width

        Column {
            id: gridCol
            width: Math.max(flick.width, cols * cellW)

            Row {
                Repeater {
                    model: headers
                    Rectangle {
                        required property var modelData
                        width: cellW
                        height: Math.max(32, headTxt.implicitHeight + 14)
                        color: Theme.panel
                        border.color: Theme.border
                        border.width: 1
                        Text {
                            id: headTxt
                            anchors.fill: parent
                            anchors.margins: 7
                            text: String(modelData)
                            color: Theme.text
                            font.pixelSize: 13
                            font.weight: Font.DemiBold
                            wrapMode: Text.Wrap
                        }
                    }
                }
            }

            Repeater {
                model: rows
                Row {
                    id: tableRow
                    required property var modelData
                    required property int index
                    Repeater {
                        model: {
                            const row = tableRow.modelData
                            const cells = []
                            for (let c = 0; c < root.cols; ++c)
                                cells.push((row && c < row.length) ? row[c] : "")
                            return cells
                        }
                        Rectangle {
                            required property var modelData
                            width: cellW
                            height: Math.max(28, bodyTxt.implicitHeight + 12)
                            color: tableRow.index % 2 ? Theme.hover : Theme.bg
                            border.color: Theme.border
                            border.width: 1
                            Text {
                                id: bodyTxt
                                anchors.fill: parent
                                anchors.margins: 7
                                text: String(modelData)
                                color: Theme.text
                                font.pixelSize: 13
                                wrapMode: Text.Wrap
                            }
                        }
                    }
                }
            }
        }

        ScrollBar.horizontal: ScrollBar {
            policy: flick.contentWidth > flick.width ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff
        }
    }
}
