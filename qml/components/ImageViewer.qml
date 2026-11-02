import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Pop-out image viewer with zoom, full screen, copy controls, and an
// auto-hiding control bar that fades in on mouse activity.
Window {
    id: root
    property string path: ""
    property url source: ""
    property string prompt: ""
    property int imageWidth: 0
    property int imageHeight: 0
    property bool fitMode: true
    property real zoom: 1.0   // 1.0 = actual pixels

    // Current zoom accounting for fit mode.
    readonly property real fitZoom: (imageWidth > 0 && imageHeight > 0)
            ? Math.min(flick.width / imageWidth, flick.height / imageHeight) : 1
    readonly property real shownZoom: fitMode ? fitZoom : zoom

    color: "black"
    visible: false
    minimumWidth: 480
    minimumHeight: 360

    // Window title with a truncated prompt.
    title: "Image" + (prompt.length ? ": " + prompt.substring(0, 60) : "")

    // Open the window, sizing it to the image (capped at 80 % of screen).
    function open() {
        if (!visible) {
            var maxW = Math.floor(Screen.desktopAvailableWidth * 0.8)
            var maxH = Math.floor(Screen.desktopAvailableHeight * 0.8)
            if (maxW < 480) maxW = 480
            if (maxH < 360) maxH = 360
            if (imageWidth > 0 && imageHeight > 0) {
                var w = imageWidth
                var h = imageHeight
                if (w > maxW || h > maxH) {
                    var scale = Math.min(maxW / w, maxH / h)
                    w = Math.round(w * scale)
                    h = Math.round(h * scale)
                }
                width = Math.max(w, 480)
                height = Math.max(h, 360)
            } else {
                width = Math.max(width, 480)
                height = Math.max(height, 360)
            }
            show()
            raise()
            requestActivate()
        }
        fit()
    }

    // Switch to fit mode and re-apply the fit zoom.
    function fit() {
        fitMode = true
        applyZoom(fitZoom, false)
    }

    // Re-fit if still in fit mode. Queued re-fits call this, so a zoom the user
    // chose after the re-fit was queued is not undone when it runs.
    function refit() {
        if (fitMode) fit()
    }

    // Zoom by a factor relative to the currently shown zoom.
    function zoomBy(f) {
        var z = shownZoom * f
        if (z < 0.05) z = 0.05
        if (z > 8) z = 8
        fitMode = false
        zoom = z
        applyZoom(z, true)
    }

    // Resize the content to image * z. With keepCentre, the image point at the
    // centre of the view stays there; otherwise the image is shown from its
    // top-left corner (centred by the margins when it is smaller than the view).
    function applyZoom(z, keepCentre) {
        // The centre of the view as a fraction of the image, before resizing.
        const fx = flick.contentWidth > 0
                ? (flick.contentX + flick.width / 2) / flick.contentWidth : 0.5
        const fy = flick.contentHeight > 0
                ? (flick.contentY + flick.height / 2) / flick.contentHeight : 0.5
        flick.contentWidth = imageWidth * z
        flick.contentHeight = imageHeight * z
        if (keepCentre) {
            flick.contentX = fx * flick.contentWidth - flick.width / 2
            flick.contentY = fy * flick.contentHeight - flick.height / 2
        } else {
            flick.contentX = -flick.leftMargin
            flick.contentY = -flick.topMargin
        }
        flick.returnToBounds()
    }

    // Toggle full-screen mode.
    function toggleFullScreen() {
        if (root.visibility === Window.FullScreen) {
            root.showNormal()
        } else {
            root.showFullScreen()
        }
    }

    // Copy the image to clipboard; show status for 2 s; poke the bar.
    function copyImage() {
        var ok = chat.copyImageToClipboard(path)
        copyStatus.text = ok ? "Copied" : "Could not copy"
        statusTimer.restart()
        poke()
    }

    // Show the bar and restart the idle timer.
    function poke() {
        bar.shown = true
        idleTimer.restart()
    }


    // ------------------------------------------------------------------
    // Image container – no bindings on contentWidth/contentHeight;
    // only resizeContent() sets them.
    // ------------------------------------------------------------------
    Flickable {
        id: flick
        objectName: "flick"
        anchors.fill: parent
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        leftMargin: Math.max(0, (width - contentWidth) / 2)
        topMargin: Math.max(0, (height - contentHeight) / 2)

        // Re-fit when the view itself changes size (refit() keeps a zoom the user
        // chose). Watching the view and not the window matters: on a real
        // display the view is sized only after the window manager answers, which
        // is after open(), so a fit done then would fit into a 0x0 view.
        onWidthChanged: {
            Qt.callLater(root.refit)
        }
        onHeightChanged: {
            Qt.callLater(root.refit)
        }

        Image {
            width: flick.contentWidth
            height: flick.contentHeight
            source: root.source
            asynchronous: true
            smooth: true
            mipmap: true
            fillMode: Image.Stretch
        }
    }

    // ------------------------------------------------------------------
    // Mouse interaction handlers (children of the Window / contentItem).
    // ------------------------------------------------------------------
    WheelHandler {
        onWheel: (event) => root.zoomBy(event.angleDelta.y > 0 ? 1.25 : 0.8)
    }

    HoverHandler {
        id: activity
        // Where the pointer was last seen. Qt Quick re-sends hover updates on every
        // repainted frame (the bar's own fade is one), so only a real move counts.
        property point lastPos: Qt.point(-1, -1)
        onPointChanged: {
            const p = point.scenePosition
            if (p.x !== lastPos.x || p.y !== lastPos.y) {
                lastPos = p
                root.poke()
            }
        }
        cursorShape: (root.visibility === Window.FullScreen && !bar.shown)
                         ? Qt.BlankCursor : Qt.ArrowCursor
    }

    // ------------------------------------------------------------------
    // Auto-hiding control bar.
    // ------------------------------------------------------------------
    Rectangle {
        id: bar
        objectName: "controlBar"
        anchors.top: parent.top
        width: parent.width
        height: 40
        color: "#80000000"

        property bool shown: false
        opacity: bar.shown ? 1 : 0
        visible: opacity > 0
        Behavior on opacity { NumberAnimation { duration: bar.shown ? 150 : 600 } }

        HoverHandler { id: barHover }

        RowLayout {
            anchors.fill: parent
            anchors.margins: 10
            spacing: 16

            // Zoom out
            Text {
                text: "\u2212"
                color: "white"
                font.pixelSize: 14
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.zoomBy(0.8)
                }
            }

            // Current zoom percentage label
            Text {
                objectName: "zoomLabel"
                text: Math.round(root.shownZoom * 100) + "%"
                color: "white"
                font.pixelSize: 12
            }

            // Zoom in
            Text {
                text: "+"
                color: "white"
                font.pixelSize: 14
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.zoomBy(1.25)
                }
            }

            // Fit to window
            Text {
                text: "Fit"
                color: "white"
                font.pixelSize: 12
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.fit()
                }
            }

            Item { Layout.fillWidth: true }

            // Copy status message (empty by default)
            Text {
                id: copyStatus
                color: "white"
                font.pixelSize: 12
            }

            // Copy the image
            Text {
                text: "Copy"
                color: "white"
                font.pixelSize: 12
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.copyImage()
                }
            }

            // Toggle full-screen
            Text {
                text: root.visibility === Window.FullScreen ? "Exit full screen" : "Full screen"
                color: "white"
                font.pixelSize: 12
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.toggleFullScreen()
                }
            }
        }
    }

    // Idle timer: fade the bar out after no mouse activity.
    Timer {
        id: idleTimer
        interval: 1500
        onTriggered: {
            if (!barHover.hovered && !statusTimer.running) {
                bar.shown = false
            } else {
                restart()
            }
        }
    }

    // Status message timer: clear the copy status after 2 s.
    Timer {
        id: statusTimer
        interval: 2000
        onTriggered: copyStatus.text = ""
    }

    // ------------------------------------------------------------------
    // Hotkeys.
    // ------------------------------------------------------------------
    Shortcut {
        sequences: ["+"]
        onActivated: root.zoomBy(1.25)
    }
    Shortcut {
        sequences: ["="]
        onActivated: root.zoomBy(1.25)
    }
    Shortcut {
        sequences: ["-"]
        onActivated: root.zoomBy(0.8)
    }
    Shortcut {
        sequence: "0"
        onActivated: root.fit()
    }
    Shortcut {
        sequence: "F"
        onActivated: root.toggleFullScreen()
    }
    Shortcut {
        sequences: [StandardKey.Copy]
        onActivated: root.copyImage()
    }
    Shortcut {
        sequence: "Esc"
        onActivated: {
            if (root.visibility === Window.FullScreen) {
                root.showNormal()
            } else {
                root.close()
            }
        }
    }
}
