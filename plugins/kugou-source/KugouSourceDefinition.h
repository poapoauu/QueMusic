#pragma once
#include "v2/IMusicSourcePluginV2.h"

// Plugin-private definition shared by the native factory and test-only factory.
// This is not a Source SDK extension or a transport injection API.
namespace KugouSourceDefinition {
inline SourceDescriptorV2 descriptor()
{
    SourceDescriptorV2 result;
    result.pluginPackageId = "org.quemusic.source.kugou";
    result.sourceId = "kugou";
    result.name = "Kugou (experimental anonymous)";
    result.version = "0.1.0";
    result.sdkAbi = QUEMUSIC_MUSIC_SOURCE_SDK_V2_ABI;
    result.declaredActions.insert(SourceActionV2::Play, {AvailabilityV2::Available, {}, {}});
    return result;
}
inline SettingsSchemaV2 settingsSchema()
{
    // Optional, backend-only storage. Anonymous instances remain supported;
    // storing a record never certifies its token or grants account capabilities.
    SettingsFieldV2 credential{QStringLiteral("credentialBundle"),
        QStringLiteral("kugou.settings.credentialBundle"), SettingsFieldTypeV2::Secret};
    credential.secret = true;
    return {{QStringLiteral("account"), QStringLiteral("kugou.settings.account"), {credential}, {}}};
}
}
