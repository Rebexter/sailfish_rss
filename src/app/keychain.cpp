#include "keychain.h"

#include <Sailfish/Secrets/createcollectionrequest.h>
#include <Sailfish/Secrets/deletesecretrequest.h>
#include <Sailfish/Secrets/secret.h>
#include <Sailfish/Secrets/storedsecretrequest.h>
#include <Sailfish/Secrets/storesecretrequest.h>

using namespace Sailfish::Secrets;

namespace {

const QString CollectionName = QStringLiteral("sailrss");

Secret::Identifier identifier(const QString &name)
{
    return Secret::Identifier(name, CollectionName, SecretManager::DefaultEncryptedStoragePluginName);
}

// Runs the request to completion; returns false and sets *error on failure.
bool run(Request &request, SecretManager *manager, QString *error)
{
    request.setManager(manager);
    request.startRequest();
    request.waitForFinished();
    if (request.result().code() == Result::Failed) {
        *error = request.result().errorMessage();
        return false;
    }
    return true;
}

}  // namespace

bool Keychain::store(const QString &name, const QByteArray &data)
{
    if (!ensureCollection())
        return false;

    // Storing over an existing secret fails, so replace it.
    remove(name);

    Secret secret(identifier(name));
    secret.setType(Secret::TypeBlob);
    secret.setData(data);

    StoreSecretRequest request;
    request.setSecretStorageType(StoreSecretRequest::CollectionSecret);
    request.setUserInteractionMode(SecretManager::PreventInteraction);
    request.setSecret(secret);
    return run(request, &m_manager, &m_error);
}

std::optional<QByteArray> Keychain::load(const QString &name)
{
    StoredSecretRequest request;
    request.setIdentifier(identifier(name));
    request.setUserInteractionMode(SecretManager::PreventInteraction);
    if (!run(request, &m_manager, &m_error))
        return std::nullopt;
    return request.secret().data();
}

bool Keychain::remove(const QString &name)
{
    DeleteSecretRequest request;
    request.setIdentifier(identifier(name));
    request.setUserInteractionMode(SecretManager::PreventInteraction);
    return run(request, &m_manager, &m_error);
}

QString Keychain::lastError() const
{
    return m_error;
}

bool Keychain::ensureCollection()
{
    CreateCollectionRequest request;
    request.setCollectionName(CollectionName);
    request.setAccessControlMode(SecretManager::OwnerOnlyMode);
    request.setCollectionLockType(CreateCollectionRequest::DeviceLock);
    request.setDeviceLockUnlockSemantic(SecretManager::DeviceLockKeepUnlocked);
    request.setStoragePluginName(SecretManager::DefaultEncryptedStoragePluginName);
    request.setEncryptionPluginName(SecretManager::DefaultEncryptedStoragePluginName);
    request.setUserInteractionMode(SecretManager::PreventInteraction);
    request.setManager(&m_manager);
    request.startRequest();
    request.waitForFinished();
    if (request.result().code() == Result::Failed
        && request.result().errorCode() != Result::CollectionAlreadyExistsError) {
        m_error = request.result().errorMessage();
        return false;
    }
    return true;
}
