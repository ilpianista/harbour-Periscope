#include "photometadata.h"

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QHash>

#include <cmath>
#include <limits>

namespace {

// Most JPEGs carry their EXIF block in the first few kilobytes; read a small
// chunk first and only grow it when the EXIF segment is not found yet.
constexpr int kInitialHeaderSize = 64 * 1024;
constexpr int kMaxHeaderSize = 512 * 1024;

quint16 readU16(const QByteArray &data, qint64 offset, bool littleEndian)
{
    if (offset < 0 || offset + 2 > data.size()) {
        return 0;
    }
    const uchar *p = reinterpret_cast<const uchar *>(data.constData() + offset);
    if (littleEndian) {
        return quint16(quint32(p[0]) | (quint32(p[1]) << 8));
    }
    return quint16((quint32(p[0]) << 8) | quint32(p[1]));
}

quint32 readU32(const QByteArray &data, qint64 offset, bool littleEndian)
{
    if (offset < 0 || offset + 4 > data.size()) {
        return 0;
    }
    const uchar *p = reinterpret_cast<const uchar *>(data.constData() + offset);
    if (littleEndian) {
        return quint32(p[0]) | (quint32(p[1]) << 8) | (quint32(p[2]) << 16) | (quint32(p[3]) << 24);
    }
    return (quint32(p[0]) << 24) | (quint32(p[1]) << 16) | (quint32(p[2]) << 8) | quint32(p[3]);
}

int typeSize(quint16 type)
{
    switch (type) {
    case 1: // BYTE
    case 2: // ASCII
    case 6: // SBYTE
    case 7: // UNDEFINED
        return 1;
    case 3: // SHORT
    case 8: // SSHORT
        return 2;
    case 4:  // LONG
    case 9:  // SLONG
    case 11: // FLOAT
        return 4;
    case 5:  // RATIONAL
    case 10: // SRATIONAL
    case 12: // DOUBLE
        return 8;
    default:
        return 0;
    }
}

struct IfdEntry
{
    quint16 tag = 0;
    quint16 type = 0;
    quint32 count = 0;
    QByteArray value;
};

bool readIfd(const QByteArray &tiff,
             quint32 offset,
             bool littleEndian,
             QHash<quint16, IfdEntry> &entries)
{
    if (offset < 8 || offset + 2 > quint32(tiff.size())) {
        return false;
    }

    const quint16 count = readU16(tiff, offset, littleEndian);
    if (count == 0 || count > 512) {
        return false;
    }

    bool found = false;
    for (quint16 i = 0; i < count; ++i) {
        const qint64 entryOffset = qint64(offset) + 2 + qint64(i) * 12;
        if (entryOffset + 12 > tiff.size()) {
            break;
        }

        IfdEntry entry;
        entry.tag = readU16(tiff, entryOffset, littleEndian);
        entry.type = readU16(tiff, entryOffset + 2, littleEndian);
        entry.count = readU32(tiff, entryOffset + 4, littleEndian);

        const int unit = typeSize(entry.type);
        if (unit == 0 || entry.count == 0) {
            continue;
        }

        const qint64 size = qint64(unit) * qint64(entry.count);
        if (size <= 0 || size > tiff.size()) {
            continue;
        }

        qint64 valueOffset = entryOffset + 8;
        if (size > 4) {
            valueOffset = readU32(tiff, entryOffset + 8, littleEndian);
        }
        if (valueOffset < 0 || valueOffset + size > tiff.size()) {
            continue;
        }

        entry.value = tiff.mid(static_cast<int>(valueOffset), static_cast<int>(size));
        entries.insert(entry.tag, entry);
        found = true;
    }

    return found;
}

double readRational(const QByteArray &value, int index, bool littleEndian)
{
    const qint64 offset = qint64(index) * 8;
    if (offset + 8 > value.size()) {
        return std::numeric_limits<double>::quiet_NaN();
    }

    const quint32 numerator = readU32(value, offset, littleEndian);
    const quint32 denominator = readU32(value, offset + 4, littleEndian);
    if (denominator == 0) {
        return std::numeric_limits<double>::quiet_NaN();
    }

    return double(numerator) / double(denominator);
}

QChar asciiRef(const IfdEntry &entry)
{
    if (entry.value.isEmpty()) {
        return QChar();
    }
    return QChar::fromLatin1(entry.value.at(0));
}

QDateTime parseExifDate(const IfdEntry &entry)
{
    if (entry.value.isEmpty()) {
        return QDateTime();
    }

    QByteArray raw = entry.value;
    const int nul = raw.indexOf('\0');
    if (nul >= 0) {
        raw.truncate(nul);
    }

    const QString text = QString::fromLatin1(raw).trimmed();
    QDateTime date = QDateTime::fromString(text, QStringLiteral("yyyy:MM:dd HH:mm:ss"));
    if (!date.isValid()) {
        return QDateTime();
    }
    date.setTimeSpec(Qt::LocalTime);
    return date;
}

bool findExifApp1(const QByteArray &data, QByteArray &tiff)
{
    if (data.size() < 4 || uchar(data.at(0)) != 0xFF || uchar(data.at(1)) != 0xD8) {
        return false;
    }

    qint64 pos = 2;
    while (pos + 2 <= data.size()) {
        if (uchar(data.at(static_cast<int>(pos))) != 0xFF) {
            return false;
        }

        // A marker may be preceded by any number of 0xFF fill bytes.
        while (pos + 1 < data.size() && uchar(data.at(static_cast<int>(pos + 1))) == 0xFF) {
            ++pos;
        }
        if (pos + 4 > data.size()) {
            return false;
        }

        const quint8 marker = uchar(data.at(static_cast<int>(pos + 1)));
        if (marker == 0x01 || (marker >= 0xD0 && marker <= 0xD7)) {
            pos += 2;
            continue;
        }
        if (marker == 0xD9 || marker == 0xDA) {
            return false;
        }

        const quint16 length = readU16(data, pos + 2, false);
        if (length < 2 || pos + 2 + length > data.size()) {
            return false;
        }

        if (marker == 0xE1) {
            const qint64 payloadOffset = pos + 4;
            const qint64 payloadSize = qint64(length) - 2;
            static const char exifHeader[] = {'E', 'x', 'i', 'f', 0, 0};
            if (payloadSize > 6
                && data.mid(static_cast<int>(payloadOffset), 6) == QByteArray(exifHeader, 6)) {
                tiff = data.mid(static_cast<int>(payloadOffset + 6),
                                static_cast<int>(payloadSize - 6));
                return true;
            }
        }

        pos += 2 + length;
    }

    return false;
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

    QByteArray header = file.read(kInitialHeaderSize);

    if (header.size() < 2 || uchar(header.at(0)) != 0xFF || uchar(header.at(1)) != 0xD8) {
        file.close();
        result.insert(QStringLiteral("error"), tr("Not a JPEG picture"));
        return result;
    }

    result.insert(QStringLiteral("valid"), true);
    result.insert(QStringLiteral("jpeg"), true);

    QByteArray tiff;
    bool hasExif = findExifApp1(header, tiff);
    if (!hasExif && header.size() == kInitialHeaderSize) {
        header += file.read(kMaxHeaderSize - header.size());
        hasExif = findExifApp1(header, tiff);
    }

    file.close();

    if (!hasExif) {
        return result;
    }

    if (tiff.size() < 8) {
        return result;
    }

    bool littleEndian = false;
    if (tiff.startsWith(QByteArrayLiteral("II"))) {
        littleEndian = true;
    } else if (!tiff.startsWith(QByteArrayLiteral("MM"))) {
        return result;
    }

    if (readU16(tiff, 2, littleEndian) != 42) {
        return result;
    }

    const quint32 ifd0Offset = readU32(tiff, 4, littleEndian);
    QHash<quint16, IfdEntry> ifd0;
    if (!readIfd(tiff, ifd0Offset, littleEndian, ifd0)) {
        return result;
    }

    QDateTime captured;
    if (ifd0.contains(0x0132)) {
        captured = parseExifDate(ifd0.value(0x0132));
    }

    if (ifd0.contains(0x8769)) {
        const quint32 exifOffset = readU32(ifd0.value(0x8769).value, 0, littleEndian);
        QHash<quint16, IfdEntry> exifIfd;
        if (readIfd(tiff, exifOffset, littleEndian, exifIfd)) {
            if (exifIfd.contains(0x9003)) {
                captured = parseExifDate(exifIfd.value(0x9003));
            } else if (exifIfd.contains(0x9004)) {
                captured = parseExifDate(exifIfd.value(0x9004));
            }
        }
    }

    if (captured.isValid()) {
        result.insert(QStringLiteral("capturedAt"),
                      captured.toString(QStringLiteral("yyyy-MM-dd HH:mm")));
    }

    if (ifd0.contains(0x8825)) {
        const quint32 gpsOffset = readU32(ifd0.value(0x8825).value, 0, littleEndian);
        QHash<quint16, IfdEntry> gps;
        if (readIfd(tiff, gpsOffset, littleEndian, gps) && gps.contains(0x0001)
            && gps.contains(0x0002) && gps.contains(0x0003) && gps.contains(0x0004)) {
            const double latDegrees = readRational(gps.value(0x0002).value, 0, littleEndian);
            const double latMinutes = readRational(gps.value(0x0002).value, 1, littleEndian);
            const double latSeconds = readRational(gps.value(0x0002).value, 2, littleEndian);
            const double lonDegrees = readRational(gps.value(0x0004).value, 0, littleEndian);
            const double lonMinutes = readRational(gps.value(0x0004).value, 1, littleEndian);
            const double lonSeconds = readRational(gps.value(0x0004).value, 2, littleEndian);

            if (!std::isnan(latDegrees) && !std::isnan(latMinutes) && !std::isnan(latSeconds)
                && !std::isnan(lonDegrees) && !std::isnan(lonMinutes) && !std::isnan(lonSeconds)) {
                double latitude = latDegrees + latMinutes / 60.0 + latSeconds / 3600.0;
                double longitude = lonDegrees + lonMinutes / 60.0 + lonSeconds / 3600.0;

                if (asciiRef(gps.value(0x0001)) == QLatin1Char('S')) {
                    latitude = -latitude;
                }
                if (asciiRef(gps.value(0x0003)) == QLatin1Char('W')) {
                    longitude = -longitude;
                }

                if (latitude >= -90.0 && latitude <= 90.0 && longitude >= -180.0
                    && longitude <= 180.0) {
                    result.insert(QStringLiteral("hasGps"), true);
                    result.insert(QStringLiteral("latitude"), latitude);
                    result.insert(QStringLiteral("longitude"), longitude);
                }
            }
        }
    }

    return result;
}
