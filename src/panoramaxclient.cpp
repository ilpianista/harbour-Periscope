#include "panoramaxclient.h"
#include "appsettings.h"
#include "securestorage.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QHttpMultiPart>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

namespace {

constexpr int kMaxUploadAttempts = 3;

QString normalizeInstanceUrl(const QString &input)
{
    QString url = input.trimmed();
    if (url.isEmpty()) {
        return url;
    }
    if (!url.contains(QStringLiteral("://"))) {
        url.prepend(QStringLiteral("https://"));
    }
    while (url.endsWith(QLatin1Char('/'))) {
        url.chop(1);
    }
    return url;
}

QString safeFileName(const QString &name)
{
    // The name is embedded in a multipart header, so strip control characters
    // (header injection) as well as the characters that break the quoting.
    QString result;
    result.reserve(name.size());
    for (int i = 0; i < name.size(); ++i) {
        const QChar character = name.at(i);
        const ushort code = character.unicode();
        if (code < 0x20 || code == 0x7f || character == QLatin1Char('"')
            || character == QLatin1Char('\\')) {
            result.append(QLatin1Char('_'));
        } else {
            result.append(character);
        }
    }

    if (result.isEmpty()) {
        result = QStringLiteral("picture.jpg");
    }
    return result;
}

QByteArray readReplyBody(QNetworkReply *reply)
{
    // A reply aborted by a timeout has its device closed; reading it would
    // trigger "QIODevice::read: device not open" warnings.
    if (!reply || !reply->isOpen()) {
        return QByteArray();
    }
    return reply->readAll();
}

QJsonArray parseSemanticTags(const QString &text)
{
    QJsonArray tags;
    const QStringList items = text.split(QLatin1Char(','), QString::SkipEmptyParts);
    for (const QString &item : items) {
        const int equals = item.indexOf(QLatin1Char('='));
        if (equals <= 0) {
            continue;
        }

        const QString key = item.left(equals).trimmed();
        const QString value = item.mid(equals + 1).trimmed();
        if (key.isEmpty() || value.isEmpty()) {
            continue;
        }

        QJsonObject tag;
        tag.insert(QStringLiteral("action"), QStringLiteral("add"));
        tag.insert(QStringLiteral("key"), key);
        tag.insert(QStringLiteral("value"), value);
        tags.append(tag);
    }
    return tags;
}

QString uniqueFileName(const QString &name, const QStringList &used)
{
    if (!used.contains(name, Qt::CaseInsensitive)) {
        return name;
    }

    const QFileInfo info(name);
    const QString base = info.completeBaseName();
    const QString suffix = info.suffix();

    int counter = 2;
    QString candidate;
    do {
        if (suffix.isEmpty()) {
            candidate = QStringLiteral("%1_%2").arg(base).arg(counter);
        } else {
            candidate = QStringLiteral("%1_%2.%3").arg(base).arg(counter).arg(suffix);
        }
        ++counter;
    } while (used.contains(candidate, Qt::CaseInsensitive));

    return candidate;
}

} // namespace

PanoramaxClient::PanoramaxClient(QObject *parent)
    : QObject(parent)
    , m_manager(new QNetworkAccessManager(this))
    , m_secrets(new SecureStorage(this))
    , m_pollTimer(new QTimer(this))
    , m_settings(nullptr)
    , m_activeUpload(nullptr)
    , m_authReply(nullptr)
    , m_authRequestId(0)
    , m_authEnabled(true)
    , m_signedIn(false)
    , m_authResolved(false)
    , m_tosAccepted(false)
    , m_tosRequired(true)
    , m_busy(false)
    , m_progress(0)
    , m_filesDone(0)
    , m_filesTotal(0)
    , m_queueIndex(0)
    , m_pollElapsed(0)
    , m_uploadAttempt(0)
    , m_polling(false)
{
    m_settings = new QSettings(appSettingsPath(), QSettings::NativeFormat, this);

    m_instanceUrl = m_settings->value(QStringLiteral("instanceUrl")).toString();
    m_instanceLabel = m_settings->value(QStringLiteral("instanceLabel")).toString();
    m_authEnabled = m_settings->value(QStringLiteral("authEnabled"), true).toBool();
    m_tosRequired = m_settings->value(QStringLiteral("tosRequired"), true).toBool();
    m_userName = m_settings->value(QStringLiteral("userName")).toString();

    m_pollTimer->setInterval(3000);
    connect(m_pollTimer, &QTimer::timeout, this, &PanoramaxClient::pollUploadSet);

    connect(m_secrets, &SecureStorage::tokenLoaded, this, [this](const QString &token) {
        m_token = token;
        if (m_instanceUrl.isEmpty() && !m_token.isEmpty()) {
            // A token cannot be used without its instance. Drop leftovers from an
            // interrupted first sign-in so they do not leak into the next run.
            m_token.clear();
            m_secrets->clearAll();
        }
        if (!m_instanceUrl.isEmpty() && !m_token.isEmpty()) {
            QTimer::singleShot(0, this, [this]() { checkAuth(); });
        } else {
            setAuthResolved(true);
        }
    });
    connect(m_secrets, &SecureStorage::error, this, [this](const QString &message) {
        setAuthResolved(true);
        emit errorReported(message);
    });

    m_secrets->initialize();
}

