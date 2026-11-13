TARGET = harbour-periscope

CONFIG += sailfishapp

# The RPM spec passes the package version so it stays the single source of truth.
isEmpty(APP_VERSION): APP_VERSION = 0.0.0
DEFINES += APP_VERSION=\\\"$$APP_VERSION\\\"

QT += network multimedia

PKGCONFIG += sailfishsecrets

INCLUDEPATH += /usr/include/Sailfish

SAILFISHAPP_ICONS = 86x86 108x108 128x128 172x172 256x256

# By default use the system libexif. Build with CONFIG+=harbour
# to use the self-contained EXIF parser instead, which does
# not add any external runtime dependency (needed by Harbour).
# https://github.com/sailfishos/sdk-harbour-rpmvalidator/pull/191
CONFIG(harbour)|CONFIG(no_libexif) {
    SOURCES += src/photometadata_builtin.cpp
} else {
    PKGCONFIG += libexif
    SOURCES += src/photometadata_libexif.cpp
}

SOURCES += \
    src/cameracapabilities.cpp \
    src/main.cpp \
    src/panoramaxclient.cpp \
    src/recorder.cpp \
    src/securestorage.cpp

HEADERS += \
    src/appsettings.h \
    src/cameracapabilities.h \
    src/panoramaxclient.h \
    src/photometadata.h \
    src/recorder.h \
    src/securestorage.h

OTHER_FILES += \
    qml/Periscope.qml \
    qml/components/ErrorLabel.qml \
    qml/components/PhotoRow.qml \
    qml/cover/CoverPage.qml \
    qml/pages/AuthPage.qml \
    qml/pages/RecordPage.qml \
    qml/pages/SettingsPage.qml \
    qml/pages/SetupPage.qml \
    qml/pages/UploadPage.qml \
    harbour-periscope.desktop \
    rpm/harbour-periscope.changes \
    rpm/harbour-periscope.spec \
    translations/*.ts

CONFIG += sailfishapp_i18n

TRANSLATIONS += \
    translations/harbour-periscope-it.ts
