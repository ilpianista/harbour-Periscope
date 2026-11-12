import QtQuick 2.0
import Sailfish.Silica 1.0
import harbour.periscope 1.0
import "pages"

ApplicationWindow {
    id: app

    property PanoramaxClient client: PanoramaxClient {}
    property PhotoMetadata metadata: PhotoMetadata {}

    Component {
        id: setupPage

        SetupPage {}
    }

    Component {
        id: uploadPage

        UploadPage {}
    }

    initialPage: client.configured ? uploadPage : setupPage
    cover: Qt.resolvedUrl("cover/CoverPage.qml")
    allowedOrientations: defaultAllowedOrientations
}
