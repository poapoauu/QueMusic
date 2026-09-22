.pragma library

function translated(labelKey, fallback) {
    if (labelKey) {
        var value = qsTrId(labelKey)
        if (value && value !== labelKey)
            return value
    }
    return fallback || qsTr("Unavailable")
}

function availability(availabilityValue, reasonKey) {
    if (availabilityValue === 1)
        return qsTr("Available")
    if (reasonKey === "source.settings.forbidden")
        return qsTr("Forbidden by source policy")
    if (reasonKey === "source.settings.unavailable")
        return qsTr("Temporarily unavailable")
    return qsTr("Unavailable")
}

function pluginState(state) {
    if (state === "unloaded")
        return qsTr("Unloaded")
    if (state === 0 || state === "discovered")
        return qsTr("Discovered")
    if (state === 1 || state === "loading")
        return qsTr("Loading")
    if (state === 2 || state === "loaded")
        return qsTr("Loaded")
    if (state === 3 || state === "failed")
        return qsTr("Failed")
    if (state === 4 || state === "unloading")
        return qsTr("Unloading")
    return qsTr("Unknown")
}

function actionName(action) {
    var names = [qsTr("Play"), qsTr("Artwork"), qsTr("Lyrics"), qsTr("Download"),
                 qsTr("Favorite"), qsTr("Unfavorite"), qsTr("Rating"), qsTr("Scrobble"),
                 qsTr("Create playlist"), qsTr("Update playlist"), qsTr("Delete playlist"),
                 qsTr("Add playlist tracks"), qsTr("Remove playlist tracks"),
                 qsTr("Fetch play queue"), qsTr("Save play queue"), qsTr("Fetch bookmarks"),
                 qsTr("Create bookmark"), qsTr("Delete bookmark")]
    return action >= 0 && action < names.length ? names[action] : qsTr("Unknown action")
}

function layerAvailability(state, currentDraftProbed) {
    return state === 2 && !currentDraftProbed ? qsTr("Not checked") : availability(state, "")
}

function sessionState(state) {
    if (state === 0)
        return qsTr("Not connected")
    if (state === 1)
        return qsTr("Connecting")
    if (state === 2)
        return qsTr("Ready")
    if (state === 3)
        return qsTr("Authentication required")
    if (state === 4)
        return qsTr("Failed")
    return qsTr("Unknown")
}

function runtimeState(state) {
    if (state === -1 || state === undefined || state === null)
        return qsTr("Not checked")
    if (state === 1)
        return qsTr("Available now")
    if (state === 0)
        return qsTr("Unavailable now")
    return qsTr("Not checked")
}

function error(lastErrorKey) {
    if (!lastErrorKey)
        return ""
    if (lastErrorKey === "source.settings.invalid")
        return qsTr("Check the visible settings and try again")
    if (lastErrorKey === "source.settings.forbidden")
        return qsTr("This operation is not allowed")
    return qsTr("Plugin settings are unavailable")
}

function configuredSecretPlaceholder(configured) {
    return configured ? qsTr("Configured — enter a value to replace it")
                      : qsTr("Enter credential")
}
