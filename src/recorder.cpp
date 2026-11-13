#include "recorder.h"
#include "appsettings.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>

namespace {

QString picturesRootPath()
{
    QString pictures = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    if (pictures.isEmpty()) {
        pictures = QDir::homePath() + QStringLiteral("/Pictures");
    }
    return pictures + QStringLiteral("/Periscope");
}

} // namespace

Recorder::Recorder(QObject *parent)
    : QObject(parent)
    , m_settings(new QSettings(appSettingsPath(), QSettings::NativeFormat, this))
{
    m_intervalSeconds
        = qBound(1, m_settings->value(QStringLiteral("recording/intervalSeconds"), 5).toInt(), 3600);
}

QString Recorder::startSession()
{
    if (m_sessionActive) {
        finishSession();
    }

    QDir root(picturesRootPath());
    if (!root.exists() && !root.mkpath(QStringLiteral("."))) {
        setError(tr("Cannot create %1").arg(root.absolutePath()));
        return QString();
    }

    const QString base = QDateTime::currentDateTime().toString(
        QStringLiteral("yyyy-MM-dd_HH-mm-ss"));
    QString path = root.filePath(base);
    int suffix = 2;
    while (QFileInfo::exists(path)) {
        path = root.filePath(QStringLiteral("%1_%2").arg(base).arg(suffix++));
    }

    if (!QDir().mkpath(path)) {
        setError(tr("Cannot create %1").arg(path));
        return QString();
    }

    m_sessionPath = path;
    m_sessionActive = true;
    m_photoCount = 0;
    m_photoIndex = 0;
    clearError();

    emit sessionChanged();
    emit photoCountChanged();

    return path;
}

QString Recorder::nextPhotoPath()
{
    if (!m_sessionActive || m_sessionPath.isEmpty()) {
        setError(tr("No recording in progress"));
        return QString();
    }

    const QString prefix = QFileInfo(m_sessionPath).fileName();
    QString path;
    do {
        path = QDir(m_sessionPath)
                   .filePath(QStringLiteral("IMG_%1_%2.jpg")
                                 .arg(prefix)
                                 .arg(m_photoIndex++, 4, 10, QLatin1Char('0')));
    } while (QFileInfo::exists(path));

    return path;
}

void Recorder::photoSaved(const QString &path)
{
    if (!m_sessionActive) {
        return;
    }

    const QFileInfo info(path);
    if (!info.exists() || info.size() <= 0) {
        setError(tr("The picture was not saved"));
        return;
    }

    if (info.absolutePath() != QDir(m_sessionPath).absolutePath()) {
        return;
    }

    ++m_photoCount;
    clearError();
    emit photoCountChanged();
}

QVariantMap Recorder::finishSession()
{
    QVariantMap result;
    result.insert(QStringLiteral("path"), m_sessionPath);
    result.insert(QStringLiteral("count"), m_photoCount);

    if (m_sessionActive) {
        if (m_photoCount == 0 && !m_sessionPath.isEmpty()) {
            QDir dir(m_sessionPath);
            if (dir.exists() && dir.entryList(QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty()) {
                dir.removeRecursively();
            }
        }
        m_sessionActive = false;
        emit sessionChanged();
    }

    return result;
}

QStringList Recorder::pictureFiles(const QString &folder) const
{
    QStringList result;
    if (folder.isEmpty()) {
        return result;
    }

    const QDir dir(folder);
    if (!dir.exists()) {
        return result;
    }

    const QFileInfoList entries = dir.entryInfoList(QDir::Files | QDir::Readable, QDir::Name);
    for (const QFileInfo &entry : entries) {
        const QString suffix = entry.suffix().toLower();
        if (suffix == QStringLiteral("jpg") || suffix == QStringLiteral("jpeg")) {
            result.append(entry.absoluteFilePath());
        }
    }

    return result;
}

void Recorder::setIntervalSeconds(int seconds)
{
    seconds = qBound(1, seconds, 3600);
    if (m_intervalSeconds == seconds) {
        return;
    }

    m_intervalSeconds = seconds;
    m_settings->setValue(QStringLiteral("recording/intervalSeconds"), seconds);
    emit intervalSecondsChanged();
}

void Recorder::setError(const QString &message)
{
    if (m_lastError == message) {
        return;
    }

    m_lastError = message;
    emit lastErrorChanged();
}

void Recorder::clearError()
{
    setError(QString());
}
