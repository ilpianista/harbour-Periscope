import QtQuick 2.0
import Sailfish.Silica 1.0
import harbour.periscope 1.0
import "pages"

ApplicationWindow {
    id: app

    property PanoramaxClient client: PanoramaxClient {}
    property PhotoMetadata metadata: PhotoMetadata {}
    property Recorder recorder: Recorder {}

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

    function showUploads() {
        activate();

        if (pageStack.currentPage && pageStack.currentPage.recorderPage) {
            pageStack.navigateBack(PageStackAction.Immediate);
            return;
        }

        pageStack.pop(null);
    }

    function openRecorder() {
        activate();

        var current = pageStack.currentPage;
        if (current && current.recorderPage)
            return;

        pageStack.pop(null);
        current = pageStack.currentPage;
        if (!current || !current.attachRecordPage)
            return;

        current.attachRecordPage();
        if (pageStack.nextPage(current) !== null)
            pageStack.navigateForward(PageStackAction.Immediate);
    }
}