void PanoramaxClient::checkInstance(const QString &url)
{
    if (m_signedIn) {
        emit instanceChecked(false, QString(), tr("Sign out before changing the instance"));
        return;
    }

    const QString normalized = normalizeInstanceUrl(url);
    if (normalized.isEmpty()) {
        emit instanceChecked(false, QString(), tr("Please enter an instance URL"));
        return;
    }

    const QUrl parsed(normalized);
    if (!parsed.isValid() || parsed.host().isEmpty()
        || (parsed.scheme() != QLatin1String("https") && parsed.scheme() != QLatin1String("http"))) {
        emit instanceChecked(false, QString(), tr("Invalid instance URL"));
        return;
    }
    if (m_busy) {
        emit instanceChecked(false,
                             QString(),
                             tr("Please wait for the current operation to finish"));
        return;
    }

    setBusy(true);
    setStatus(tr("Contacting the instance…"));

    QNetworkRequest request = apiRequestFor(QUrl(normalized + QStringLiteral("/api/configuration")),
                                            false);

    QNetworkReply *reply = m_manager->get(request);
    setTimeout(reply, 30000);

    connect(reply, &QNetworkReply::finished, this, [this, reply, normalized, parsed]() {
        reply->deleteLater();
        const QByteArray body = readReplyBody(reply);

        if (reply->error() != QNetworkReply::NoError) {
            setBusy(false);
            setStatus(QString());
            emit instanceChecked(false, QString(), replyErrorMessage(reply, body));
            return;
        }

        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            setBusy(false);
            setStatus(QString());
            emit instanceChecked(false,
                                 QString(),
                                 tr("This URL does not look like a Panoramax instance"));
            return;
        }

        const QJsonObject object = document.object();
        QString label = object.value(QStringLiteral("name"))
                            .toObject()
                            .value(QStringLiteral("label"))
                            .toString();
        if (label.isEmpty()) {
            label = parsed.host();
        }
        const bool authEnabled = object.value(QStringLiteral("auth"))
                                     .toObject()
                                     .value(QStringLiteral("enabled"))
                                     .toBool(true);
        const bool tosRequired = object.value(QStringLiteral("auth"))
                                     .toObject()
                                     .value(QStringLiteral("enforce_tos_acceptance"))
                                     .toBool(true);

        const QString previous = m_instanceUrl;
        m_instanceUrl = normalized;
        m_instanceLabel = label;
        m_authEnabled = authEnabled;
        m_tosRequired = tosRequired;

        if (!previous.isEmpty() && previous != normalized) {
            clearSession();
        }

        setBusy(false);
        setStatus(QString());
        emit instanceUrlChanged();
        emit instanceInfoChanged();
        emit instanceChecked(true, label, QString());

        // Keep the instance only in memory until the user completes the sign-in.
        if (!m_authEnabled) {
            saveInstanceSettings();
        }

        if (m_authEnabled && !m_token.isEmpty()) {
            QTimer::singleShot(0, this, [this]() { checkAuth(); });
        }
    });
}

