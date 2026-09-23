#include "PluginTheme.h"

#include <QQmlEngine>
#include <QJSEngine>

PluginTheme::PluginTheme(QObject *parent) : QObject(parent)
{
    m_tokens.fontCaption.setPixelSize(11);
    m_tokens.fontBody.setPixelSize(13);
    m_tokens.fontTitle.setPixelSize(17);
}

PluginTheme *PluginTheme::instance()
{
    static PluginTheme theme;
    return &theme;
}

PluginTheme *PluginTheme::create(QQmlEngine *, QJSEngine *)
{
    auto *theme = instance();
    QQmlEngine::setObjectOwnership(theme, QQmlEngine::CppOwnership);
    return theme;
}

void PluginTheme::apply(const PluginThemeTokens &tokens)
{
    m_tokens = tokens;
    emit themeChanged();
}
