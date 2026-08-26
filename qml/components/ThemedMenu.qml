import QtQuick
import QtQuick.Controls

Menu {
    property string iconKind: ""
    padding: 6
    overlap: 4
    delegate: ThemedMenuItem {}
    background: Rectangle {
        implicitWidth: 248
        color: Theme.sidebar
        radius: 12
        border.color: Theme.border
        border.width: 1
    }
}