void PanoramaxClient::saveInstanceSettings()
{
    m_settings->setValue(QStringLiteral("instanceUrl"), m_instanceUrl);
    m_settings->setValue(QStringLiteral("instanceLabel"), m_instanceLabel);
    m_settings->setValue(QStringLiteral("authEnabled"), m_authEnabled);
    m_settings->setValue(QStringLiteral("tosRequired"), m_tosRequired);
    m_settings->sync();
}

void PanoramaxClient::setAuthResolved(bool resolved)
{
    if (m_authResolved == resolved) {
        return;
    }
    m_authResolved = resolved;
    emit authChanged();
}

void PanoramaxClient::checkAuth()
{
    doAuthCheck(false);
}

void PanoramaxClient::pollAuth()
{
    doAuthCheck(true);
}

void PanoramaxClient::doAuthCheck(bool silent)
{
    if (m_instanceUrl.isEmpty()) {
        if (!silent) {
            emit authChecked(false, QString(), false, tr("No instance configured"));
        }
        setAuthResolved(true);
        return;
    }

    if (m_token.isEmpty()) {
        const bool wasSignedIn = m_signedIn;
        m_signedIn = false;
        m_tosAccepted = false;
        if (wasSignedIn) {
            emit authChanged();
        }
        if (!silent) {
            emit authChecked(false,
                             QString(),
                             false,
                             m_authEnabled ? tr("Not signed in") : QString());
        }
        setAuthResolved(true);
        return;
    }

    // A silent poll must not restart an in-flight request, otherwise a slow
    // connection could keep aborting and restarting the check forever.
    if (m_authReply) {
        if (silent) {
            return;
        }
        cancelAuthCheck();
    }

    const quint64 requestId = ++m_authRequestId;

    QNetworkReply *reply = m_manager->get(apiRequest(QStringLiteral("/users/me")));
    m_authReply = reply;
    setTimeout(reply, 30000);

    connect(reply, &QNetworkReply::finished, this, [this, reply, silent, requestId]() {
        reply->deleteLater();
        if (m_authReply == reply) {
            m_authReply = nullptr;
        }
        if (requestId != m_authRequestId) {
            return;
        }

        const QByteArray body = readReplyBody(reply);
        const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

        if (reply->error() == QNetworkReply::NoError && httpStatus == 200) {
            const QJsonDocument document = QJsonDocument::fromJson(body);
            if (!document.isObject()) {
                if (!silent) {
                    emit authChecked(false,
                                     QString(),
                                     false,
                                     tr("Unexpected response from the instance"));
                }
                setAuthResolved(true);
                return;
            }

            const QJsonObject object = document.object();
            m_userName = object.value(QStringLiteral("name")).toString();
            m_tosAccepted = object.value(QStringLiteral("tos_accepted")).toBool(false);
            m_signedIn = true;

            m_settings->setValue(QStringLiteral("userName"), m_userName);
            saveInstanceSettings();

            setAuthResolved(true);
            emit authChanged();
            emit authChecked(true, m_userName, m_tosAccepted, QString());
            return;
        }

        const bool wasSignedIn = m_signedIn;
        m_signedIn = false;
        m_tosAccepted = false;
        setAuthResolved(true);
        if (wasSignedIn) {
            emit authChanged();
        }

        if (!silent) {
            emit authChecked(false,
                             QString(),
                             false,
                             httpStatus == 401 ? tr("Invalid or expired token")
                                               : replyErrorMessage(reply, body));
        }
    });
}

void PanoramaxClient::cancelAuthCheck()
{
    ++m_authRequestId;
    QNetworkReply *reply = m_authReply;
    m_authReply = nullptr;
    if (reply) {
        reply->abort();
    }
}

