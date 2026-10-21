#ifndef SAILRSS_KEYCHAIN_H
#define SAILRSS_KEYCHAIN_H

#include <QByteArray>
#include <QString>
#include <Sailfish/Secrets/secretmanager.h>
#include <optional>

// Stores small secrets in the system keychain (Sailfish Secrets), in an
// encrypted collection only this app can access. The collection unlocks
// with the device lock and stays unlocked, so no prompts are shown.
//
// Calls block briefly while the secrets daemon answers; use them for
// occasional operations such as login, not per request.
class Keychain
{
public:
    bool store(const QString &name, const QByteArray &data);
    // nullopt if there is no such secret or it cannot be read.
    std::optional<QByteArray> load(const QString &name);
    bool remove(const QString &name);
    QString lastError() const;

private:
    bool ensureCollection();

    Sailfish::Secrets::SecretManager m_manager;
    QString m_error;
};

#endif  // SAILRSS_KEYCHAIN_H
