#ifndef SECURESTORAGE_H
#define SECURESTORAGE_H

#include <Sailfish/Secrets/result.h>
#include <Sailfish/Secrets/secretmanager.h>

#include <QObject>
#include <QQueue>
#include <QString>

class SecureStorage : public QObject
{
    Q_OBJECT

public:
    explicit SecureStorage(QObject *parent = nullptr);

    void initialize();
    void saveToken(const QString &token);
    void clearAll();

signals:
    void tokenLoaded(const QString &token);
    void error(const QString &message);

private:
    struct Operation
    {
        enum Kind { Load, Save, Delete };

        Kind kind;
        QString value;
    };

    void ensureCollection();
    void enqueue(Operation::Kind kind, const QString &value = QString());
    void processQueue();
    void runLoad();
    void runSave(const QString &value);
    void runDelete();
    void storeSecret(const QString &value);
    void finishOperation();

    Sailfish::Secrets::SecretManager m_secretManager;
    QString m_collectionName;

    bool m_initializing;
    bool m_initialized;
    bool m_operationInFlight;
    bool m_secretExists;

    // The desired token state is tracked so that a load which completes after
    // the user already saved or cleared the token does not resurrect old data.
    bool m_hasDesiredToken;
    QString m_desiredToken;

    QQueue<Operation> m_queue;
};

#endif // SECURESTORAGE_H
