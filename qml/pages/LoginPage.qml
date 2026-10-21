import QtQuick 2.0
import Sailfish.Silica 1.0

Page {
    id: page

    allowedOrientations: Orientation.All

    function login() {
        errorLabel.text = ""
        app.login(serverField.text, userField.text, passwordField.text)
    }

    Connections {
        target: app
        onLoginFinished: {
            if (ok)
                pageStack.replace(Qt.resolvedUrl("StreamsPage.qml"))
            else
                errorLabel.text = message
        }
    }

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.height + Theme.paddingLarge

        Column {
            id: column

            width: parent.width
            spacing: Theme.paddingMedium

            PageHeader {
                title: qsTr("Log in to FreshRSS")
            }

            TextField {
                id: serverField

                width: parent.width
                label: qsTr("Server address")
                placeholderText: "https://rss.example.com"
                text: app.serverUrl
                inputMethodHints: Qt.ImhUrlCharactersOnly | Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
                EnterKey.iconSource: "image://theme/icon-m-enter-next"
                EnterKey.onClicked: userField.focus = true
            }

            TextField {
                id: userField

                width: parent.width
                label: qsTr("User name")
                placeholderText: label
                text: app.userName
                inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
                EnterKey.iconSource: "image://theme/icon-m-enter-next"
                EnterKey.onClicked: passwordField.focus = true
            }

            PasswordField {
                id: passwordField

                width: parent.width
                label: qsTr("API password")
                placeholderText: label
                EnterKey.iconSource: "image://theme/icon-m-enter-accept"
                EnterKey.enabled: loginButton.enabled
                EnterKey.onClicked: page.login()
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.secondaryHighlightColor
                text: qsTr("The API password is not your login password. Set it in FreshRSS under Profile → API management. API access must be enabled under Administration → Authentication.")
            }

            Label {
                id: errorLabel

                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                visible: text.length > 0
                wrapMode: Text.Wrap
                color: Theme.highlightColor
            }

            Button {
                id: loginButton

                anchors.horizontalCenter: parent.horizontalCenter
                text: qsTr("Log in")
                enabled: !app.loggingIn && serverField.text.length > 0 && userField.text.length > 0
                         && passwordField.text.length > 0
                onClicked: page.login()
            }

            BusyIndicator {
                anchors.horizontalCenter: parent.horizontalCenter
                size: BusyIndicatorSize.Medium
                running: app.loggingIn
            }
        }
    }
}
