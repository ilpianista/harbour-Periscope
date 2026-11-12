import QtQuick 2.2
import Sailfish.Silica 1.0

Page {
    id: page
    allowedOrientations: Orientation.All

    property string claimUrl
    property string errorText
    property bool waiting
    property bool closing

    readonly property bool needsTos: client.signedIn && client.tosRequired && !client.tosAccepted

    function finish() {
        if (closing || status !== PageStatus.Active)
            return;
        closing = true;
        if (pageStack.depth <= 1)
            pageStack.replace(Qt.resolvedUrl("UploadPage.qml"));
        else
            pageStack.pop();
    }

    onStatusChanged: {
        if (status === PageStatus.Active && client.signedIn && (!client.tosRequired || client.tosAccepted))
            finish();
    }

    function signInWithBrowser() {
        errorText = "";
        claimUrl = "";
        waiting = true;
        client.generateToken();
    }

    function useToken() {
        errorText = "";
        waiting = false;
        client.saveToken(tokenArea.text);
        client.checkAuth();
    }

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: content.height + Theme.paddingLarge

        Column {
            id: content
            width: parent.width
            spacing: Theme.paddingLarge

            PageHeader {
                title: qsTr("Sign in")
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                text: qsTr("Pictures are uploaded on your behalf, so %1 needs to know who you are.").arg(client.instanceLabel)
            }

            Label {
                visible: client.signedIn && !page.needsTos
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                color: Theme.highlightColor
                text: qsTr("Signed in as %1").arg(client.userName)
            }

            Column {
                visible: page.needsTos
                width: parent.width
                spacing: Theme.paddingMedium

                Label {
                    x: Theme.horizontalPageMargin
                    width: parent.width - 2 * Theme.horizontalPageMargin
                    wrapMode: Text.Wrap
                    text: qsTr("Please read and accept the terms of service of %1 before uploading.").arg(client.instanceLabel)
                }

                Button {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: qsTr("Read the terms of service")
                    onClicked: Qt.openUrlExternally(client.instanceUrl + "/terms")
                }

                Button {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: qsTr("Accept the terms of service")
                    onClicked: client.acceptTos()
                }
            }

            Column {
                visible: !client.signedIn
                width: parent.width
                spacing: Theme.paddingMedium

                Button {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: page.waiting ? qsTr("Waiting for sign-in…") : qsTr("Sign in with the browser")
                    enabled: !page.waiting
                    onClicked: page.signInWithBrowser()
                }

                Button {
                    anchors.horizontalCenter: parent.horizontalCenter
                    visible: page.claimUrl.length > 0
                    text: qsTr("Open the sign-in page again")
                    onClicked: Qt.openUrlExternally(page.claimUrl)
                }

                Label {
                    visible: page.waiting
                    x: Theme.horizontalPageMargin
                    width: parent.width - 2 * Theme.horizontalPageMargin
                    wrapMode: Text.Wrap
                    horizontalAlignment: Text.AlignHCenter
                    font.pixelSize: Theme.fontSizeSmall
                    color: Theme.secondaryColor
                    text: qsTr("Complete the sign-in in your browser, then come back here.")
                }

                BusyIndicator {
                    anchors.horizontalCenter: parent.horizontalCenter
                    size: BusyIndicatorSize.Medium
                    running: page.waiting
                    visible: running
                }

                SectionHeader {
                    text: qsTr("Or use a token")
                }

                TextArea {
                    id: tokenArea
                    width: parent.width
                    label: qsTr("Token")
                    placeholderText: qsTr("Paste your Panoramax token")
                    enabled: !client.busy
                }

                Button {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: qsTr("Use this token")
                    enabled: tokenArea.text.trim().length > 0
                    onClicked: page.useToken()
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

            Label {
                visible: page.waiting
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                horizontalAlignment: Text.AlignHCenter
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.secondaryColor
                text: qsTr("Nothing happened? Make sure the browser opened the sign-in page of %1.").arg(client.instanceLabel)
            }
        }
    }

    Timer {
        interval: 3000
        repeat: true
        running: page.waiting && !client.signedIn && page.status === PageStatus.Active
        onTriggered: client.pollAuth()
    }

    Connections {
        target: client

        onTokenGenerated: {
            if (error.length > 0) {
                page.waiting = false;
                page.errorText = error;
                return;
            }

            page.claimUrl = claimUrl;
            Qt.openUrlExternally(claimUrl);
        }

        onAuthChecked: {
            if (ok) {
                if (tosAccepted || !client.tosRequired)
                    page.finish();
            } else if (!page.waiting) {
                page.errorText = error;
            }
        }

        onTosAcceptedResult: {
            if (ok)
                page.finish();
            else
                page.errorText = error;
        }
    }
}
