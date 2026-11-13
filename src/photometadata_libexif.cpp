#include "photometadata.h"

#include <QDateTime>
#include <QFile>
#include <QFileInfo>

#include <libexif/exif-data.h>
#include <libexif/exif-loader.h>
#include <libexif/exif-utils.h>

namespace {

constexpr int kReadChunkSize = 64 * 1024;
constexpr int kMaxScanSize = 512 * 1024;

bool readGpsCoordinate(ExifData *data,
                       ExifTag valueTag,
                       ExifTag referenceTag,
                       char negativeReference,
                       double &coordinate)
{
    if (!data || !data->ifd[EXIF_IFD_GPS]) {
        return false;
    }

    ExifContent *gps = data->ifd[EXIF_IFD_GPS];
    ExifEntry *entry = exif_content_get_entry(gps, valueTag);
    ExifEntry *reference = exif_content_get_entry(gps, referenceTag);
    if (!entry || !reference || !entry->data || !reference->data
        || entry->format != EXIF_FORMAT_RATIONAL || entry->components < 3 || reference->size == 0) {
        return false;
    }

    static const double divisors[] = {1.0, 60.0, 3600.0};
    const ExifByteOrder byteOrder = exif_data_get_byte_order(data);
    double degrees = 0.0;
    for (unsigned int i = 0; i < 3; ++i) {
        const ExifRational rational = exif_get_rational(entry->data + i * sizeof(ExifRational),
                                                        byteOrder);
        if (rational.denominator == 0) {
            return false;
        }
        degrees += double(rational.numerator) / double(rational.denominator) / divisors[i];
    }

    if (char(reference->data[0]) == negativeReference) {
        degrees = -degrees;
    }

    coordinate = degrees;
    return true;
}

QDateTime readCaptureDate(ExifData *data)
{
    ExifEntry *entry = nullptr;
    if (ExifContent *exif = data->ifd[EXIF_IFD_EXIF]) {
        entry = exif_content_get_entry(exif, EXIF_TAG_DATE_TIME_ORIGINAL);
        if (!entry) {
            entry = exif_content_get_entry(exif, EXIF_TAG_DATE_TIME_DIGITIZED);
        }
    }
    if (!entry && data->ifd[EXIF_IFD_0]) {
        entry = exif_content_get_entry(data->ifd[EXIF_IFD_0], EXIF_TAG_DATE_TIME);
    }
    if (!entry) {
        return QDateTime();
    }

    char text[64] = {};
    if (!exif_entry_get_value(entry, text, static_cast<unsigned int>(sizeof(text)))) {
        return QDateTime();
    }

    QDateTime date = QDateTime::fromString(QString::fromLatin1(text).trimmed(),
                                           QStringLiteral("yyyy:MM:dd HH:mm:ss"));
    if (date.isValid()) {
        date.setTimeSpec(Qt::LocalTime);
    }
    return date;
}

} // namespace

PhotoMetadata::PhotoMetadata(QObject *parent)
    : QObject(parent)
{}

QVariantMap PhotoMetadata::read(const QString &path) const
{
    QVariantMap result;
    const QFileInfo info(path);
    result.insert(QStringLiteral("filePath"), path);
    result.insert(QStringLiteral("fileName"), info.fileName());
    result.insert(QStringLiteral("valid"), false);
    result.insert(QStringLiteral("jpeg"), false);
    result.insert(QStringLiteral("hasGps"), false);
    result.insert(QStringLiteral("latitude"), 0.0);
    result.insert(QStringLiteral("longitude"), 0.0);
    result.insert(QStringLiteral("capturedAt"), QString());

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        result.insert(QStringLiteral("error"), tr("Cannot read the picture"));
        return result;
    }

    const QByteArray signature = file.read(2);
    if (signature.size() < 2 || static_cast<unsigned char>(signature.at(0)) != 0xFF
        || static_cast<unsigned char>(signature.at(1)) != 0xD8) {
        result.insert(QStringLiteral("error"), tr("Not a JPEG picture"));
        return result;
    }

    result.insert(QStringLiteral("valid"), true);
    result.insert(QStringLiteral("jpeg"), true);

    file.seek(0);
    ExifLoader *loader = exif_loader_new();
    int scanned = 0;
    while (scanned < kMaxScanSize) {
        QByteArray chunk = file.read(qMin(kReadChunkSize, kMaxScanSize - scanned));
        if (chunk.isEmpty()) {
            break;
        }
        scanned += chunk.size();
        if (!exif_loader_write(loader,
                               reinterpret_cast<unsigned char *>(chunk.data()),
                               static_cast<unsigned int>(chunk.size()))) {
            break;
        }
    }
    file.close();

    ExifData *data = exif_loader_get_data(loader);
    exif_loader_unref(loader);
    if (!data) {
        return result;
    }

    const QDateTime captured = readCaptureDate(data);
    if (captured.isValid()) {
        result.insert(QStringLiteral("capturedAt"),
                      captured.toString(QStringLiteral("yyyy-MM-dd HH:mm")));
    }

    double latitude = 0.0;
    double longitude = 0.0;
    if (readGpsCoordinate(data,
                          static_cast<ExifTag>(EXIF_TAG_GPS_LATITUDE),
                          static_cast<ExifTag>(EXIF_TAG_GPS_LATITUDE_REF),
                          'S',
                          latitude)
        && readGpsCoordinate(data,
                             static_cast<ExifTag>(EXIF_TAG_GPS_LONGITUDE),
                             static_cast<ExifTag>(EXIF_TAG_GPS_LONGITUDE_REF),
                             'W',
                             longitude)
        && latitude >= -90.0 && latitude <= 90.0 && longitude >= -180.0 && longitude <= 180.0) {
        result.insert(QStringLiteral("hasGps"), true);
        result.insert(QStringLiteral("latitude"), latitude);
        result.insert(QStringLiteral("longitude"), longitude);
    }

    exif_data_unref(data);

    return result;
}