void PanoramaxClient::generateToken()
{
    if (m_instanceUrl.isEmpty()) {
        emit tokenGenerated(QString(), tr("Choose an instance first"));
        return;
    }

    const QString description = QString::fromLatin1(
        QUrl::toPercentEncoding(QStringLiteral("Periscope on SailfishOS")));
    QNetworkRequest request
        = apiRequest(QStringLiteral("/auth/tokens/generate?description=%1").arg(description), false);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));

    QNetworkReply *reply = m_manager->post(request, QByteArrayLiteral("{}"));
    setTimeout(reply, 30000);

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        const QByteArray body = readReplyBody(reply);

        if (reply->error() != QNetworkReply::NoError) {
            emit tokenGenerated(QString(), replyErrorMessage(reply, body));
            return;
        }

        const QJsonDocument document = QJsonDocument::fromJson(body);
        if (!document.isObject()) {
            emit tokenGenerated(QString(), tr("Unexpected response from the instance"));
            return;
        }

        const QJsonObject object = document.object();
        const QString jwt = object.value(QStringLiteral("jwt_token")).toString();
        if (jwt.isEmpty()) {
            emit tokenGenerated(QString(), tr("The instance did not return a token"));
            return;
        }

        saveToken(jwt);

        QString claimUrl;
        const QJsonArray links = object.value(QStringLiteral("links")).toArray();
        for (const QJsonValue &value : links) {
            const QJsonObject link = value.toObject();
            if (link.value(QStringLiteral("rel")).toString() == QLatin1String("claim")) {
                claimUrl = link.value(QStringLiteral("href")).toString();
                break;
            }
        }

        emit tokenGenerated(claimUrl, QString());
    });
}

void PanoramaxClient::saveToken(const QString &token)
{
    const QString trimmed = token.trimmed();
    if (trimmed.isEmpty()) {
        return;
    }

    // A check for the previous token must not overwrite the new state.
    cancelAuthCheck();

    m_token = trimmed;
    m_secrets->saveToken(m_token);

    m_signedIn = false;
    m_tosAccepted = false;
    m_settings->remove(QStringLiteral("userName"));

    // Persist the instance together with the token: a token is useless without
    // its instance, and losing the URL would make the next launch discard it.
    if (!m_instanceUrl.isEmpty()) {
        saveInstanceSettings();
    } else {
        m_settings->sync();
    }

    emit authChanged();
}

void PanoramaxClient::signOut()
{
    cancelUpload();
    clearSession();
    setStatus(QString());
}

void PanoramaxClient::acceptTos()
{
    if (m_instanceUrl.isEmpty() || m_token.isEmpty()) {
        emit tosAcceptedResult(false, tr("Sign in first"));
        return;
    }

    QNetworkRequest request = apiRequest(QStringLiteral("/users/me/accept_tos"));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));

    QNetworkReply *reply = m_manager->post(request, QByteArrayLiteral("{}"));
    setTimeout(reply, 30000);

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        const QByteArray body = readReplyBody(reply);

        if (reply->error() != QNetworkReply::NoError) {
            emit tosAcceptedResult(false, replyErrorMessage(reply, body));
            return;
        }

        m_tosAccepted = true;
        emit authChanged();
        emit tosAcceptedResult(true, QString());
        checkAuth();
    });
}

void PanoramaxClient::startUpload(const QStringList &paths,
                                  const QString &title,
                                  const QString &tags,
                                  bool singleSequence)
{
    if (m_busy) {
        emit uploadFailed(tr("Another operation is already running"));
        return;
    }
    if (m_instanceUrl.isEmpty()) {
        emit uploadFailed(tr("Choose an instance first"));
        return;
    }
    if (paths.isEmpty()) {
        emit uploadFailed(tr("Select at least one picture"));
        return;
    }

    m_queuePaths.clear();
    m_queueNames.clear();
    QStringList usedNames;
    for (const QString &path : paths) {
        const QString name = uniqueFileName(QFileInfo(path).fileName(), usedNames);
        usedNames.append(name);
        m_queuePaths.append(path);
        m_queueNames.append(name);
    }

    m_queueIndex = 0;
    m_filesDone = 0;
    m_filesTotal = m_queuePaths.size();
    m_progress = 0;
    m_uploadSetId.clear();
    m_failedPaths.clear();
    m_failedNames.clear();
    m_lastProcessing.clear();
    m_pollElapsed = 0;
    m_uploadAttempt = 0;
    m_polling = false;
    m_pollTimer->stop();

    emit progressChanged();
    emit uploadStateChanged();
    emit uploadStarted();

    setBusy(true);
    setStatus(tr("Creating upload set…"));
    createUploadSet(title.isEmpty() ? QString() : title, tags, singleSequence);
}

