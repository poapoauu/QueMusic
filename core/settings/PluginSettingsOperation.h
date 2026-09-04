#pragma once
#include "PluginManager.h"
#include "v2/IMusicSourceSessionV2.h"
#include <QPointer>
#include <memory>

// Internal C++ helper. Configuration and credentials never cross its public-result boundary.
class PluginSettingsOperation final : public QObject,
                                      public std::enable_shared_from_this<PluginSettingsOperation> {
    Q_OBJECT
public:
    PluginSettingsOperation(PluginManager *, SourceConfigurationV2, SettingsSchemaV2,
                            QString actionId = {}, bool confirmed = false, int deadlineMs = 15000);
    ~PluginSettingsOperation() override;
    QUuid requestId() const;
    void start();
    void cancel();
signals:
    void finished(QUuid requestId, QVariantMap result, QVariantList actions, QVariantList sourceCapabilities);
private:
    struct State;
    std::unique_ptr<State> d;
};
