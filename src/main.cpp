#include <QtQuick>

#include <sailfishapp.h>

#include "cameracapabilities.h"
#include "panoramaxclient.h"
#include "photometadata.h"
#include "recorder.h"

int main(int argc, char *argv[])
{
    QScopedPointer<QGuiApplication> app(SailfishApp::application(argc, argv));
    QScopedPointer<QQuickView> view(SailfishApp::createView());

    QCoreApplication::setApplicationName(QStringLiteral("harbour-periscope"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.2.0"));
    QCoreApplication::setOrganizationName(QStringLiteral("dev.scarpino"));

    qmlRegisterType<PanoramaxClient>("harbour.periscope", 1, 0, "PanoramaxClient");
    qmlRegisterType<PhotoMetadata>("harbour.periscope", 1, 0, "PhotoMetadata");
    qmlRegisterType<Recorder>("harbour.periscope", 1, 0, "Recorder");
    qmlRegisterType<CameraCapabilities>("harbour.periscope", 1, 0, "CameraCapabilities");

    view->setSource(SailfishApp::pathTo("qml/Periscope.qml"));
    view->show();

    return app->exec();
}
