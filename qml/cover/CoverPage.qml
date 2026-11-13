import QtQuick 2.0
import Sailfish.Silica 1.0

CoverBackground {
    id: cover

    readonly property bool accountReady: !client.authEnabled || (client.signedIn && (!client.tosRequired || client.tosAccepted))

    Column {
        anchors.centerIn: parent
        width: parent.width
        spacing: Theme.paddingSmall

        Image {
            anchors.horizontalCenter: parent.horizontalCenter
            width: parent.width - 5 * Theme.paddingLarge
            height: width
            fillMode: Image.PreserveAspectFit
            source: "/usr/share/icons/hicolor/86x86/apps/harbour-periscope.png"
        }

        Label {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "Periscope"
            font.pixelSize: Theme.fontSizeLarge
            color: Theme.primaryColor
        }

        Label {
            anchors.horizontalCenter: parent.horizontalCenter
            width: parent.width - 2 * Theme.paddingMedium
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            font.pixelSize: Theme.fontSizeSmall
            color: client.busy ? Theme.highlightColor : Theme.secondaryColor
            text: client.busy ? client.statusText : ""
        }

        ProgressBar {
            anchors.horizontalCenter: parent.horizontalCenter
            width: parent.width - 2 * Theme.paddingLarge
            visible: client.busy
            value: client.progress / 100.0
        }
    }

    CoverActionList {
        enabled: cover.accountReady

        CoverAction {
            iconSource: "image://theme/icon-cover-new"

            onTriggered: app.showUploads()
        }

        CoverAction {
            iconSource: "image://theme/icon-cover-record"

            onTriggered: app.openRecorder()
        }
    }
}
