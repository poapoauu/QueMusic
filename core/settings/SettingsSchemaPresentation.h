#pragma once
#include "v2/SourceV2Types.h"

QString settingsAvailabilityReasonV2(AvailabilityV2 state);
std::optional<SettingsSchemaV2> detachSettingsSchemaV2(const SettingsSchemaV2 &schema);
struct BackendSecretPresentationV1 {
    QStringList fieldIds;
    SettingsSchemaV2 schema;
};
std::optional<BackendSecretPresentationV1> backendSecretPresentationV1(
    const SettingsSchemaV2 &schema, const QStringList &backendSecretFields);
SourceDescriptorV2 detachSettingsDescriptorV2(const SourceDescriptorV2 &descriptor);
QVariantList settingsSectionsPresentationV2(const SettingsSchemaV2 &schema,
    const QVariantMap &draft, const QVariantMap &previous, const QStringList &configured);
QVariantList sourceCapabilitiesPresentationV2(const QHash<SourceActionV2, ActionAvailabilityV2> &declared,
    const std::optional<CapabilitySetV2> &runtime = {});
QVariantList settingsActionsPresentationV2(const SettingsSchemaV2 &schema,
    const std::optional<SettingsActionCapabilitiesV2> &capabilities = {},
    bool providerSupported = true);
