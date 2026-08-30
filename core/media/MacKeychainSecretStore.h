#pragma once

#include "SourceAccountStore.h"

class MacKeychainSecretStore final : public ISecretStore {
public:
    bool write(const QString &reference, const QByteArray &secret,
               QString *error = nullptr) override;
    std::optional<QByteArray> read(const QString &reference,
                                   QString *error = nullptr) const override;
    bool remove(const QString &reference, QString *error = nullptr) override;
};
