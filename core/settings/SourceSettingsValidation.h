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
QString validateSourceSettingsDraftV2(const SettingsSchemaV2 &schema, const QVariantMap &draft);
SourceSettingsValidationV2 validateSourceSettingsV2(
    const SettingsSchemaV2 &schema, const QVariantMap &draft,
    const QVariantMap &previousParameters = {}, const QStringList &configuredSecretFieldIds = {},
    bool editing = false);
