#include "PluginThemeAdapter.h"

#include "PluginTheme.h"

#include <QThread>
#include <cmath>

PluginThemeAdapter::PluginThemeAdapter(QObject *parent) : QObject(parent) {}

bool PluginThemeAdapter::apply(const QVariantMap &values)
{
    PluginThemeTokens tokens;
    auto color = [&values](const char *key, QColor *target) {
        if (!values.contains(QLatin1String(key))) return false;
        const QVariant value = values.value(QLatin1String(key));
        *target = value.value<QColor>();
        if (!target->isValid()) *target = QColor(value.toString());
        return target->isValid();
    };
    auto number = [&values](const char *key, qreal *target) {
        bool ok = false;
        *target = values.value(QLatin1String(key)).toDouble(&ok);
        return values.contains(QLatin1String(key)) && ok && std::isfinite(*target)
            && *target > 0;
    };
    auto boolean = [&values](const char *key, bool *target) {
        const QVariant value = values.value(QLatin1String(key));
        if (!value.isValid() || value.metaType().id() != QMetaType::Bool) return false;
        *target = value.toBool();
        return true;
    };

    qreal captionSize = 0, bodySize = 0, titleSize = 0;
    if (!color("background", &tokens.background)
        || !color("surface", &tokens.surface)
        || !color("surfaceHover", &tokens.surfaceHover)
        || !color("primary", &tokens.primary)
        || !color("textPrimary", &tokens.textPrimary)
        || !color("textSecondary", &tokens.textSecondary)
        || !color("border", &tokens.border)
        || !color("success", &tokens.success)
        || !color("warning", &tokens.warning)
        || !color("danger", &tokens.danger)
        || !number("spacingSmall", &tokens.spacingSmall)
        || !number("spacingMedium", &tokens.spacingMedium)
        || !number("spacingLarge", &tokens.spacingLarge)
        || !number("radiusSmall", &tokens.radiusSmall)
        || !number("radiusMedium", &tokens.radiusMedium)
        || !number("radiusLarge", &tokens.radiusLarge)
        || !number("fontCaptionPixelSize", &captionSize)
        || !number("fontBodyPixelSize", &bodySize)
        || !number("fontTitlePixelSize", &titleSize)
        || !number("scaleFactor", &tokens.scaleFactor)
        || !boolean("dark", &tokens.dark)
        || !boolean("reducedMotion", &tokens.reducedMotion)
        || !values.contains(QStringLiteral("fontFamily"))
        || values.value(QStringLiteral("fontFamily")).metaType().id() != QMetaType::QString
        || QThread::currentThread() != PluginTheme::instance()->thread()) {
        return false;
    }
    const QString family = values.value(QStringLiteral("fontFamily")).toString();
    tokens.fontCaption.setFamily(family);
    tokens.fontBody.setFamily(family);
    tokens.fontTitle.setFamily(family);
    tokens.fontCaption.setPixelSize(qRound(captionSize));
    tokens.fontBody.setPixelSize(qRound(bodySize));
    tokens.fontTitle.setPixelSize(qRound(titleSize));
    PluginTheme::instance()->apply(tokens);
    return true;
}
