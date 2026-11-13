#ifndef APPSETTINGS_H
#define APPSETTINGS_H

#include <QCoreApplication>
#include <QDir>
#include <QStandardPaths>
#include <QString>

inline QString appSettingsPath()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation))
        .filePath(QCoreApplication::applicationName() + QStringLiteral(".conf"));
}

#endif // APPSETTINGS_H
