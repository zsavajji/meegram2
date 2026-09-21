import QtQuick 1.1
import com.nokia.meego 1.1
import MyComponent 1.0
import "components"

Page {
    id: root

    orientationLock: PageOrientation.LockPortrait

    TopBar {
        id: header
        title: qsTr("Language")
    }

    ListView {
        id: listView
        anchors {
            bottom: parent.bottom
            left: parent.left
            right: parent.right
            top: header.bottom
        }

        model: appManager.languagePackInfoModel

        delegate: ListItem {
            Column {
                id: column
                anchors {
                    left: parent.left
                    leftMargin: 12
                    right: icon.left
                    rightMargin: 12
                    verticalCenter: parent.verticalCenter
                }

                Label {
                    width: parent.width
                    font.pixelSize: 26
                    font.bold: true
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                    text: model.name
                }

                Label {
                    width: parent.width
                    font.pixelSize: 20
                    font.family: "Nokia Pure Light"
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                    text: model.nativeName
                }
            }

            Image {
                id: icon
                anchors {
                    right: parent.right
                    rightMargin: 12
                    verticalCenter: parent.verticalCenter
                }
                source: settings.languagePackId !== model.id
                    ? "image://theme/meegotouch-button-checkbox-background"
                    : "image://theme/meegotouch-button-radiobutton-background-selected"
            }

            onClicked: {
                if (settings.languagePackId === model.id) {
                    appWindow.pageStack.pop()
                    return
                }

                settings.languagePackId = model.id
                settings.languagePluralId = model.pluralCode

                // Tell TDLib straight away. Nothing in *this* process reads it again until
                // the next launch, but meegramd does: it follows language_pack_id through
                // updateOption and re-requests the strings it composes banners from, so
                // notifications change language now rather than at the next restart.
                appManager.setOption("language_pack_id", model.id)

                // And deliberately do *not* reload this process's own Locale. QML1 never
                // retranslates - every qsTr on a built page is already a plain string - so
                // swapping the pack under a running scene leaves the pages that exist in
                // the old language and gives the new one only to pages pushed afterwards.
                // Half a translated app is worse than a consistent one.
                //
                // The cache is keyed by language id (Locale::loadCache), so the next
                // launch drops it, asks for the new pack and comes up translated.
                restartDialog.open()
            }
        }
    }

    ScrollDecorator {
        flickableItem: listView
    }

    // A query dialog rather than a banner: the choice does not take effect until the app is
    // restarted, and the platform reserves this for the case where the user has to act for
    // something to happen. Closing is offered rather than done, because a settings page is
    // not a place anybody expects to lose what they were doing.
    QueryDialog {
        id: restartDialog

        // Deliberately not qsTr: Telegram has no key for this, and the language pack that
        // would translate it is the *old* one anyway - which is the right language to say
        // this in, since it is the one still on screen.
        titleText: "Language changed"
        message: "MeeGram shows the new language after it restarts. Notifications change now."
        acceptButtonText: "Close now"
        rejectButtonText: "Later"

        onAccepted: Qt.quit()
        onRejected: appWindow.pageStack.pop()
    }

    tools: ToolBarLayout {
        ToolIcon {
            platformIconId: "toolbar-back"
            onClicked: appWindow.pageStack.pop()
        }
    }
}
