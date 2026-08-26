import QtQuick
import QtWebEngine

WebEngineView {
    id: view
    property string statusText: ""
    url: chat.currentArtifactPreviewUrl
    backgroundColor: Theme.bg

    settings.javascriptEnabled: true
    settings.localContentCanAccessFileUrls: true
    settings.localContentCanAccessRemoteUrls: true
    settings.allowRunningInsecureContent: true
    settings.javascriptCanOpenWindows: false
    settings.errorPageEnabled: true
    settings.focusOnNavigationEnabled: false

    onLoadingChanged: function(request) {
        if (request.status === WebEngineView.LoadStartedStatus)
            statusText = "Loading…"
        else if (request.status === WebEngineView.LoadFailedStatus)
            statusText = request.errorString.length ? request.errorString : "Preview failed to load."
        else if (request.status === WebEngineView.LoadSucceededStatus)
            statusText = ""
    }
}
