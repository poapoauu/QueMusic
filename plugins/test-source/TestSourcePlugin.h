#pragma once

#include "IMusicSourcePlugin.h"

class TestSourcePlugin final : public QObject, public IMusicSourcePlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID QUEMUSIC_MUSIC_SOURCE_PLUGIN_IID FILE "plugin.json")
    Q_INTERFACES(IMusicSourcePlugin)

public:
    SourceDescriptor descriptor() const override;
    bool initialize(SourcePluginContext &context) override;
    IMusicSourceSession *createSession(const SourceAccount &account, QObject *parent) override;
};