void PanoramaxClient::retryUpload()
{
    if (m_busy || m_failedPaths.isEmpty() || m_uploadSetId.isEmpty()) {
        return;
    }

    m_queuePaths = m_failedPaths;
    m_queueNames = m_failedNames;
    m_failedPaths.clear();
    m_failedNames.clear();

    m_queueIndex = 0;
    m_filesDone = 0;
    m_filesTotal = m_queuePaths.size();
    m_progress = 0;
    m_uploadAttempt = 0;

    emit progressChanged();
    emit uploadStateChanged();

    setBusy(true);
    setStatus(m_filesTotal == 1 ? tr("Retrying 1 picture…")
                                : tr("Retrying %1 pictures…").arg(m_filesTotal));
    uploadNext();
}

void PanoramaxClient::createUploadSet(const QString &title, const QString &tags, bool singleSequence)
{
    QJsonObject payload;
    payload.insert(QStringLiteral("title"), title);
    payload.insert(QStringLiteral("estimated_nb_files"), m_filesTotal);

    const QJsonArray semantics = parseSemanticTags(tags);
    if (!semantics.isEmpty()) {
        payload.insert(QStringLiteral("semantics"), semantics);
    }

    if (singleSequence) {
        // Group every picture of the upload in one long sequence, which is
        // what a walk, a ride or a drive usually is.
        payload.insert(QStringLiteral("no_split"), true);
        payload.insert(QStringLiteral("sort_method"), QStringLiteral("time-asc"));
    }

    QNetworkRequest request = apiRequest(QStringLiteral("/upload_sets"));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));

    QNetworkReply *reply = m_manager->post(request,
                                           QJsonDocument(payload).toJson(QJsonDocument::Compact));
    m_activeUpload = reply;
    setTimeout(reply, 60000);

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (m_activeUpload == reply) {
            m_activeUpload = nullptr;
        }
        if (!m_busy) {
            return;
        }

        const QByteArray body = readReplyBody(reply);
        if (reply->error() != QNetworkReply::NoError) {
            failUpload(replyErrorMessage(reply, body));
            return;
        }

        const QJsonDocument document = QJsonDocument::fromJson(body);
        if (!document.isObject()) {
            failUpload(tr("Unexpected response from the instance"));
            return;
        }

        m_uploadSetId = document.object().value(QStringLiteral("id")).toString();
        if (m_uploadSetId.isEmpty()) {
            failUpload(tr("The instance did not create an upload set"));
            return;
        }

        emit uploadStateChanged();
        uploadNext();
    });
}

void PanoramaxClient::uploadNext()
{
    if (!m_busy) {
        return;
    }

    if (m_queueIndex >= m_queuePaths.size()) {
        completeUploadSet();
        return;
    }

    const QString path = m_queuePaths.at(m_queueIndex);
    const QString name = m_queueNames.at(m_queueIndex);
    const int index = m_queueIndex;

    QFile *file = new QFile(path);
    if (!file->open(QIODevice::ReadOnly)) {
        delete file;

        ++m_queueIndex;
        ++m_filesDone;
        m_failedPaths.append(path);
        m_failedNames.append(name);

        updateProgress(m_filesDone);
        emit uploadStateChanged();
        QTimer::singleShot(0, this, [this]() { uploadNext(); });
        return;
    }

    setStatus(tr("Uploading %1 of %2…").arg(index + 1).arg(m_filesTotal));

    QHttpMultiPart *multiPart = new QHttpMultiPart(QHttpMultiPart::FormDataType);

    QHttpPart filePart;
    filePart.setHeader(QNetworkRequest::ContentDispositionHeader,
                       QVariant(QStringLiteral("form-data; name=\"file\"; filename=\"%1\"")
                                    .arg(safeFileName(name))));
    filePart.setHeader(QNetworkRequest::ContentTypeHeader, QVariant(QStringLiteral("image/jpeg")));
    filePart.setBodyDevice(file);
    file->setParent(multiPart);
    multiPart->append(filePart);

    QNetworkRequest request = apiRequest(QStringLiteral("/upload_sets/%1/files").arg(m_uploadSetId));
    QNetworkReply *reply = m_manager->post(request, multiPart);
    multiPart->setParent(reply);
    m_activeUpload = reply;
    setTimeout(reply, 600000);

    connect(reply,
            &QNetworkReply::uploadProgress,
            this,
            [this, index](qint64 bytesSent, qint64 bytesTotal) {
                if (bytesTotal <= 0 || !m_busy) {
                    return;
                }
                const double fraction = double(bytesSent) / double(bytesTotal);
                const int percent = qMin(100,
                                         int(100.0 * (double(index) + fraction)
                                             / double(qMax(1, m_filesTotal))));
                if (percent != m_progress) {
                    m_progress = percent;
                    emit progressChanged();
                }
            });

    connect(reply, &QNetworkReply::finished, this, [this, reply, path, name]() {
        reply->deleteLater();
        if (m_activeUpload == reply) {
            m_activeUpload = nullptr;
        }
        if (!m_busy) {
            return;
        }

        const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const bool alreadyUploaded = httpStatus == 409;
        const bool ok = reply->error() == QNetworkReply::NoError || alreadyUploaded;

        const bool transient = !ok && !alreadyUploaded
                               && (httpStatus == 0 || httpStatus >= 500
                                   || reply->error() == QNetworkReply::TimeoutError
                                   || reply->error() == QNetworkReply::TemporaryNetworkFailureError
                                   || reply->error() == QNetworkReply::NetworkSessionFailedError);
        if (transient && m_uploadAttempt < kMaxUploadAttempts - 1) {
            ++m_uploadAttempt;
            setStatus(tr("Retrying %1…").arg(name));
            QTimer::singleShot(1000 * m_uploadAttempt, this, [this]() { uploadNext(); });
            return;
        }

        m_uploadAttempt = 0;
        if (!ok) {
            m_failedPaths.append(path);
            m_failedNames.append(name);
        }

        ++m_queueIndex;
        ++m_filesDone;
        updateProgress(m_filesDone);
        emit uploadStateChanged();

        QTimer::singleShot(0, this, [this]() { uploadNext(); });
    });
}

