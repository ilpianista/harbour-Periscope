#ifndef PANORAMAXCLIENT_H
#define PANORAMAXCLIENT_H

#include <QNetworkAccessManager>
#include <QObject>
#include <QSettings>
#include <QStringList>
#include <QVariantMap>

class QNetworkReply;
class QNetworkRequest;
class QTimer;
class SecureStorage;

class PanoramaxClient : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString instanceUrl READ instanceUrl NOTIFY instanceUrlChanged)
    Q_PROPERTY(QString instanceLabel READ instanceLabel NOTIFY instanceInfoChanged)
    Q_PROPERTY(bool configured READ configured NOTIFY instanceUrlChanged)
    Q_PROPERTY(bool authEnabled READ authEnabled NOTIFY instanceInfoChanged)
    Q_PROPERTY(QString userName READ userName NOTIFY authChanged)
    Q_PROPERTY(bool signedIn READ signedIn NOTIFY authChanged)
    Q_PROPERTY(bool authResolved READ authResolved NOTIFY authChanged)
    Q_PROPERTY(bool tosAccepted READ tosAccepted NOTIFY authChanged)
    Q_PROPERTY(bool tosRequired READ tosRequired NOTIFY instanceInfoChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)
    Q_PROPERTY(int progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(int filesDone READ filesDone NOTIFY progressChanged)
    Q_PROPERTY(int filesTotal READ filesTotal NOTIFY progressChanged)
    Q_PROPERTY(bool hasFailedFiles READ hasFailedFiles NOTIFY uploadStateChanged)

public:
    explicit PanoramaxClient(QObject *parent = nullptr);

    QString instanceUrl() const { return m_instanceUrl; }
    QString instanceLabel() const { return m_instanceLabel; }
    bool configured() const { return !m_instanceUrl.isEmpty(); }
    bool authEnabled() const { return m_authEnabled; }
    QString userName() const { return m_userName; }
    bool signedIn() const { return m_signedIn; }
    bool authResolved() const { return m_authResolved; }
    bool tosAccepted() const { return m_tosAccepted; }
    bool tosRequired() const { return m_tosRequired; }
    bool busy() const { return m_busy; }
    QString statusText() const { return m_statusText; }
    int progress() const { return m_progress; }
    int filesDone() const { return m_filesDone; }
    int filesTotal() const { return m_filesTotal; }
    bool hasFailedFiles() const { return !m_failedPaths.isEmpty(); }

    Q_INVOKABLE void checkInstance(const QString &url);
    Q_INVOKABLE void checkAuth();
    Q_INVOKABLE void pollAuth();
    Q_INVOKABLE void generateToken();
    Q_INVOKABLE void saveToken(const QString &token);
    Q_INVOKABLE void signOut();
    Q_INVOKABLE void acceptTos();

    Q_INVOKABLE void startUpload(const QStringList &paths,
                                 const QString &title,
                                 const QString &tags,
                                 bool singleSequence);
    Q_INVOKABLE void retryUpload();

signals:
    void instanceUrlChanged();
    void instanceInfoChanged();
    void authChanged();
    void busyChanged();
    void statusTextChanged();
    void progressChanged();
    void uploadStateChanged();

    void instanceChecked(bool ok, const QString &label, const QString &error);
    void authChecked(bool ok, const QString &name, bool tosAccepted, const QString &error);
    void tokenGenerated(const QString &claimUrl, const QString &error);
    void tosAcceptedResult(bool ok, const QString &error);
    void uploadStarted();
    void uploadCompleted(const QVariantMap &summary);
    void uploadFailed(const QString &message);
    void errorReported(const QString &message);

private:
    QUrl apiUrl(const QString &path) const;
    QNetworkRequest apiRequest(const QString &path, bool withAuth = true) const;
    QNetworkRequest apiRequestFor(const QUrl &url, bool withAuth) const;
    void setTimeout(QNetworkReply *reply, int milliseconds);
    QString replyErrorMessage(QNetworkReply *reply, const QByteArray &body) const;
    void setBusy(bool busy);
    void setStatus(const QString &status);
    void updateProgress(int done);
    void clearSession();
    void saveInstanceSettings();
    void setAuthResolved(bool resolved);
    void doAuthCheck(bool silent);
    void cancelAuthCheck();
    void cancelUpload();

    void createUploadSet(const QString &title, const QString &tags, bool singleSequence);
    void uploadNext();
    void completeUploadSet();
    void startPolling();
    void pollUploadSet();
    void fetchUploadSetFiles();
    void failUpload(const QString &message);

    QNetworkAccessManager *m_manager;
    SecureStorage *m_secrets;
    QTimer *m_pollTimer;
    QSettings *m_settings;
    QNetworkReply *m_activeUpload;
    QNetworkReply *m_authReply;
    quint64 m_authRequestId;

    QString m_instanceUrl;
    QString m_instanceLabel;
    bool m_authEnabled;
    QString m_token;
    QString m_userName;
    bool m_signedIn;
    bool m_authResolved;
    bool m_tosAccepted;
    bool m_tosRequired;
    bool m_busy;
    QString m_statusText;
    int m_progress;
    int m_filesDone;
    int m_filesTotal;

    QString m_uploadSetId;
    QStringList m_queuePaths;
    QStringList m_queueNames;
    int m_queueIndex;
    QStringList m_failedPaths;
    QStringList m_failedNames;
    QVariantMap m_lastProcessing;
    int m_pollElapsed;
    int m_uploadAttempt;
    bool m_polling;
};

#endif // PANORAMAXCLIENT_H
