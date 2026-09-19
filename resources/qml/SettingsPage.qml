import QtQuick 1.1
import com.nokia.meego 1.1
import MyComponent 1.0
import "components"

Page {
    id: root

    orientationLock: PageOrientation.LockPortrait

    TopBar {
        id: header
        title: appWindow.tr("SETTINGS")
    }

    // Three rows, so no ListView and no ScrollDecorator: they cannot fill a portrait
    // screen between them, and a model of heterogeneous rows needs a delegate that
    // switches on the index - which is how this page grew a switch statement for one
    // entry.
    Column {
        anchors {
            left: parent.left
            right: parent.right
            top: header.bottom
        }

        DrillDownDelegate {
            text: qsTr("Language")
            onClicked: appWindow.pageStack.push(Qt.createComponent("LanguageSettingsPage.qml"))
        }

        // Writes the setting and nothing else: main.qml applies it to theme.inverted here
        // and on every start, and Settings persists it. The whole row is the target -
        // ListItem's own MouseArea sits above its children, so the switch below never sees
        // a press of its own and stays a plain binding on the setting.
        ListItem {
            onClicked: settings.invertedTheme = !settings.invertedTheme

            Label {
                anchors {
                    left: parent.left
                    leftMargin: 12
                    right: themeSwitch.left
                    rightMargin: 12
                    verticalCenter: parent.verticalCenter
                }
                elide: Text.ElideRight
                font.pixelSize: 26
                font.bold: true
                text: qsTr("SwitchThemeToNight")
            }

            Switch {
                id: themeSwitch

                anchors {
                    right: parent.right
                    rightMargin: 16
                    verticalCenter: parent.verticalCenter
                }
                checked: settings.invertedTheme
            }
        }

        // Bubbles, or the flat layout - see MessageBubble. Same shape as the row above:
        // it writes the setting, and everything that draws a message binds to it.
        ListItem {
            onClicked: settings.showBubbles = !settings.showBubbles

            Label {
                anchors {
                    left: parent.left
                    leftMargin: 12
                    right: bubblesSwitch.left
                    rightMargin: 12
                    verticalCenter: parent.verticalCenter
                }
                elide: Text.ElideRight
                font.pixelSize: 26
                font.bold: true
                // Not qsTr: Telegram's language pack has no key for this - it is not a
                // setting Telegram has - and an absent key renders as the key itself.
                // English until there is somewhere to translate it.
                text: "Show bubbles"
            }

            Switch {
                id: bubblesSwitch

                anchors {
                    right: parent.right
                    rightMargin: 16
                    verticalCenter: parent.verticalCenter
                }
                checked: settings.showBubbles
            }
        }
    }

    tools: ToolBarLayout {
        ToolIcon {
            platformIconId: "toolbar-back"
            onClicked: appWindow.pageStack.pop()
        }
    }
}