void PanoramaxClient::completeUploadSet()
{
    setStatus(tr("Finishing upload…"));

    QNetworkRequest request = apiRequest(
        QStringLiteral("/upload_sets/%1/complete").arg(m_uploadSetId));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    QNetworkReply *reply = m_manager->post(request, QByteArrayLiteral("{}"));
    m_activeUpload = reply;
    setTimeout(reply, 60000);

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (m_activeUpload == reply) {
            m_activeUpload = nullptr;
        }
        if (!m_busy) {
            return;
        }

        const QByteArray body = readReplyBody(reply);
        const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        // 409 means the upload set was already completed, which is harmless.
        // Any other failure (including 400) must be surfaced to the user.
        const bool ok = reply->error() == QNetworkReply::NoError || httpStatus == 409;
        if (!ok) {
            failUpload(replyErrorMessage(reply, body));
            return;
        }

        startPolling();
    });
}

void PanoramaxClient::startPolling()
{
    setStatus(tr("Processing pictures…"));
    m_pollElapsed = 0;
    m_polling = false;
    m_pollTimer->start();
}

void PanoramaxClient::pollUploadSet()
{
    if (!m_busy) {
        m_pollTimer->stop();
        return;
    }

    if (m_polling) {
        return;
    }

    m_pollElapsed += m_pollTimer->interval();
    if (m_pollElapsed > 15 * 60 * 1000) {
        m_pollTimer->stop();
        m_polling = false;
        fetchUploadSetFiles();
        return;
    }

    m_polling = true;

    QNetworkRequest request = apiRequest(QStringLiteral("/upload_sets/%1").arg(m_uploadSetId));
    QNetworkReply *reply = m_manager->get(request);
    m_activeUpload = reply;
    setTimeout(reply, 30000);

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (m_activeUpload == reply) {
            m_activeUpload = nullptr;
        }
        m_polling = false;
        if (!m_busy) {
            return;
        }

        const QByteArray body = readReplyBody(reply);
        const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

        if (reply->error() != QNetworkReply::NoError) {
            if (httpStatus == 401 || httpStatus == 403 || httpStatus == 404) {
                failUpload(replyErrorMessage(reply, body));
            }
            return;
        }

        const QJsonDocument document = QJsonDocument::fromJson(body);
        if (!document.isObject()) {
            return;
        }

        const QJsonObject object = document.object();
        const QJsonObject items = object.value(QStringLiteral("items_status")).toObject();

        QVariantMap status;
        status.insert(QStringLiteral("ready"), object.value(QStringLiteral("ready")).toBool(false));
        status.insert(QStringLiteral("prepared"), items.value(QStringLiteral("prepared")).toInt());
        status.insert(QStringLiteral("preparing"), items.value(QStringLiteral("preparing")).toInt());
        status.insert(QStringLiteral("notProcessed"),
                      items.value(QStringLiteral("not_processed")).toInt());
        status.insert(QStringLiteral("rejected"), items.value(QStringLiteral("rejected")).toInt());
        status.insert(QStringLiteral("broken"), items.value(QStringLiteral("broken")).toInt());

        QVariantList collections;
        const QJsonArray collectionArray = object.value(QStringLiteral("associated_collections"))
                                               .toArray();
        for (const QJsonValue &value : collectionArray) {
            const QJsonObject collection = value.toObject();
            QVariantMap entry;
            entry.insert(QStringLiteral("id"), collection.value(QStringLiteral("id")).toString());
            entry.insert(QStringLiteral("title"),
                         collection.value(QStringLiteral("title")).toString());
            entry.insert(QStringLiteral("nbItems"),
                         collection.value(QStringLiteral("nb_items")).toInt());
            collections.append(entry);
        }
        status.insert(QStringLiteral("collections"), collections);

        m_lastProcessing = status;

        if (status.value(QStringLiteral("ready")).toBool()) {
            m_pollTimer->stop();
            fetchUploadSetFiles();
        }
    });
}

