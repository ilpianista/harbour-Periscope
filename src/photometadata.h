#ifndef PHOTOMETADATA_H
#define PHOTOMETADATA_H

#include <QObject>
#include <QVariantMap>

class PhotoMetadata : public QObject
{
    Q_OBJECT

public:
    explicit PhotoMetadata(QObject *parent = nullptr);

    Q_INVOKABLE QVariantMap read(const QString &path) const;
};

#endif
