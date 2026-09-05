#pragma once

#include "v2/SourceV2Types.h"
#include "v2/SourceSecretsV2.h"

// Value-only host result. Never forward secretUpdates or unvalidated schema defaults to QML.
struct SourceSettingsValidationV2 {
    QString errorKey;
    QVariantMap parameters;
    SourceNamedSecretsV2 secretUpdates;
    QStringList secretFieldIds;
    QStringList requiredSecretFieldIds;
    bool preserveOmittedSecrets = true;
};

// Checks the complete schema and supplied draft, without requiring omitted fields.
// The two-argument preflight defers only conditional requiredness because an
// existing value may supply the condition context after metadata is read.
QString validateSourceSettingsDraftV2(const SettingsSchemaV2 &schema, const QVariantMap &draft);
QString validateSourceSettingsDraftV2(const SettingsSchemaV2 &schema, const QVariantMap &draft,
    const QVariantMap &previousParameters);
QVariantMap sourceSettingsPublicValuesV2(const SettingsSchemaV2 &schema,
    const QVariantMap &draft, const QVariantMap &previous = {});
bool sourceSettingsFieldVisibleV2(const SettingsFieldV2 &field, const QVariantMap &values);
SourceSettingsValidationV2 validateSourceSettingsV2(
    const SettingsSchemaV2 &schema, const QVariantMap &draft,
    const QVariantMap &previousParameters = {}, const QStringList &configuredSecretFieldIds = {},
    bool editing = false);
