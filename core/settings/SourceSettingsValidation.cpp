#include "SourceSettingsValidation.h"
#include <QRegularExpression>
#include <QSet>
#include <cmath>

namespace {
bool integer(const QVariant &v)
{
    switch (v.metaType().id()) {
    case QMetaType::Int: case QMetaType::UInt: case QMetaType::LongLong:
    case QMetaType::ULongLong: case QMetaType::Long: case QMetaType::ULong:
    case QMetaType::Short: case QMetaType::UShort: case QMetaType::Char:
    case QMetaType::SChar: case QMetaType::UChar:
    case QMetaType::Double: case QMetaType::Float: {
        const double n = v.toDouble();
        return std::isfinite(n) && std::trunc(n) == n && std::abs(n) <= 9007199254740991.0;
    }
    default: return false;
    }
}
int category(const QVariant &v)
{
    if (v.metaType().id() == QMetaType::QString) return 1;
    if (v.metaType().id() == QMetaType::Bool) return 2;
    return integer(v) ? 3 : 0;
}
bool stringField(SettingsFieldTypeV2 type)
{
    return type == SettingsFieldTypeV2::Text || type == SettingsFieldTypeV2::Secret
        || type == SettingsFieldTypeV2::Url || type == SettingsFieldTypeV2::Directory;
}
bool secretField(const SettingsFieldV2 &f) { return f.secret || f.type == SettingsFieldTypeV2::Secret; }
bool validValue(const SettingsFieldV2 &f, const QVariant &value)
{
    if (stringField(f.type)) {
        if (value.metaType().id() != QMetaType::QString) return false;
        const QString text = value.toString();
        if (f.required && text.isEmpty()) return false;
        if (f.type == SettingsFieldTypeV2::Url) {
            const QUrl url(text, QUrl::StrictMode);
            if (!url.isValid() || url.isRelative() || url.host().isEmpty()
                || (url.scheme() != "https" && url.scheme() != "http")
                || url.authority().contains('@')) return false;
        }
        if (f.constraints.contains("minLength") && text.size() < f.constraints.value("minLength").toLongLong()) return false;
        if (f.constraints.contains("maxLength") && text.size() > f.constraints.value("maxLength").toLongLong()) return false;
        if (f.constraints.contains("pattern") && !QRegularExpression(f.constraints.value("pattern").toString()).match(text).hasMatch()) return false;
        return true;
    }
    if (f.type == SettingsFieldTypeV2::Boolean) return value.metaType().id() == QMetaType::Bool;
    if (f.type == SettingsFieldTypeV2::Integer) {
        if (!integer(value)) return false;
        return (!f.constraints.contains("min") || value.toDouble() >= f.constraints.value("min").toDouble())
            && (!f.constraints.contains("max") || value.toDouble() <= f.constraints.value("max").toDouble());
    }
    if (f.type == SettingsFieldTypeV2::Choice && category(value)) {
        for (const auto &choice : f.choices) {
            if (category(value) == category(choice) && value == choice) return true;
        }
    }
    return false;
}
bool validField(const SettingsFieldV2 &f)
{
    switch (f.type) {
    case SettingsFieldTypeV2::Text: case SettingsFieldTypeV2::Secret:
    case SettingsFieldTypeV2::Url: case SettingsFieldTypeV2::Directory:
    case SettingsFieldTypeV2::Integer: case SettingsFieldTypeV2::Boolean:
    case SettingsFieldTypeV2::Choice: break;
    default: return false;
    }
    if (secretField(f) && !stringField(f.type)) return false;
    if (f.type == SettingsFieldTypeV2::Choice) {
        if (f.choices.isEmpty()) return false;
        for (const auto &choice : f.choices) if (!category(choice)) return false;
    } else if (!f.choices.isEmpty()) return false;
    for (auto it = f.constraints.cbegin(); it != f.constraints.cend(); ++it) {
        if (f.type == SettingsFieldTypeV2::Integer && (it.key() == "min" || it.key() == "max")) {
            if (!integer(*it)) return false;
        } else if (stringField(f.type) && (it.key() == "minLength" || it.key() == "maxLength")) {
            if (!integer(*it) || it->toDouble() < 0) return false;
        } else if (stringField(f.type) && it.key() == "pattern") {
            if (it->metaType().id() != QMetaType::QString || !QRegularExpression(it->toString()).isValid()) return false;
        } else return false;
    }
    for (const auto &bounds : {qMakePair(QString("min"), QString("max")),
                               qMakePair(QString("minLength"), QString("maxLength"))}) {
        if (f.constraints.contains(bounds.first) && f.constraints.contains(bounds.second)
            && f.constraints.value(bounds.first).toDouble() > f.constraints.value(bounds.second).toDouble()) return false;
    }
    return !f.defaultValue.isValid() || validValue(f, f.defaultValue);
}

SourceSettingsValidationV2 validate(const SettingsSchemaV2 &schema, const QVariantMap &draft,
    const QVariantMap &previous, const QStringList &configured, bool editing, bool draftOnly)
{
    SourceSettingsValidationV2 result;
    auto fail = [](const char *key) {
        SourceSettingsValidationV2 failure;
        failure.errorKey = QString::fromLatin1(key);
        return failure; // Do not return partial values on failure.
    };
    QSet<QString> sections, ids;
    for (const auto &section : schema) {
        if (!isSafeSourceSettingsIdV2(section.id) || sections.contains(section.id))
            return fail("source.settings.invalidSchema");
        sections.insert(section.id);
        for (const auto &f : section.fields) {
            if (!isSafeSourceSettingsIdV2(f.id) || ids.contains(f.id) || !validField(f))
                return fail("source.settings.invalidSchema");
            ids.insert(f.id);
        }
    }
    for (auto it = draft.cbegin(); it != draft.cend(); ++it)
        if (!ids.contains(it.key())) return fail("source.settings.unknownField");
    for (const auto &section : schema) {
        for (const auto &f : section.fields) {
            if (secretField(f)) {
                result.secretFieldIds.append(f.id);
                if (f.required) result.requiredSecretFieldIds.append(f.id);
                if (draft.contains(f.id)) {
                    const auto value = draft.value(f.id);
                    if (value.metaType().id() != QMetaType::QString) return fail("source.settings.invalidValue");
                    if (!value.toString().isEmpty()) {
                        if (!validValue(f, value)) return fail("source.settings.invalidValue");
                        result.secretUpdates.insert(f.id, value.toString().toUtf8());
                    }
                }
                if (!draftOnly && f.required && !result.secretUpdates.contains(f.id) && !configured.contains(f.id))
                    return fail("source.settings.requiredField");
                continue;
            }
            QVariant value;
            if (draft.contains(f.id)) value = draft.value(f.id);
            else if (!draftOnly && editing) value = previous.value(f.id);
            else if (!draftOnly) value = f.defaultValue;
            if (!value.isValid()) {
                if (draft.contains(f.id)) return fail("source.settings.invalidValue");
                if (!draftOnly && f.required) return fail("source.settings.requiredField");
                continue;
            }
            if (!validValue(f, value)) return fail("source.settings.invalidValue");
            result.parameters.insert(f.id, category(value) == 3 ? QVariant(value.toLongLong()) : value);
        }
    }
    result.secretFieldIds.sort();
    result.requiredSecretFieldIds.sort();
    if (!encodeSourceSecretsV2(result.secretUpdates))
        return fail("source.settings.invalidSecretEnvelope");
    return result;
}
}

QString validateSourceSettingsDraftV2(const SettingsSchemaV2 &schema, const QVariantMap &draft)
{
    return validate(schema, draft, {}, {}, false, true).errorKey;
}

SourceSettingsValidationV2 validateSourceSettingsV2(const SettingsSchemaV2 &schema, const QVariantMap &draft,
    const QVariantMap &previousParameters, const QStringList &configuredSecretFieldIds, bool editing)
{
    return validate(schema, draft, previousParameters, configuredSecretFieldIds, editing, false);
}
