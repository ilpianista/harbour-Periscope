#include "securestorage.h"

#include <Sailfish/Secrets/createcollectionrequest.h>
#include <Sailfish/Secrets/deletesecretrequest.h>
#include <Sailfish/Secrets/secret.h>
#include <Sailfish/Secrets/storedsecretrequest.h>
#include <Sailfish/Secrets/storesecretrequest.h>

#include <QDebug>

using namespace Sailfish::Secrets;

namespace {

const char kTokenName[] = "token";

Secret::Identifier tokenIdentifier(const QString &collectionName)
{
    return Secret::Identifier(QString::fromLatin1(kTokenName),
                              collectionName,
                              SecretManager::DefaultEncryptedStoragePluginName);
}

} // namespace

SecureStorage::SecureStorage(QObject *parent)
    : QObject(parent)
    , m_collectionName(QStringLiteral("Periscope"))
    , m_initializing(false)
    , m_initialized(false)
    , m_operationInFlight(false)
    , m_secretExists(false)
    , m_hasDesiredToken(false)
{}

void SecureStorage::initialize()
{
    if (m_initialized || m_initializing) {
        return;
    }

    m_initializing = true;
    ensureCollection();
}

void SecureStorage::ensureCollection()
{
    CreateCollectionRequest *request = new CreateCollectionRequest(this);
    request->setManager(&m_secretManager);
    request->setCollectionName(m_collectionName);
    request->setAccessControlMode(SecretManager::OwnerOnlyMode);
    request->setCollectionLockType(CreateCollectionRequest::DeviceLock);
    request->setDeviceLockUnlockSemantic(SecretManager::DeviceLockKeepUnlocked);
    request->setStoragePluginName(SecretManager::DefaultEncryptedStoragePluginName);
    request->setEncryptionPluginName(SecretManager::DefaultEncryptedStoragePluginName);

    connect(request, &CreateCollectionRequest::statusChanged, this, [this, request]() {
        if (request->status() != Request::Finished) {
            return;
        }
        request->deleteLater();

        const Result result = request->result();
        if (result.code() != Result::Succeeded
            && result.errorCode() != Result::CollectionAlreadyExistsError) {
            m_initializing = false;
            qWarning() << "Failed to open secrets collection:" << result.errorMessage();
            emit error(tr("Could not open the secure storage: %1").arg(result.errorMessage()));
            return;
        }

        m_initializing = false;
        m_initialized = true;
        enqueue(Operation::Load);
    });

    request->startRequest();
}

void SecureStorage::saveToken(const QString &token)
{
    m_hasDesiredToken = true;
    m_desiredToken = token;
    enqueue(Operation::Save, token);
}

void SecureStorage::clearAll()
{
    m_hasDesiredToken = true;
    m_desiredToken.clear();
    enqueue(Operation::Delete);
}

void SecureStorage::enqueue(Operation::Kind kind, const QString &value)
{
    Operation operation;
    operation.kind = kind;
    operation.value = value;
    m_queue.enqueue(operation);
    processQueue();
}

void SecureStorage::processQueue()
{
    if (!m_initialized || m_operationInFlight || m_queue.isEmpty()) {
        return;
    }

    m_operationInFlight = true;
    const Operation operation = m_queue.dequeue();

    switch (operation.kind) {
    case Operation::Load:
        runLoad();
        break;
    case Operation::Save:
        runSave(operation.value);
        break;
    case Operation::Delete:
        runDelete();
        break;
    }
}

void SecureStorage::finishOperation()
{
    m_operationInFlight = false;
    processQueue();
}

void SecureStorage::runLoad()
{
    StoredSecretRequest *request = new StoredSecretRequest(this);
    request->setManager(&m_secretManager);
    request->setIdentifier(tokenIdentifier(m_collectionName));
    request->setUserInteractionMode(SecretManager::SystemInteraction);

    connect(request, &StoredSecretRequest::statusChanged, this, [this, request]() {
        if (request->status() != Request::Finished) {
            return;
        }
        request->deleteLater();

        const Result result = request->result();
        QString token;
        if (result.code() == Result::Succeeded) {
            token = QString::fromUtf8(request->secret().data());
            m_secretExists = true;
        } else {
            // A missing token is the normal first-run state; not an error.
            m_secretExists = false;
        }

        // If the token changed while the load was in flight, report the
        // requested state instead of the stale value read from storage.
        if (m_hasDesiredToken) {
            token = m_desiredToken;
            m_hasDesiredToken = false;
        }

        emit tokenLoaded(token);
        finishOperation();
    });

    request->startRequest();
}

void SecureStorage::runSave(const QString &value)
{
    // A secret cannot be overwritten, so remove any previous value first.
    // A missing secret is expected on the first save, so the result is ignored.
    DeleteSecretRequest *request = new DeleteSecretRequest(this);
    request->setManager(&m_secretManager);
    request->setIdentifier(tokenIdentifier(m_collectionName));
    request->setUserInteractionMode(SecretManager::SystemInteraction);

    connect(request, &DeleteSecretRequest::statusChanged, this, [this, request, value]() {
        if (request->status() != Request::Finished) {
            return;
        }
        request->deleteLater();
        m_secretExists = false;
        storeSecret(value);
    });

    request->startRequest();
}

void SecureStorage::storeSecret(const QString &value)
{
    Secret secret(tokenIdentifier(m_collectionName));
    secret.setData(value.toUtf8());
    secret.setType(Secret::TypeBlob);

    StoreSecretRequest *request = new StoreSecretRequest(this);
    request->setManager(&m_secretManager);
    request->setSecretStorageType(StoreSecretRequest::CollectionSecret);
    request->setUserInteractionMode(SecretManager::SystemInteraction);
    request->setSecret(secret);

    connect(request, &StoreSecretRequest::statusChanged, this, [this, request]() {
        if (request->status() != Request::Finished) {
            return;
        }
        request->deleteLater();

        const Result result = request->result();
        if (result.code() == Result::Succeeded) {
            m_secretExists = true;
        } else {
            qWarning() << "Failed to store the token:" << result.errorMessage();
            emit error(tr("Could not save the token: %1").arg(result.errorMessage()));
        }

        finishOperation();
    });

    request->startRequest();
}

void SecureStorage::runDelete()
{
    const bool hadSecret = m_secretExists;

    DeleteSecretRequest *request = new DeleteSecretRequest(this);
    request->setManager(&m_secretManager);
    request->setIdentifier(tokenIdentifier(m_collectionName));
    request->setUserInteractionMode(SecretManager::SystemInteraction);

    connect(request, &DeleteSecretRequest::statusChanged, this, [this, request, hadSecret]() {
        if (request->status() != Request::Finished) {
            return;
        }
        request->deleteLater();

        const Result result = request->result();
        if (result.code() != Result::Succeeded && hadSecret) {
            qWarning() << "Failed to delete the stored token:" << result.errorMessage();
            emit error(tr("Could not remove the stored token: %1").arg(result.errorMessage()));
        }

        m_secretExists = false;
        finishOperation();
    });

    request->startRequest();
}
