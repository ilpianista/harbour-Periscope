import QtQuick 2.2
import Sailfish.Silica 1.0
import "../components"

Page {
    id: page
    allowedOrientations: Orientation.All

    property string errorText

    function connectToInstance() {
        errorText = "";
        client.checkInstance(instanceField.text);
    }

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: content.height + Theme.paddingLarge

        PullDownMenu {
            MenuItem {
                visible: client.signedIn
                enabled: !client.busy
                text: qsTr("Sign out")
                onClicked: client.signOut()
            }
        }

        Column {
            id: content
            width: parent.width

            PageHeader {
                title: qsTr("Settings")
            }

            SectionHeader {
                text: qsTr("Instance")
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                color: Theme.highlightColor
                text: client.instanceLabel
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.secondaryColor
                visible: client.signedIn
                text: client.instanceUrl
            }

            TextField {
                id: instanceField
                width: parent.width
                visible: !client.signedIn
                label: qsTr("Instance URL")
                text: client.instanceUrl
                inputMethodHints: Qt.ImhUrlCharactersOnly | Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
                enabled: !client.busy
                EnterKey.enabled: text.length > 0
                EnterKey.onClicked: page.connectToInstance()
            }

            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                visible: !client.signedIn
                text: client.busy ? qsTr("Connecting…") : qsTr("Connect")
                enabled: !client.busy && instanceField.text.trim().length > 0
                onClicked: page.connectToInstance()
            }

            ErrorLabel {
                message: page.errorText
            }

            Column {
                visible: client.authEnabled
                width: parent.width
                spacing: Theme.paddingMedium

                SectionHeader {
                    text: qsTr("Account")
                }

                Label {
                    x: Theme.horizontalPageMargin
                    width: parent.width - 2 * Theme.horizontalPageMargin
                    wrapMode: Text.Wrap
                    text: client.signedIn ? qsTr("Signed in as %1").arg(client.userName) : qsTr("Not signed in")
                }

                Button {
                    anchors.horizontalCenter: parent.horizontalCenter
                    visible: !client.signedIn
                    enabled: !client.busy
                    text: qsTr("Sign in")
                    onClicked: pageStack.push(Qt.resolvedUrl("AuthPage.qml"))
                }

                Button {
                    anchors.horizontalCenter: parent.horizontalCenter
                    visible: client.signedIn && client.tosRequired && !client.tosAccepted
                    enabled: !client.busy
                    text: qsTr("Accept the terms of service")
                    onClicked: client.acceptTos()
                }
            }

            SectionHeader {
                text: qsTr("Recording")
            }

            Slider {
                id: intervalSlider
                width: parent.width
                label: qsTr("Snapshot interval")
                valueText: qsTr("%1 s").arg(Math.round(value))
                minimumValue: 1
                maximumValue: 60
                stepSize: 1
                value: recorder.intervalSeconds
                onValueChanged: {
                    var seconds = Math.round(value);
                    if (seconds !== recorder.intervalSeconds)
                        recorder.intervalSeconds = seconds;
                }
            }
        }
    }

    Connections {
        target: client

        onInstanceChecked: {
            page.errorText = ok ? "" : error;
        }

        onTosAcceptedResult: {
            if (!ok)
                page.errorText = error;
        }
    }
}
