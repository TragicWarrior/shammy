pragma Singleton
import QtQuick

QtObject {
    property bool dark: true

    // ChatGPT desktop dark: charcoal surfaces, light type, almost no chroma.
    readonly property color bg: dark ? "#212121" : "#ffffff"
    readonly property color sidebar: dark ? "#171717" : "#f9f9f9"
    readonly property color panel: dark ? "#2f2f2f" : "#f4f4f4"
    readonly property color hover: dark ? "#2a2a2a" : "#ececec"
    readonly property color selected: dark ? "#2f2f2f" : "#e8e8e8"
    readonly property color selection: dark ? "#5a5a5a" : "#c8c8c8"
    readonly property color composer: dark ? "#303030" : "#ffffff"
    readonly property color border: dark ? "#424242" : "#e5e5e5"
    readonly property color hairline: dark ? "#2e2e2e" : "#ececec"
    readonly property color text: dark ? "#ececec" : "#0d0d0d"
    readonly property color muted: dark ? "#b4b4b4" : "#8e8e8e"
    readonly property color accent: dark ? "#ffffff" : "#0d0d0d"
    readonly property color accentDim: dark ? "#3d3d3d" : "#ececec"
    readonly property color userBubble: dark ? "#2f2f2f" : "#f4f4f4"
    readonly property color sendBg: dark ? "#ffffff" : "#0d0d0d"
    readonly property color sendFg: dark ? "#0d0d0d" : "#ffffff"
    readonly property color sendDisabled: dark ? "#676767" : "#c6c6c6"
    readonly property color codeBg: dark ? "#0d0d0d" : "#f7f7f8"
    readonly property color warning: "#f59e0b"
    readonly property color privacy: "#7c3aed"
    readonly property color privacyFg: "#ececec"
    readonly property color danger: "#ef4444"
    readonly property color tool: dark ? "#262626" : "#f4f4f4"
    readonly property color avatar: "#000000"
    readonly property int radius: 12
    readonly property int radiusMd: 18
    readonly property int radiusLg: 26
    readonly property int sidebarWidth: 260
    readonly property int artifactWidth: 420
    readonly property int chatMaxWidth: 768
}
