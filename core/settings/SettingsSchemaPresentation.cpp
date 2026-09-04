#include "SettingsSchemaPresentation.h"
#include "SourceSettingsValidation.h"

namespace {
QString copyString(const QString &s) { return QString(s.constData(), s.size()); }
QVariant copyPrimitive(const QVariant &v)
{
    if (v.metaType().id() == QMetaType::QString) return copyString(v.toString());
    if (v.metaType().id() == QMetaType::Bool) return v.toBool();
    if (!v.isValid()) return {};
    return v.toLongLong();
}
QVariantMap copyMap(const QVariantMap &map)
{
    QVariantMap result;
    for (auto it = map.cbegin(); it != map.cend(); ++it) result.insert(copyString(it.key()), copyPrimitive(it.value()));
    return result;
}
bool sdkNumericType(const QVariant &value)
{
    switch (value.metaType().id()) {
    case QMetaType::Int: case QMetaType::UInt: case QMetaType::LongLong:
    case QMetaType::ULongLong: case QMetaType::Float: case QMetaType::Double:
        return true;
    default: return false;
    }
}
ActionAvailabilityV2 copyAvailability(const ActionAvailabilityV2 &value)
{
    ActionAvailabilityV2 copy{value.state, {}, {}};
    if (int(copy.state) < 0 || int(copy.state) > int(AvailabilityV2::Forbidden)) copy.state = AvailabilityV2::Unavailable;
    // Retain only SDK-understood primitive constraints; unknown data still fails closed.
    for (auto it = value.constraints.cbegin(); it != value.constraints.cend(); ++it) {
        if (it.key() == "sameSourceOnly" && it.value().metaType().id() == QMetaType::Bool)
            copy.constraints.insert(QStringLiteral("sameSourceOnly"), it.value().toBool());
        else if (it.key() == "maxBitrate" && sdkNumericType(it.value()))
            copy.constraints.insert(QStringLiteral("maxBitrate"), it.value().toDouble());
        else copy.constraints.insert(QStringLiteral("unsupported"), true);
    }
    return copy;
}
}

std::optional<SettingsSchemaV2> detachSettingsSchemaV2(const SettingsSchemaV2 &schema)
{
    if (!validateSourceSettingsDraftV2(schema, {}).isEmpty()) return {};
    SettingsSchemaV2 copy;
    for (const auto &section : schema) {
        SettingsSectionV2 s{copyString(section.id), copyString(section.titleKey), {}, {}};
        for (const auto &f : section.fields) {
            SettingsFieldV2 field{copyString(f.id), copyString(f.labelKey), f.type, f.required, f.secret};
            if (!f.secret && f.type != SettingsFieldTypeV2::Secret) field.defaultValue = copyPrimitive(f.defaultValue);
            for (const auto &choice : f.choices) field.choices.append(copyPrimitive(choice));
            field.constraints = copyMap(f.constraints);
            if (f.visibleWhen) field.visibleWhen = SettingsVisibilityConditionV2{
                copyString(f.visibleWhen->fieldId), f.visibleWhen->comparison, copyPrimitive(f.visibleWhen->value)};
            s.fields.append(field);
        }
        for (const auto &a : section.actions) s.actions.append({copyString(a.id), copyString(a.labelKey), a.requiresConfirmation});
        copy.append(s);
    }
    return copy;
}

SourceDescriptorV2 detachSettingsDescriptorV2(const SourceDescriptorV2 &descriptor)
{
    SourceDescriptorV2 result{copyString(descriptor.pluginPackageId), copyString(descriptor.sourceId),
        copyString(descriptor.name), copyString(descriptor.version), descriptor.sdkAbi, {}};
    for (auto it = descriptor.declaredActions.cbegin(); it != descriptor.declaredActions.cend(); ++it)
        if (int(it.key()) >= int(SourceActionV2::Play) && int(it.key()) <= int(SourceActionV2::DeleteBookmark))
            result.declaredActions.insert(it.key(), copyAvailability(it.value()));
    return result;
}

