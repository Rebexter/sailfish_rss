import QtQuick 2.0
import Sailfish.Silica 1.0

Page {
    id: page

    allowedOrientations: Orientation.All

    SilicaFlickable {
        anchors.fill: parent

        PageHeader {
            title: "SailRSS"
        }

        ViewPlaceholder {
            enabled: true
            text: qsTr("No account")
            hintText: qsTr("Connecting to FreshRSS is coming soon")
        }
    }
}
