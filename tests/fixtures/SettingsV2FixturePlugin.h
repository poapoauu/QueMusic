#pragma once

#include "v2/SourceV2Types.h"
#include <QPointer>
#include <QObject>
#include <functional>

// Allocated by the host test, never module-static. Kept alive until manager teardown.
struct SettingsV2FixtureControl {
    enum Mode { Inline, Delayed, Failure, Unstarted, WrongThenCorrect, WrongOnly, Duplicate,
                ReadyThenAuth, TerminalBeforeStarted, FailureBeforeStarted, NestedForeign };
    Mode openMode = Inline;
    Mode actionMode = Inline;
    bool noActionProvider = false;
    bool invalidIdentity = false;
    bool revokeReadyInCapabilities = false;
    QObject *forcedParent = nullptr;
    bool requiresConfirmation = false;
    bool missingAccountGrant = false;
    bool invalidSchema = false;
    AvailabilityV2 accountGrant = AvailabilityV2::Available;
    QVariantMap actionConstraints;
    CapabilitySetV2 musicCapabilities{
        {{SourceActionV2::Play, {AvailabilityV2::Available, "PRIVATE", {}}},
         {SourceActionV2::Favorite, {AvailabilityV2::Available, "PRIVATE", {}}}},
        {{SourceActionV2::Play, {AvailabilityV2::Forbidden, "PRIVATE", {}}}}};
    int created = 0, opened = 0, closed = 0, destroyed = 0, invoked = 0, canceled = 0;
    QStringList events;
    SourceConfigurationV2 lastConfiguration;
    QPointer<QObject> lastSession;
    std::function<void(const QString &, QObject *)> callback;
    void event(const QString &name, QObject *session) {
        events.append(name);
        if (callback) callback(name, session);
    }
};
