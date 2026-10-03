#pragma once

#include "v2/SourceV2Types.h"
#include <QPointer>
#include <QStringList>
#include <QUrl>

class SourceRegistry;

// Used only while importing legacy file-path records. It does not persist a
// path, modify SourceInstances, enqueue media or resolve playback resources.
class LegacyMediaIdentityResolver final {
public:
    enum class Status { Matched, NoMatch, Ambiguous, Unavailable, InvalidRequest };
    struct Result {
        Status status = Status::InvalidRequest;
        MediaRefV2 ref;
    };

    explicit LegacyMediaIdentityResolver(SourceRegistry *registry);
    Result resolve(const QUrl &fileUrl, const QStringList &candidateInstanceIds) const;

private:
    QPointer<SourceRegistry> m_registry;
};
