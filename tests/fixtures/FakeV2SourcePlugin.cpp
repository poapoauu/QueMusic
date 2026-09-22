#include "FakeV2SourcePlugin.h"

#if !defined(QUEMUSIC_FAKE_V2_OMIT_INTERFACE)

namespace {

class FakeV2SourceSession final : public IMusicSourceSessionV2 {
public:
    using IMusicSourceSessionV2::IMusicSourceSessionV2;

    SourceIdentityV2 identity() const override { return {}; }
    SourceSessionStateV2 state() const override { return SourceSessionStateV2::Closed; }
    CapabilitySetV2 capabilities() const override { return {}; }
    QUuid open() override
    {
        const QUuid requestId = QUuid::createUuid();
        emit requestStarted(requestId);
        return requestId;
    }
    void close() override { }
    void cancel(const QUuid &) override { }
};

}

SourceDescriptorV2 FakeV2SourcePlugin::descriptor() const
{
    SourceDescriptorV2 descriptor;
    descriptor.pluginPackageId = QStringLiteral("org.quemusic.source.fixture-v2");
    descriptor.sourceId = QStringLiteral("fixture-v2");
    descriptor.name = QStringLiteral("Fixture V2");
    descriptor.version = QStringLiteral("2.0.0");
    descriptor.sdkAbi = QUEMUSIC_MUSIC_SOURCE_SDK_V2_ABI;
    return descriptor;
}

IMusicSourceSessionV2 *FakeV2SourcePlugin::createSession(
    const SourceConfigurationV2 &, QObject *parent)
{
    return new FakeV2SourceSession(parent);
}

#endif
