#include "SourceV2Types.h"
#include <cmath>

namespace {
int availabilityPriority(AvailabilityV2 state)
{
    switch (state) {
    case AvailabilityV2::Unsupported: return 3;
    case AvailabilityV2::Forbidden: return 2;
    case AvailabilityV2::Unavailable: return 1;
    case AvailabilityV2::Available: return 0;
    }
    return 1;
}
bool number(const QVariant &value)
{
    switch (value.metaType().id()) {
    case QMetaType::Int: case QMetaType::UInt: case QMetaType::LongLong:
    case QMetaType::ULongLong: case QMetaType::Float: case QMetaType::Double:
        return std::isfinite(value.toDouble()) && value.toDouble() >= 0;
    default: return false;
    }
}
}

ActionAvailabilityV2 intersectActionAvailabilityV2(const QList<ActionAvailabilityV2> &layers)
{
    if (layers.isEmpty()) return {};
    ActionAvailabilityV2 result{AvailabilityV2::Available, {}, {}};
    bool invalidConstraints = false;
    for (auto layer : layers) {
        if (int(layer.state) < int(AvailabilityV2::Unsupported)
            || int(layer.state) > int(AvailabilityV2::Forbidden)) {
            layer.state = AvailabilityV2::Unavailable;
            layer.reasonKey = QStringLiteral("source.capability.invalid");
        }
        const int priority = availabilityPriority(layer.state);
        if (priority > availabilityPriority(result.state)) {
            result.state = layer.state;
            result.reasonKey = layer.reasonKey;
        } else if (priority == availabilityPriority(result.state) && !layer.reasonKey.isEmpty()) {
            result.reasonKey = layer.reasonKey;
        }
        for (auto it = layer.constraints.cbegin(); it != layer.constraints.cend(); ++it) {
            if (it.key() == QLatin1String("sameSourceOnly") && it->metaType().id() == QMetaType::Bool) {
                result.constraints[it.key()] = result.constraints.value(it.key()).toBool() || it->toBool();
            } else if (it.key() == QLatin1String("maxBitrate") && number(*it)) {
                const double maximum = it->toDouble();
                result.constraints[it.key()] = result.constraints.contains(it.key())
                    ? qMin(result.constraints.value(it.key()).toDouble(), maximum) : maximum;
            } else {
                invalidConstraints = true;
            }
        }
    }
    if (invalidConstraints && availabilityPriority(result.state) <= 1) {
        result.state = AvailabilityV2::Unavailable;
        result.reasonKey = QStringLiteral("music.actionConstraintsUnsupported");
    }
    return result;
}

ActionAvailabilityV2 CapabilitySetV2::serverAction(SourceActionV2 key) const
{
    return serverActions.value(key, {AvailabilityV2::Unavailable, QStringLiteral("source.capability.unknown"), {}});
}
ActionAvailabilityV2 CapabilitySetV2::accountAction(SourceActionV2 key) const
{
    return accountActions.value(key, {AvailabilityV2::Unavailable, QStringLiteral("source.permission.unknown"), {}});
}
ActionAvailabilityV2 CapabilitySetV2::action(SourceActionV2 key) const
{
    return intersectActionAvailabilityV2({serverAction(key), accountAction(key)});
}

QVariantMap mediaRefV2ToVariantMap(const MediaRefV2 &media)
{
    return {
        {QStringLiteral("sourcePluginId"), media.sourcePluginId},
        {QStringLiteral("sourceInstanceId"), media.sourceInstanceId},
        {QStringLiteral("accountId"), media.accountId},
        {QStringLiteral("entityType"), static_cast<int>(media.entityType)},
        {QStringLiteral("entityId"), media.entityId},
    };
}

MediaRefV2 mediaRefV2FromVariantMap(const QVariantMap &map)
{
    return {
        map.value(QStringLiteral("sourcePluginId")).toString(),
        map.value(QStringLiteral("sourceInstanceId")).toString(),
        map.value(QStringLiteral("accountId")).toString(),
        static_cast<MediaEntityTypeV2>(map.value(QStringLiteral("entityType")).toInt()),
        map.value(QStringLiteral("entityId")).toString(),
    };
}

QJsonObject actionAvailabilityV2ToJson(const ActionAvailabilityV2 &availability)
{
    return {
        {QStringLiteral("state"), static_cast<int>(availability.state)},
        {QStringLiteral("reasonKey"), availability.reasonKey},
        {QStringLiteral("constraints"), QJsonObject::fromVariantMap(availability.constraints)},
    };
}

ActionAvailabilityV2 actionAvailabilityV2FromJson(const QJsonObject &object)
{
    return {
        static_cast<AvailabilityV2>(object.value(QStringLiteral("state")).toInt()),
        object.value(QStringLiteral("reasonKey")).toString(),
        object.value(QStringLiteral("constraints")).toObject().toVariantMap(),
    };
}
