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

    // A Column inside a Flickable rather than a ListView: the rows are heterogeneous -
    // switches, drill-downs, a row that shows a value - so a model would need a delegate
    // that switches on the index, which is how this page once grew a switch statement for
    // a single entry. Sections are SectionHeader items in the same Column for the same
    // reason.
    //
    // The Flickable is not optional. These rows are about 1400px against a portrait
    // content area of 819, and without it everything below the Storage section is simply
    // unreachable - which is what happened when the sections went in and this comment
    // still claimed the page could not fill a screen.
    Flickable {
        id: flickable

        anchors {
            left: parent.left
            right: parent.right
            top: header.bottom
            bottom: parent.bottom
        }

        clip: true
        contentHeight: column.height
        boundsBehavior: Flickable.StopAtBounds

        Column {
            id: column

            width: flickable.width

            // These two are the English text as well as the language-pack key, so a pack that
            // has no key for them renders the right word anyway - which is the only reason
            // they are not marked untranslated like the bubble switches below. Worth reading
            // on device in a non-English pack before trusting that.
            SectionHeader { text: qsTr("General") }

            DrillDownDelegate {
                text: qsTr("Language")
                onClicked: appWindow.pageStack.push(Qt.createComponent("LanguageSettingsPage.qml"))
            }

            SectionHeader { text: qsTr("Appearance") }

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

            // The 2012 balloon - the nine-slice PNGs with the tail - instead of the drawn
            // shape. Off by default.
            //
            // Dead while the flat layout is on, because there is no balloon to draw either
            // way: ListItem dims itself and disables its own MouseArea from `enabled`, so the
            // row cannot be tapped and reads as unavailable rather than as off. The stored
            // value is left alone by that - turning bubbles back on returns to whichever of
            // the two was last chosen, rather than silently resetting it.
            ListItem {
                enabled: settings.showBubbles

                onClicked: settings.skeuomorphicBubbles = !settings.skeuomorphicBubbles

                Label {
                    anchors {
                        left: parent.left
                        leftMargin: 12
                        right: skeuomorphSwitch.left
                        rightMargin: 12
                        verticalCenter: parent.verticalCenter
                    }
                    elide: Text.ElideRight
                    font.pixelSize: 26
                    font.bold: true
                    // Not qsTr, for the same reason as the row above: Telegram's language
                    // pack has no key for a setting Telegram does not have, and an absent key
                    // renders as the key itself.
                    text: "Skeumorphic bubbles"
                }

                Switch {
                    id: skeuomorphSwitch

                    anchors {
                        right: parent.right
                        rightMargin: 16
                        verticalCenter: parent.verticalCenter
                    }
                    checked: settings.skeuomorphicBubbles
                }
            }

            // Animated stickers off means rlottie never runs, which on this device is a
            // performance setting as much as a visual one - see Settings::animateStickers.
            ListItem {
                onClicked: settings.animateStickers = !settings.animateStickers

                Label {
                    anchors {
                        left: parent.left
                        leftMargin: 12
                        right: animationSwitch.left
                        rightMargin: 12
                        verticalCenter: parent.verticalCenter
                    }
                    elide: Text.ElideRight
                    font.pixelSize: 26
                    font.bold: true
                    // Telegram's own key for this setting, so it arrives translated.
                    text: qsTr("AnimatedStickers")
                }

                Switch {
                    id: animationSwitch

                    anchors {
                        right: parent.right
                        rightMargin: 16
                        verticalCenter: parent.verticalCenter
                    }
                    checked: settings.animateStickers
                }
            }

            // Sensitive content is a TDLib option rather than a local setting - the server
            // enforces it and other clients see the same switch. Two options, not one:
            // can_ignore_sensitive_content_restrictions says whether this account is allowed to
            // turn it off at all, which depends on where it is and how old it says it is.
            //
            // Read once when the page is built rather than bound: StorageManager keeps options
            // in a map with no per-option notify, so a binding would never re-evaluate anyway.
            // The row drives its own value from there on, which is why it is a property here.
            ListItem {
                id: sensitiveRow

                property bool allowed: storageManager.getOption("can_ignore_sensitive_content_restrictions") === true
                property bool showing: storageManager.getOption("ignore_sensitive_content_restrictions") === true

                enabled: allowed

                onClicked: {
                    showing = !showing
                    appManager.setOption("ignore_sensitive_content_restrictions", showing)
                }

                Label {
                    anchors {
                        left: parent.left
                        leftMargin: 12
                        right: sensitiveSwitch.left
                        rightMargin: 12
                        verticalCenter: parent.verticalCenter
                    }
                    elide: Text.ElideRight
                    font.pixelSize: 26
                    font.bold: true
                    text: qsTr("ShowSensitiveContent")
                }

                Switch {
                    id: sensitiveSwitch

                    anchors {
                        right: parent.right
                        rightMargin: 16
                        verticalCenter: parent.verticalCenter
                    }
                    checked: sensitiveRow.showing
                }
            }

            SectionHeader { text: qsTr("NotificationsAndSounds") }

            // Telegram keeps three notification scopes and a phone wants groups silent far
            // more often than it wants everything silent, so they are three rows rather than
            // one. The switch reads as "notifications on", which is the opposite of the muted
            // flag underneath - hence the negation on both sides.
            //
            // LED, vibration and sound are deliberately not here: on Harmattan those come from
            // the notification event type and the device profile, and meegramd posts the
            // banners. See docs/features.md.
            Repeater {
                model: [
                    { title: qsTr("NotificationsPrivateChats"), scope: 0 },
                    { title: qsTr("NotificationsGroups"), scope: 1 },
                    { title: qsTr("NotificationsChannels"), scope: 2 }
                ]

                ListItem {
                    id: scopeRow

                    // modelData rather than model: inside a Repeater, `model` is the Repeater's
                    // own. Copied into a property so the row reads it once.
                    property variant entry: modelData
                    property bool muted: entry.scope === 0 ? appManager.privateChatsMuted
                                       : entry.scope === 1 ? appManager.groupChatsMuted
                                                           : appManager.channelChatsMuted

                    onClicked: appManager.setScopeMuted(entry.scope, !muted)

                    Label {
                        anchors {
                            left: parent.left
                            leftMargin: 12
                            right: scopeSwitch.left
                            rightMargin: 12
                            verticalCenter: parent.verticalCenter
                        }
                        elide: Text.ElideRight
                        font.pixelSize: 26
                        font.bold: true
                        text: scopeRow.entry.title
                    }

                    Switch {
                        id: scopeSwitch

                        anchors {
                            right: parent.right
                            rightMargin: 16
                            verticalCenter: parent.verticalCenter
                        }
                        checked: !scopeRow.muted
                    }
                }
            }

            // DataSettings is Telegram's own name for this section ("Dati e archivio");
            // there is no "Storage" key, and a missing key renders as itself.
            SectionHeader { text: qsTr("DataSettings") }

            // One row, not two. It used to be a size row above a clear row, and the size row
            // carried the key "LocalDatabase" - which is Telegram's label for *deleting* the
            // database, so in Italian it read "delete local database" and did nothing but
            // re-read the number. The action and the number it acts on belong together.
            //
            // What it clears is TDLib's downloaded files plus this app's avatar cache. Not
            // the databases: those are the chat list and the history, and dropping them
            // would cost a resync rather than free a cache.
            ListItem {
                // Not while one is already running: the request takes about ten seconds on
                // a full cache and a second tap would ask again mid-flight.
                onClicked: if (!appManager.clearingCache) clearCacheDialog.open()

                Label {
                    anchors {
                        left: parent.left
                        leftMargin: 12
                        right: cacheValue.left
                        rightMargin: 12
                        verticalCenter: parent.verticalCenter
                    }
                    elide: Text.ElideRight
                    font.pixelSize: 26
                    font.bold: true
                    text: qsTr("ClearCache")
                }

                // Takes the number's place while the clear runs, which on a full cache is
                // about ten seconds - long enough that a number sitting there unchanged
                // reads as a tap that did nothing.
                BusyIndicator {
                    id: clearIndicator

                    anchors {
                        right: parent.right
                        rightMargin: 16
                        verticalCenter: parent.verticalCenter
                    }

                    running: appManager.clearingCache
                    visible: running
                }

                Label {
                    id: cacheValue

                    anchors {
                        right: clearIndicator.visible ? clearIndicator.left : parent.right
                        rightMargin: 16
                        verticalCenter: parent.verticalCenter
                    }
                    visible: !clearIndicator.visible
                    font.pixelSize: 22
                    color: appWindow.secondaryColor
                    // No width binding on purpose: bound to its own implicitWidth with elide
                    // set, a Text feeds its elided layout back into that width and settles
                    // far narrower than the string. The label on the left elides instead.
                    //
                    // Empty until the answer lands, which on a cold cache is also the truth.
                    text: appManager.cacheSize
                }
            }

            SectionHeader { text: qsTr("Account") }

            DrillDownDelegate {
                text: qsTr("Account")
                onClicked: appWindow.pageStack.push(Qt.createComponent("AccountSettingsPage.qml"))
            }

            // Sessions rather than profile, so its own row rather than a section of the one
            // above: what it lists is other devices, and the only thing it does to this one is
            // nothing.
            DrillDownDelegate {
                text: qsTr("Devices")
                onClicked: appWindow.pageStack.push(Qt.createComponent("SessionsPage.qml"))
            }
        }
    }

    ScrollDecorator { flickableItem: flickable }

    QueryDialog {
        id: clearCacheDialog

        titleText: qsTr("ClearCache")
        // Not qsTr: the pack has no AreYouSureClearCache, so the key rendered on screen.
        // Says what survives, because that is the question somebody about to tap it has.
        message: "Downloaded photos, stickers and files are deleted. Your chats and messages stay."
        acceptButtonText: qsTr("ClearCache")
        rejectButtonText: qsTr("Cancel")

        onAccepted: appManager.clearCache()
    }

    // Both are one round trip and neither has an update behind it, so they are asked for
    // when the page opens rather than kept current for a page nobody is looking at.
    Component.onCompleted: {
        appManager.requestStorageStatistics()
        appManager.loadNotificationSettings()
    }

    tools: ToolBarLayout {
        ToolIcon {
            platformIconId: "toolbar-back"
            onClicked: appWindow.pageStack.pop()
        }
    }
}
