#pragma once

#include <QColor>
#include <QFont>
#include <QObject>
#include <QtQml/qqmlregistration.h>

struct PluginThemeTokens {
    QColor background = QColor("#ffffff"), surface = QColor("#f6f6f8");
    QColor surfaceHover = QColor("#eeeeef"), primary = QColor("#3481fa");
    QColor textPrimary = QColor("#1d1d1f"), textSecondary = QColor("#4d4f56");
    QColor border = QColor("#d8d8dc"), success = QColor("#168447");
    QColor warning = QColor("#9a6500"), danger = QColor("#b42318");
    qreal spacingSmall = 6, spacingMedium = 12, spacingLarge = 20;
    qreal radiusSmall = 6, radiusMedium = 10, radiusLarge = 16;
    QFont fontCaption, fontBody, fontTitle;
    qreal scaleFactor = 1.0;
    bool dark = false;
    bool reducedMotion = false;
};

class QQmlEngine;
class QJSEngine;

class PluginTheme final : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(PluginTheme)
    QML_SINGLETON
#define THEME_PROPERTY(Type, Name) Q_PROPERTY(Type Name READ Name NOTIFY themeChanged)
    THEME_PROPERTY(QColor, background)
    THEME_PROPERTY(QColor, surface)
    THEME_PROPERTY(QColor, surfaceHover)
    THEME_PROPERTY(QColor, primary)
    THEME_PROPERTY(QColor, textPrimary)
    THEME_PROPERTY(QColor, textSecondary)
    THEME_PROPERTY(QColor, border)
    THEME_PROPERTY(QColor, success)
    THEME_PROPERTY(QColor, warning)
    THEME_PROPERTY(QColor, danger)
    THEME_PROPERTY(qreal, spacingSmall)
    THEME_PROPERTY(qreal, spacingMedium)
    THEME_PROPERTY(qreal, spacingLarge)
    THEME_PROPERTY(qreal, radiusSmall)
    THEME_PROPERTY(qreal, radiusMedium)
    THEME_PROPERTY(qreal, radiusLarge)
    THEME_PROPERTY(QFont, fontCaption)
    THEME_PROPERTY(QFont, fontBody)
    THEME_PROPERTY(QFont, fontTitle)
    THEME_PROPERTY(qreal, scaleFactor)
    THEME_PROPERTY(bool, dark)
    THEME_PROPERTY(bool, reducedMotion)
#undef THEME_PROPERTY
public:
    explicit PluginTheme(QObject *parent = nullptr);
    static PluginTheme *instance();
    static PluginTheme *create(QQmlEngine *, QJSEngine *);

#define THEME_GETTER(Type, Name) Type Name() const { return m_tokens.Name; }
    THEME_GETTER(QColor, background)
    THEME_GETTER(QColor, surface)
    THEME_GETTER(QColor, surfaceHover)
    THEME_GETTER(QColor, primary)
    THEME_GETTER(QColor, textPrimary)
    THEME_GETTER(QColor, textSecondary)
    THEME_GETTER(QColor, border)
    THEME_GETTER(QColor, success)
    THEME_GETTER(QColor, warning)
    THEME_GETTER(QColor, danger)
    THEME_GETTER(qreal, spacingSmall)
    THEME_GETTER(qreal, spacingMedium)
    THEME_GETTER(qreal, spacingLarge)
    THEME_GETTER(qreal, radiusSmall)
    THEME_GETTER(qreal, radiusMedium)
    THEME_GETTER(qreal, radiusLarge)
    THEME_GETTER(QFont, fontCaption)
    THEME_GETTER(QFont, fontBody)
    THEME_GETTER(QFont, fontTitle)
    THEME_GETTER(qreal, scaleFactor)
    THEME_GETTER(bool, dark)
    THEME_GETTER(bool, reducedMotion)
#undef THEME_GETTER

    void apply(const PluginThemeTokens &tokens);

signals:
    void themeChanged();

private:
    PluginThemeTokens m_tokens;
};