void PanoramaxClient::fetchUploadSetFiles()
{
    m_polling = false;

    QNetworkRequest request = apiRequest(QStringLiteral("/upload_sets/%1/files").arg(m_uploadSetId));
    QNetworkReply *reply = m_manager->get(request);
    m_activeUpload = reply;
    setTimeout(reply, 60000);

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (m_activeUpload == reply) {
            m_activeUpload = nullptr;
        }
        if (!m_busy) {
            return;
        }

        QVariantList rejections;
        const QByteArray body = readReplyBody(reply);
        if (reply->error() != QNetworkReply::NoError) {
            failUpload(replyErrorMessage(reply, body));
            return;
        }

        const QJsonDocument document = QJsonDocument::fromJson(body);

        QJsonArray files;
        if (document.isArray()) {
            files = document.array();
        } else if (document.isObject()) {
            // The API wraps the list in an object: { "files": [ ... ], "links": [ ... ] }
            files = document.object().value(QStringLiteral("files")).toArray();
        }

        for (const QJsonValue &value : files) {
            const QJsonObject file = value.toObject();
            const QJsonObject rejected = file.value(QStringLiteral("rejected")).toObject();
            if (rejected.isEmpty()) {
                continue;
            }

            QVariantMap entry;
            entry.insert(QStringLiteral("name"), file.value(QStringLiteral("file_name")).toString());
            entry.insert(QStringLiteral("reason"),
                         rejected.value(QStringLiteral("reason")).toString());
            entry.insert(QStringLiteral("message"),
                         rejected.value(QStringLiteral("message")).toString());
            entry.insert(QStringLiteral("severity"),
                         rejected.value(QStringLiteral("severity")).toString());
            rejections.append(entry);
        }

        QVariantMap summary = m_lastProcessing;
        summary.insert(QStringLiteral("uploadSetId"), m_uploadSetId);
        summary.insert(QStringLiteral("rejections"), rejections);
        summary.insert(QStringLiteral("failed"), m_failedPaths.size());

        const bool ready = summary.value(QStringLiteral("ready")).toBool();
        updateProgress(m_filesTotal);
        m_pollTimer->stop();
        setBusy(false);
        setStatus(ready ? tr("Upload finished")
                        : tr("Upload sent, processing continues on the instance"));
        emit uploadStateChanged();
        emit uploadCompleted(summary);
    });
}

void PanoramaxClient::failUpload(const QString &message)
{
    m_pollTimer->stop();
    m_polling = false;
    m_uploadAttempt = 0;
    setBusy(false);
    setStatus(message);
    emit progressChanged();
    emit uploadStateChanged();
    emit uploadFailed(message);
}