QVariantList settingsSectionsPresentationV2(const SettingsSchemaV2 &schema,
    const QVariantMap &draft, const QVariantMap &previous, const QStringList &configured)
{
    const auto values = sourceSettingsPublicValuesV2(schema, draft, previous);
    QVariantList sections;
    for (const auto &s : schema) {
        QVariantList fields;
        for (const auto &f : s.fields) {
            const bool secret = f.secret || f.type == SettingsFieldTypeV2::Secret;
            QVariantMap row{{"id", f.id}, {"labelKey", f.labelKey}, {"type", int(f.type)},
                {"required", f.required}, {"secret", secret}, {"visible", sourceSettingsFieldVisibleV2(f, values)},
                {"choices", f.choices}, {"constraints", f.constraints}, {"credentialConfigured", secret && configured.contains(f.id)}};
            if (!secret && values.contains(f.id)) row.insert("value", values.value(f.id));
            fields.append(row);
        }
        sections.append(QVariantMap{{"id", s.id}, {"titleKey", s.titleKey}, {"fields", fields}});
    }
    return sections;
}

QVariantList sourceCapabilitiesPresentationV2(const QHash<SourceActionV2, ActionAvailabilityV2> &declared,
    const std::optional<CapabilitySetV2> &runtime)
{
    QVariantList result;
    const ActionAvailabilityV2 unknown{AvailabilityV2::Unavailable, {}, {}};
    for (int key = int(SourceActionV2::Play); key <= int(SourceActionV2::DeleteBookmark); ++key) {
        const auto action = SourceActionV2(key);
        const auto plugin = copyAvailability(declared.value(action));
        const auto server = runtime ? copyAvailability(runtime->serverActions.value(action, unknown)) : unknown;
        const auto account = runtime ? copyAvailability(runtime->accountActions.value(action, unknown)) : unknown;
        const auto state = intersectActionAvailabilityV2({plugin, server, account}).state;
        result.append(QVariantMap{{"action", key}, {"pluginState", int(plugin.state)},
            {"serverState", int(server.state)}, {"accountState", int(account.state)}, {"state", int(state)},
            {"reasonKey", settingsAvailabilityReasonV2(state)}});
    }
    return result;
}

QString settingsAvailabilityReasonV2(AvailabilityV2 state)
{
    switch (state) {
    case AvailabilityV2::Available: return {};
    case AvailabilityV2::Unsupported: return QStringLiteral("source.settings.unsupported");
    case AvailabilityV2::Forbidden: return QStringLiteral("source.settings.forbidden");
    default: return QStringLiteral("source.settings.unavailable");
    }
}

QVariantList settingsActionsPresentationV2(const SettingsSchemaV2 &schema,
    const std::optional<SettingsActionCapabilitiesV2> &capabilities, bool providerSupported)
{
    QVariantList rows;
    const ActionAvailabilityV2 unknown{AvailabilityV2::Unavailable, {}, {}};
    for (const auto &section : schema) for (const auto &action : section.actions) {
        AvailabilityV2 state = providerSupported ? AvailabilityV2::Unavailable : AvailabilityV2::Unsupported;
        if (providerSupported && capabilities) {
            const auto server = capabilities->serverActions.value(action.id, unknown);
            const auto account = capabilities->accountActions.value(action.id, unknown);
            state = server.constraints.isEmpty() && account.constraints.isEmpty()
                ? intersectActionAvailabilityV2({{AvailabilityV2::Available, {}, {}}, server, account}).state
                : AvailabilityV2::Unsupported;
        }
        rows.append(QVariantMap{{"id", action.id}, {"labelKey", action.labelKey},
            {"requiresConfirmation", action.requiresConfirmation}, {"state", int(state)},
            {"reasonKey", settingsAvailabilityReasonV2(state)}});
    }
    return rows;
}
