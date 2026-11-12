import QtQuick 2.2
import Sailfish.Silica 1.0
import Sailfish.Pickers 1.0
import "../components"

Page {
    id: page
    allowedOrientations: Orientation.All

    property var photos: []
    property int uploadCount: 0
    property int noGpsCount: 0
    property int noDateCount: 0
    property int invalidCount: 0
    property bool singleSequence: false
    property bool finished: false
    property var summary: ({})
    property string errorText
    property bool authPrompted: false

    readonly property bool accountReady: !client.authEnabled || (client.signedIn && (!client.tosRequired || client.tosAccepted))

    function maybeOpenAuth() {
        if (authPrompted || !client.authEnabled || client.signedIn || !client.authResolved)
            return;
        if (page.status !== PageStatus.Active)
            return;
        authPrompted = true;
        pageStack.replace(Qt.resolvedUrl("AuthPage.qml"));
    }

    Component.onCompleted: maybeOpenAuth()
    onStatusChanged: if (status === PageStatus.Active)
        maybeOpenAuth()

    function updateCounts() {
        var uploadable = 0;
        var noGps = 0;
        var noDate = 0;
        var invalid = 0;
        for (var i = 0; i < photos.length; ++i) {
            var p = photos[i];
            if (!p.valid || !p.jpeg) {
                invalid++;
                continue;
            }
            if (!p.hasGps)
                noGps++;
            if (!p.hasDate)
                noDate++;
            if (p.hasGps && p.hasDate)
                uploadable++;
        }
        uploadCount = uploadable;
        noGpsCount = noGps;
        noDateCount = noDate;
        invalidCount = invalid;
    }

    function addSelected(model) {
        var list = photos.slice();
        for (var i = 0; i < model.count; ++i) {
            var item = model.get(i);
            var exists = false;
            for (var j = 0; j < list.length; ++j) {
                if (list[j].filePath === item.filePath) {
                    exists = true;
                    break;
                }
            }
            if (exists)
                continue;
            var meta = metadata.read(item.filePath);
            list.push({
                "filePath": meta.filePath,
                "fileName": meta.fileName,
                "valid": meta.valid,
                "jpeg": meta.jpeg,
                "hasGps": meta.hasGps,
                "latitude": meta.latitude,
                "longitude": meta.longitude,
                "hasDate": meta.capturedAt ? true : false,
                "capturedAt": meta.capturedAt ? meta.capturedAt : "",
                "error": meta.error ? meta.error : "",
                "url": item.url
            });
        }
        photos = list;
        updateCounts();
    }

    function openPicker() {
        if (picker.selectedContent)
            picker.selectedContent.clear();
        if (picker._selectedModel)
            picker._selectedModel.clear();
        picker.open();
    }

    function removeAt(index) {
        var list = photos.slice();
        list.splice(index, 1);
        photos = list;
        updateCounts();
    }

    function uploadPaths() {
        var result = [];
        for (var i = 0; i < photos.length; ++i) {
            var p = photos[i];
            if (p.valid && p.jpeg && p.hasGps && p.hasDate)
                result.push(p.filePath);
        }
        return result;
    }

    function startUpload() {
        errorText = "";
        summary = ({});
        finished = false;
        var title = titleField.text.trim();
        if (title.length === 0)
            title = titleField.placeholderText;
        client.startUpload(uploadPaths(), title, tagField.text, singleSequence);
    }

    function defaultTitle() {
        return qsTr("Sequence %1").arg(new Date().toLocaleString(Qt.locale(), Locale.ShortFormat));
    }

    function clearForm() {
        photos = [];
        singleSequence = false;
        sequenceSwitch.checked = false;
        titleField.text = "";
        titleField.placeholderText = page.defaultTitle();
        tagField.text = "";
        updateCounts();
    }

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: content.height + Theme.paddingLarge

        PullDownMenu {
            MenuItem {
                text: qsTr("Settings")
                onClicked: pageStack.push(Qt.resolvedUrl("SettingsPage.qml"))
            }

            MenuItem {
                enabled: page.accountReady && page.uploadCount > 0 && !client.busy && !page.finished
                text: page.uploadCount === 1 ? qsTr("Send picture") : qsTr("Send pictures")
                onClicked: Remorse.popupAction(page, qsTr("Uploading…"), function () {
                    page.startUpload();
                })
            }
        }

        Column {
            id: content
            width: parent.width
            spacing: Theme.paddingLarge

            PageHeader {
                title: qsTr("Periscope")
            }

            Column {
                visible: page.accountReady
                width: parent.width
                spacing: Theme.paddingMedium

                TextField {
                    id: titleField
                    width: parent.width
                    label: qsTr("Title (optional)")
                    placeholderText: page.defaultTitle()
                    enabled: !client.busy
                }

                Button {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: qsTr("Choose pictures")
                    enabled: !client.busy
                    onClicked: page.openPicker()
                }

                Label {
                    visible: page.photos.length > 0
                    x: Theme.horizontalPageMargin
                    width: parent.width - 2 * Theme.horizontalPageMargin
                    wrapMode: Text.Wrap
                    font.pixelSize: Theme.fontSizeSmall
                    color: Theme.secondaryColor
                    text: {
                        var parts = [qsTr("%1 selected").arg(page.photos.length)];
                        if (page.noGpsCount > 0)
                            parts.push(qsTr("%1 without GPS").arg(page.noGpsCount));
                        if (page.noDateCount > 0)
                            parts.push(qsTr("%1 without date").arg(page.noDateCount));
                        if (page.invalidCount > 0)
                            parts.push(qsTr("%1 not usable").arg(page.invalidCount));
                        return parts.join(" · ");
                    }
                }

                Label {
                    visible: page.photos.length > 0 && page.uploadCount < page.photos.length
                    x: Theme.horizontalPageMargin
                    width: parent.width - 2 * Theme.horizontalPageMargin
                    wrapMode: Text.Wrap
                    font.pixelSize: Theme.fontSizeExtraSmall
                    color: Theme.secondaryColor
                    text: qsTr("Pictures need both a GPS position and a capture date to be uploaded.")
                }

                Repeater {
                    model: page.photos

                    delegate: PhotoRow {
                        width: parent.width
                        photo: modelData
                        onRemoveRequested: page.removeAt(index)
                        enabled: !client.busy
                    }
                }

                SectionHeader {
                    text: qsTr("Upload")
                }

                Label {
                    x: Theme.horizontalPageMargin
                    width: parent.width - 2 * Theme.horizontalPageMargin
                    wrapMode: Text.Wrap
                    font.pixelSize: Theme.fontSizeExtraSmall
                    color: Theme.secondaryColor
                    text: qsTr("Faces and number plates are blurred by the Panoramax instance before publication ONLY if it has a blurring service enabled. Check your instance's policy.")
                }

                TextField {
                    id: tagField
                    width: parent.width
                    label: qsTr("Tags (optional)")
                    placeholderText: "transport=car, wd|P31=Q125191"
                    enabled: !client.busy
                }

                Label {
                    x: Theme.horizontalPageMargin
                    width: parent.width - 2 * Theme.horizontalPageMargin
                    wrapMode: Text.Wrap
                    font.pixelSize: Theme.fontSizeExtraSmall
                    color: Theme.secondaryColor
                    text: qsTr("Comma-separated key=value tags, applied to the whole upload.")
                }

                TextSwitch {
                    id: sequenceSwitch
                    text: qsTr("Keep all pictures in one sequence")
                    description: qsTr("Useful for a walk, a ride or a drive covering many pictures")
                    enabled: !client.busy
                    onCheckedChanged: page.singleSequence = checked
                }

                Column {
                    visible: client.busy
                    width: parent.width
                    spacing: Theme.paddingMedium

                    ProgressBar {
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: parent.width - 2 * Theme.horizontalPageMargin
                        value: client.progress / 100.0
                        label: client.statusText
                        valueText: client.filesTotal > 0 ? qsTr("%1/%2").arg(client.filesDone).arg(client.filesTotal) : ""
                    }
                }

                Label {
                    visible: page.errorText.length > 0
                    x: Theme.horizontalPageMargin
                    width: parent.width - 2 * Theme.horizontalPageMargin
                    wrapMode: Text.Wrap
                    color: Theme.errorColor
                    text: page.errorText
                }

                Column {
                    visible: page.finished
                    width: parent.width
                    spacing: Theme.paddingMedium

                    SectionHeader {
                        text: qsTr("Result")
                    }

                    Label {
                        x: Theme.horizontalPageMargin
                        width: parent.width - 2 * Theme.horizontalPageMargin
                        wrapMode: Text.Wrap
                        text: {
                            var s = page.summary;
                            var parts = [];
                            if (s.prepared > 0)
                                parts.push(qsTr("%1 uploaded").arg(s.prepared));
                            if (s.rejected > 0)
                                parts.push(qsTr("%1 rejected").arg(s.rejected));
                            if (s.failed > 0)
                                parts.push(qsTr("%1 failed").arg(s.failed));
                            if (parts.length === 0)
                                parts.push(qsTr("Finished"));
                            return parts.join(" · ");
                        }
                    }

                    Label {
                        visible: page.finished && page.summary.ready === false
                        x: Theme.horizontalPageMargin
                        width: parent.width - 2 * Theme.horizontalPageMargin
                        wrapMode: Text.Wrap
                        font.pixelSize: Theme.fontSizeSmall
                        color: Theme.secondaryColor
                        text: qsTr("The instance is still processing some pictures. You can check the result on its website later.")
                    }

                    Repeater {
                        model: page.summary.collections || []

                        delegate: Column {
                            width: parent.width
                            spacing: Theme.paddingSmall

                            Label {
                                x: Theme.horizontalPageMargin
                                width: parent.width - 2 * Theme.horizontalPageMargin
                                wrapMode: Text.Wrap
                                color: Theme.highlightColor
                                text: modelData.title ? modelData.title : qsTr("Panoramax collection")
                            }

                            Label {
                                visible: modelData.nbItems > 0
                                x: Theme.horizontalPageMargin
                                width: parent.width - 2 * Theme.horizontalPageMargin
                                wrapMode: Text.Wrap
                                font.pixelSize: Theme.fontSizeSmall
                                color: Theme.secondaryColor
                                text: modelData.nbItems === 1 ? qsTr("1 picture published") : qsTr("%1 pictures published").arg(modelData.nbItems)
                            }

                            Label {
                                x: Theme.horizontalPageMargin
                                width: parent.width - 2 * Theme.horizontalPageMargin
                                wrapMode: Text.Wrap
                                textFormat: Text.StyledText
                                linkColor: Theme.highlightColor
                                font.underline: true
                                text: "<a href=\"" + client.instanceUrl + "/?seq=" + modelData.id + "\">" + client.instanceUrl + "/?seq=" + modelData.id + "</a>"
                                onLinkActivated: Qt.openUrlExternally(link)
                            }
                        }
                    }

                    Repeater {
                        model: page.summary.rejections || []

                        delegate: Column {
                            width: parent.width
                            spacing: Theme.paddingSmall

                            Label {
                                x: Theme.horizontalPageMargin
                                width: parent.width - 2 * Theme.horizontalPageMargin
                                wrapMode: Text.Wrap
                                font.pixelSize: Theme.fontSizeSmall
                                color: modelData.severity === "error" ? Theme.errorColor : Theme.secondaryColor
                                text: {
                                    var message = modelData.message;
                                    if (!message)
                                        message = modelData.reason;
                                    return modelData.name + ": " + message;
                                }
                            }
                        }
                    }

                    Button {
                        anchors.horizontalCenter: parent.horizontalCenter
                        visible: client.hasFailedFiles
                        text: qsTr("Retry failed pictures")
                        onClicked: {
                            page.finished = false;
                            page.errorText = "";
                            client.retryUpload();
                        }
                    }
                }
            }
        }
    }

    MultiImagePickerDialog {
        id: picker
        onAccepted: page.addSelected(picker.selectedContent)
    }

    Connections {
        target: client

        onInstanceChecked: {
            if (!ok)
                page.errorText = error;
        }

        onAuthChanged: page.maybeOpenAuth()

        onUploadStarted: {
            page.finished = false;
            page.errorText = "";
        }

        onUploadFailed: {
            page.errorText = message;
        }

        onUploadCompleted: {
            page.summary = summary;
            page.finished = true;
            page.errorText = "";
            page.clearForm();
        }

        onErrorReported: {
            page.errorText = message;
        }
    }
}
