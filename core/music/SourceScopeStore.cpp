#include "SourceScopeStore.h"

#include <QSettings>

SourceScopeStore::SourceScopeStore(QSettings *settings, QObject *parent)
    : QObject(parent), m_settings(settings)
{
    Q_ASSERT(m_settings);
    m_selected = m_settings->value("MusicHub/selectedSourceInstanceId").toString();
}

QString SourceScopeStore::selectedSourceInstanceId() const
{
    return m_selected;
}

void SourceScopeStore::setSelectedSourceInstanceId(const QString &value)
{
    if (m_selected == value) return;
    m_selected = value;
    m_settings->setValue("MusicHub/selectedSourceInstanceId", value);
    emit selectedSourceInstanceIdChanged();
}
