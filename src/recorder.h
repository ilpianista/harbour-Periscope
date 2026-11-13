#ifndef RECORDER_H
#define RECORDER_H

#include <QObject>
#include <QString>
#include <QVariantMap>

class QSettings;

class Recorder : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString sessionPath READ sessionPath NOTIFY sessionChanged)
    Q_PROPERTY(bool sessionActive READ sessionActive NOTIFY sessionChanged)
    Q_PROPERTY(int photoCount READ photoCount NOTIFY photoCountChanged)
    Q_PROPERTY(int intervalSeconds READ intervalSeconds WRITE setIntervalSeconds NOTIFY
                   intervalSecondsChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)

public:
    explicit Recorder(QObject *parent = nullptr);

    QString sessionPath() const { return m_sessionPath; }
    bool sessionActive() const { return m_sessionActive; }
    int photoCount() const { return m_photoCount; }
    int intervalSeconds() const { return m_intervalSeconds; }
    QString lastError() const { return m_lastError; }

    Q_INVOKABLE QString startSession();
    Q_INVOKABLE QString nextPhotoPath();
    Q_INVOKABLE void photoSaved(const QString &path);
    Q_INVOKABLE QVariantMap finishSession();
    Q_INVOKABLE QStringList pictureFiles(const QString &folder) const;

    void setIntervalSeconds(int seconds);

signals:
    void sessionChanged();
    void photoCountChanged();
    void intervalSecondsChanged();
    void lastErrorChanged();

private:
    void setError(const QString &message);
    void clearError();

    QString m_sessionPath;
    bool m_sessionActive = false;
    int m_photoCount = 0;
    int m_photoIndex = 0;
    int m_intervalSeconds = 5;
    QString m_lastError;
    QSettings *m_settings = nullptr;
};

#endif // RECORDER_H
