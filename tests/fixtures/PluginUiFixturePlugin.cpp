#include "PluginUiFixturePlugin.h"

namespace {
class FixtureSession final : public IMusicSourceSessionV2 {
public:
    using IMusicSourceSessionV2::IMusicSourceSessionV2;
    SourceIdentityV2 identity() const override { return {}; }
    SourceSessionStateV2 state() const override { return SourceSessionStateV2::Closed; }
    CapabilitySetV2 capabilities() const override { return {}; }
    QUuid open() override { return {}; }
    void close() override {}
    void cancel(const QUuid &) override {}
};
}

SourceDescriptorV2 PluginUiFixturePlugin::descriptor() const
{
    SourceDescriptorV2 result;
    result.pluginPackageId = QStringLiteral(QUEMUSIC_UI_FIXTURE_PACKAGE_ID);
    result.sourceId = QStringLiteral(QUEMUSIC_UI_FIXTURE_SOURCE_ID);
    result.name = QStringLiteral("Plugin UI fixture");
    result.version = QStringLiteral("1.0.0");
    result.sdkAbi = QUEMUSIC_MUSIC_SOURCE_SDK_V2_ABI;
    return result;
}

IMusicSourceSessionV2 *PluginUiFixturePlugin::createSession(
    const SourceConfigurationV2 &, QObject *parent)
{
    return new FixtureSession(parent);
}

#if !defined(QUEMUSIC_UI_FIXTURE_NO_PROVIDER)
ManagementUiDescriptor PluginUiFixturePlugin::managementUi() const
{
    ManagementUiDescriptor result{QStringLiteral("1.0"),
                                  QUrl(QStringLiteral("qml/ManagementPage.qml")), true, true};
#if defined(QUEMUSIC_UI_FIXTURE_WRONG_VERSION)
    result.uiApiVersion = QStringLiteral("2.0");
#elif defined(QUEMUSIC_UI_FIXTURE_WRONG_PATH)
    result.componentUrl = QUrl(QStringLiteral("qml/OtherPage.qml"));
#elif defined(QUEMUSIC_UI_FIXTURE_NO_MODE)
    result.supportsCreate = false;
    result.supportsEdit = false;
#endif
    return result;
}

QObject *PluginUiFixturePlugin::createManagementBackend(
    const PluginUiContextData &, QObject *parent)
{
#if defined(QUEMUSIC_UI_FIXTURE_NULL_BACKEND)
    Q_UNUSED(parent);
    return nullptr;
#else
    return new QObject(parent);
#endif
}
#endif
