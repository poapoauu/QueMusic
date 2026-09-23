#include "PluginUiContext.h"

PluginUiContext::PluginUiContext(QObject *parent) : QObject(parent) {}

void PluginUiContext::setContext(const PluginUiContextData &data, QObject *backend,
                                 QObject *settings, QObject *capabilities, QObject *host)
{
    m_data = data;
    m_backend = backend;
    m_settings = settings;
    m_capabilities = capabilities;
    m_host = host;
    m_valid = true;
    emit contextChanged();
}

void PluginUiContext::invalidate()
{
    m_backend = nullptr;
    m_settings = nullptr;
    m_capabilities = nullptr;
    m_host = nullptr;
    m_valid = false;
    emit contextChanged();
}
