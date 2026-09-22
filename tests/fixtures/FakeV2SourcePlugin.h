#pragma once

#include "v2/IMusicSourcePluginV2.h"

#include <QObject>

#if defined(QUEMUSIC_FAKE_V2_OMIT_INTERFACE)
class FakeV2SourcePlugin final : public QObject {
#else
class FakeV2SourcePlugin final : public QObject, public IMusicSourcePluginV2 {
#endif
    Q_OBJECT
    Q_PLUGIN_METADATA(IID QUEMUSIC_MUSIC_SOURCE_PLUGIN_V2_IID)
#if !defined(QUEMUSIC_FAKE_V2_OMIT_INTERFACE)
    Q_INTERFACES(IMusicSourcePluginV2)
#endif
    Q_PROPERTY(int sourceSdkAbi READ sourceSdkAbi CONSTANT)

public:
    int sourceSdkAbi() const { return QUEMUSIC_MUSIC_SOURCE_SDK_V2_ABI; }

#if !defined(QUEMUSIC_FAKE_V2_OMIT_INTERFACE)
    SourceDescriptorV2 descriptor() const override;
    IMusicSourceSessionV2 *createSession(const SourceConfigurationV2 &configuration,
                                         QObject *parent) override;
#endif
};
