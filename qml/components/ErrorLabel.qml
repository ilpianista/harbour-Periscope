import QtQuick 2.2
import Sailfish.Silica 1.0

Label {
    property string message

    visible: message.length > 0
    x: Theme.horizontalPageMargin
    width: parent.width - 2 * Theme.horizontalPageMargin
    wrapMode: Text.Wrap
    color: Theme.errorColor
    text: message
}