QUrl PanoramaxClient::apiUrl(const QString &path) const
{
    return QUrl(normalizeInstanceUrl(m_instanceUrl) + QStringLiteral("/api") + path);
}

QNetworkRequest PanoramaxClient::apiRequest(const QString &path, bool withAuth) const
{
    return apiRequestFor(apiUrl(path), withAuth);
}

QNetworkRequest PanoramaxClient::apiRequestFor(const QUrl &url, bool withAuth) const
{
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::FollowRedirectsAttribute, true);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("harbour-periscope/%1 (SailfishOS)")
                          .arg(QCoreApplication::applicationVersion()));
    request.setRawHeader("Accept", "application/json");
    if (withAuth && !m_token.isEmpty()) {
        request.setRawHeader("Authorization", "Bearer " + m_token.toUtf8());
    }
    return request;
}

void PanoramaxClient::setTimeout(QNetworkReply *reply, int milliseconds)
{
    QTimer *timer = new QTimer(reply);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, reply, [reply]() {
        if (reply->isRunning()) {
            reply->setProperty("timedOut", true);
            reply->abort();
        }
    });
    // Don't abort (and close the device of) a reply that already finished.
    connect(reply, &QNetworkReply::finished, timer, &QTimer::stop);
    timer->start(milliseconds);
}

QString PanoramaxClient::replyErrorMessage(QNetworkReply *reply, const QByteArray &body) const
{
    if (reply->property("timedOut").toBool()) {
        return tr("Connection timed out");
    }

    const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

    if (httpStatus == 401) {
        return tr("Authentication required");
    }
    if (httpStatus == 403) {
        return tr("Token expired or action not allowed");
    }
    if (httpStatus == 404) {
        return tr("Not found on this instance");
    }
    if (httpStatus == 413) {
        return tr("The picture is too large for this instance");
    }

    const QJsonDocument document = QJsonDocument::fromJson(body);
    if (document.isObject()) {
        const QString message
            = document.object().value(QStringLiteral("message")).toString().trimmed();
        if (!message.isEmpty()) {
            return message;
        }
    }

    switch (reply->error()) {
    case QNetworkReply::HostNotFoundError:
        return tr("Host not found");
    case QNetworkReply::TimeoutError:
        return tr("Connection timed out");
    case QNetworkReply::SslHandshakeFailedError:
        return tr("Secure connection failed");
    case QNetworkReply::NetworkSessionFailedError:
        return tr("No network connection");
    default:
        break;
    }

    return reply->errorString();
}

void PanoramaxClient::setBusy(bool busy)
{
    if (m_busy == busy) {
        return;
    }
    m_busy = busy;
    emit busyChanged();
}

void PanoramaxClient::setStatus(const QString &status)
{
    if (m_statusText == status) {
        return;
    }
    m_statusText = status;
    emit statusTextChanged();
}

void PanoramaxClient::updateProgress(int done)
{
    const int percent = qMin(100, static_cast<int>(100.0 * done / qMax(1, m_filesTotal)));
    if (percent == m_progress) {
        return;
    }
    m_progress = percent;
    emit progressChanged();
}

void PanoramaxClient::clearSession()
{
    // A pending /users/me reply would otherwise sign the user back in.
    cancelAuthCheck();

    m_secrets->clearAll();

    m_token.clear();
    m_userName.clear();
    m_signedIn = false;
    m_tosAccepted = false;

    m_settings->remove(QStringLiteral("userName"));
    m_settings->sync();

    emit authChanged();
}

void PanoramaxClient::cancelUpload()
{
    m_pollTimer->stop();
    m_polling = false;

    // Detach and mark the client idle before aborting so that the reply's
    // finished handler does not continue the upload.
    QNetworkReply *reply = m_activeUpload;
    m_activeUpload = nullptr;

    m_queuePaths.clear();
    m_queueNames.clear();
    m_failedPaths.clear();
    m_failedNames.clear();
    m_uploadSetId.clear();
    m_queueIndex = 0;
    m_filesDone = 0;
    m_filesTotal = 0;
    m_progress = 0;
    m_lastProcessing.clear();
    m_pollElapsed = 0;
    m_uploadAttempt = 0;

    setBusy(false);

    if (reply) {
        reply->abort();
    }

    emit progressChanged();
    emit uploadStateChanged();
}
