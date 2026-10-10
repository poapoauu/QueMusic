#pragma once

#include <QString>
#include <QUuid>

// Transitional Host implementation, not a Source SDK or plugin interface.
// Correlate by request, not hash: replaying the same song has a new intent.
class LegacyPlaybackRequestGuard final {
public:
    QString begin(const QString &hash, int source) {
        invalidate();
        if (hash.isEmpty() || (source != 0 && source != 1)) return {};
        m_token = QUuid::createUuid().toString(QUuid::WithoutBraces);
        m_hash = hash;
        m_source = source;
        m_pending = true;
        return m_token;
    }
    void invalidate() { m_token.clear(); m_hash.clear(); m_source = -1; m_pending = false; }
    QString token() const { return m_token; }
    bool isCurrent(const QString &token) const { return !token.isEmpty() && token == m_token; }
    bool take(const QString &token, const QString &hash, int source) {
        if (!m_pending || !isCurrent(token) || hash != m_hash || source != m_source) return false;
        m_pending = false; // Consume before emitting a reentrant playback signal.
        return true;
    }
private:
    QString m_token;
    QString m_hash;
    int m_source = -1;
    bool m_pending = false;
};
