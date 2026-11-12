import QtQuick 2.2
import Sailfish.Silica 1.0

Page {
    id: page
    allowedOrientations: Orientation.All
    backNavigation: false

    property string errorText

    function connectToInstance() {
        errorText = "";
        client.checkInstance(urlField.text);
    }

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: content.height + Theme.paddingLarge

        Column {
            id: content
            width: parent.width
            spacing: Theme.paddingLarge

            PageHeader {
                title: qsTr("Periscope")
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                text: qsTr("Periscope uploads your pictures to Panoramax, an open platform for street-level imagery.")
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.secondaryColor
                text: qsTr("Choose the Panoramax instance you want to use. You can change it later in the settings.")
            }

            TextField {
                id: urlField
                width: parent.width
                label: qsTr("Instance URL")
                placeholderText: "https://panoramax.example.com"
                inputMethodHints: Qt.ImhUrlCharactersOnly | Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
                EnterKey.enabled: text.length > 0
                EnterKey.onClicked: page.connectToInstance()
            }

            Label {
                visible: page.errorText.length > 0
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                color: Theme.errorColor
                text: page.errorText
            }

            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                text: client.busy ? qsTr("Connecting…") : qsTr("Connect")
                enabled: !client.busy && urlField.text.trim().length > 0
                onClicked: page.connectToInstance()
            }

            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                text: qsTr("Find an instance")
                onClicked: Qt.openUrlExternally("https://explore.panoramax.fr/en/instances")
            }
        }

        BusyIndicator {
            anchors.centerIn: parent
            size: BusyIndicatorSize.Large
            running: client.busy
            visible: running
        }
    }

    Connections {
        target: client

        onInstanceChecked: {
            if (ok) {
                page.errorText = "";
                if (client.authEnabled && !client.signedIn)
                    pageStack.replace(Qt.resolvedUrl("AuthPage.qml"));
                else
                    pageStack.replace(Qt.resolvedUrl("UploadPage.qml"));
            } else {
                page.errorText = error;
            }
        }
    }
}
