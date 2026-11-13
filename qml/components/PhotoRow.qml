import QtQuick 2.5
import Sailfish.Silica 1.0

Item {
    id: row

    property var photo

    signal removeRequested

    height: Theme.itemSizeMedium

    Image {
        id: thumbnail
        x: Theme.horizontalPageMargin
        anchors.verticalCenter: parent.verticalCenter
        width: Theme.iconSizeMedium
        height: width
        asynchronous: true
        autoTransform: true
        fillMode: Image.PreserveAspectCrop
        source: row.photo ? (row.photo.url ? row.photo.url : "file://" + row.photo.filePath) : ""
        sourceSize.width: width
        sourceSize.height: height
    }

    Column {
        anchors {
            left: thumbnail.right
            leftMargin: Theme.paddingMedium
            right: removeButton.left
            rightMargin: Theme.paddingMedium
            verticalCenter: parent.verticalCenter
        }
        spacing: Theme.paddingSmall

        Label {
            width: parent.width
            text: row.photo ? row.photo.fileName : ""
            font.pixelSize: Theme.fontSizeSmall
            truncationMode: TruncationMode.Fade
        }

        Label {
            width: parent.width
            font.pixelSize: Theme.fontSizeExtraSmall
            color: row.photo && row.photo.hasGps && row.photo.hasDate ? Theme.secondaryColor : Theme.errorColor
            truncationMode: TruncationMode.Fade
            text: {
                if (!row.photo)
                    return "";
                if (!row.photo.valid)
                    return row.photo.error ? row.photo.error : qsTr("Unreadable picture");
                if (!row.photo.jpeg)
                    return qsTr("Not a JPEG picture");

                var parts = [row.photo.hasGps ? qsTr("GPS") : qsTr("No GPS"), row.photo.hasDate ? row.photo.capturedAt : qsTr("No date")];
                return parts.join(" · ");
            }
        }
    }

    IconButton {
        id: removeButton
        anchors {
            right: parent.right
            rightMargin: Theme.horizontalPageMargin
            verticalCenter: parent.verticalCenter
        }
        icon.source: "image://theme/icon-m-remove"
        onClicked: row.removeRequested()
    }
}
