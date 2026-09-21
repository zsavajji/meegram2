import QtQuick 1.1
import com.nokia.meego 1.1
import com.nokia.extras 1.1
import MyComponent 1.0
import "components"

// The signed-in user's own profile. Everything here writes straight to Telegram: there is
// no local copy and no Save button, which is what the platform asks of a settings page -
// a field commits when it loses focus, a picker when it is accepted, and Back just leaves.
//
// Nothing is written optimistically either. A field shows what the account says, the change
// goes out, and the field only moves when TDLib says it took - so a rejected username snaps
// back to the old one with the reason in a banner, rather than showing a name that is not
// really yours.
Page {
    id: root

    orientationLock: PageOrientation.LockPortrait

    property variant account: appManager.account

    TopBar {
        id: header
        title: qsTr("Account")
    }

    // A Flickable because the software keyboard takes half the screen: without it the
    // field being edited can end up under the panel with no way to scroll to it.
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

            SectionHeader { text: qsTr("Profile") }

            Item {
                width: parent.width
                height: fields.height + 24

                Column {
                    id: fields

                    anchors {
                        left: parent.left
                        right: parent.right
                        top: parent.top
                        leftMargin: 12
                        rightMargin: 12
                        topMargin: 12
                    }

                    spacing: 12

                    Label {
                        text: qsTr("FirstName")
                        font.pixelSize: 20
                        color: appWindow.secondaryColor
                    }

                    TextField {
                        id: firstNameField
                        width: parent.width
                        maximumLength: 64
                        // Not a binding: a binding would fight the user's typing every time
                        // an unrelated update touched the account. Seeded once, and
                        // re-seeded by the Connections below when the server answers.
                        Component.onCompleted: text = root.account.firstName
                        onActiveFocusChanged: if (!activeFocus) internal.commitName()
                    }

                    Label {
                        text: qsTr("LastName")
                        font.pixelSize: 20
                        color: appWindow.secondaryColor
                    }

                    TextField {
                        id: lastNameField
                        width: parent.width
                        maximumLength: 64
                        Component.onCompleted: text = root.account.lastName
                        onActiveFocusChanged: if (!activeFocus) internal.commitName()
                    }

                    Label {
                        // 70 characters is Telegram's own limit for this field.
                        text: qsTr("UserBio")
                        font.pixelSize: 20
                        color: appWindow.secondaryColor
                    }

                    TextArea {
                        id: bioField
                        width: parent.width
                        height: Math.max(80, implicitHeight)
                        maximumLength: 70
                        wrapMode: TextEdit.Wrap
                        Component.onCompleted: text = root.account.bio
                        onActiveFocusChanged: if (!activeFocus && text !== root.account.bio) root.account.setBio(text)
                    }
                }
            }

            SectionHeader { text: qsTr("Account") }

            Item {
                width: parent.width
                height: accountFields.height + 24

                Column {
                    id: accountFields

                    anchors {
                        left: parent.left
                        right: parent.right
                        top: parent.top
                        leftMargin: 12
                        rightMargin: 12
                        topMargin: 12
                    }

                    spacing: 12

                    Label {
                        text: qsTr("Username")
                        font.pixelSize: 20
                        color: appWindow.secondaryColor
                    }

                    TextField {
                        id: usernameField
                        width: parent.width
                        maximumLength: 32
                        placeholderText: qsTr("UsernamePlaceholder")
                        // The "@" is decoration: TDLib takes and returns the bare name, and
                        // typing one in would be rejected as an invalid character.
                        inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
                        Component.onCompleted: text = root.account.username
                        onActiveFocusChanged: if (!activeFocus && text !== root.account.username) root.account.setUsername(text)
                    }
                }
            }

            // A date is a picker, never a typed field - the platform has a dialog for it and
            // the guidelines are explicit about not asking anyone to type one.
            ListItem {
                onClicked: internal.openBirthdatePicker()

                Label {
                    anchors {
                        left: parent.left
                        leftMargin: 12
                        right: birthdateValue.left
                        rightMargin: 12
                        verticalCenter: parent.verticalCenter
                    }
                    elide: Text.ElideRight
                    font.pixelSize: 26
                    font.bold: true
                    text: qsTr("Birthday")
                }

                Label {
                    id: birthdateValue

                    anchors {
                        right: parent.right
                        rightMargin: 16
                        verticalCenter: parent.verticalCenter
                    }
                    font.pixelSize: 22
                    color: appWindow.secondaryColor
                    // An empty birthdate reads as an invitation rather than as a blank.
                    text: root.account.birthdateText !== "" ? root.account.birthdateText : qsTr("Set")
                }
            }

            // Read-only, and it says so when tapped rather than looking dead. Changing the
            // number is a code flow of its own - sendPhoneNumberCode, then a code screen,
            // then checkPhoneNumberCode - and it is deliberately not built.
            // ponytail: shows the number, does not change it; the upgrade is a second page
            // modelled on CodeEnterPage.
            ListItem {
                // Not qsTr: the pack has no key for this, and an absent key renders as the
                // key itself.
                onClicked: appWindow.showInfoBanner("Not possible on this device, sorry!")

                Label {
                    anchors {
                        left: parent.left
                        leftMargin: 12
                        right: phoneValue.left
                        rightMargin: 12
                        verticalCenter: parent.verticalCenter
                    }
                    elide: Text.ElideRight
                    font.pixelSize: 26
                    font.bold: true
                    text: qsTr("PhoneNumber")
                }

                Label {
                    id: phoneValue

                    anchors {
                        right: parent.right
                        rightMargin: 16
                        verticalCenter: parent.verticalCenter
                    }
                    font.pixelSize: 22
                    color: appWindow.secondaryColor
                    text: root.account.phoneNumber !== "" ? "+" + root.account.phoneNumber : ""
                }
            }

            SectionHeader { text: qsTr("LogOut") }

            // An action, not a setting, so a plain row rather than a switch - and the only
            // row here that does not take effect the moment it is tapped. Irreversible and
            // it costs the session on this device, which is the case the N9 guidelines
            // reserve a query dialog for.
            ListItem {
                onClicked: logOutDialog.open()

                Label {
                    anchors {
                        left: parent.left
                        leftMargin: 12
                        right: parent.right
                        rightMargin: 12
                        verticalCenter: parent.verticalCenter
                    }
                    elide: Text.ElideRight
                    font.pixelSize: 26
                    font.bold: true
                    text: qsTr("LogOut")
                }
            }
        }
    }

    ScrollDecorator { flickableItem: flickable }

    QueryDialog {
        id: logOutDialog

        titleText: qsTr("LogOut")
        message: qsTr("AreYouSureLogout")
        // The verb on the button that does it, which is what the platform asks for - not
        // "OK", which says nothing about which way round the two buttons are.
        acceptButtonText: qsTr("LogOut")
        rejectButtonText: qsTr("Cancel")

        // Everything happens in C++: the avatar cache goes, TDLib destroys its database and
        // closes, and AppManager turns the resulting authorization state into signedOut,
        // which is what swaps MainPage back to the welcome screen.
        onAccepted: appManager.logOut()
    }

    DatePickerDialog {
        id: birthdatePicker

        titleText: qsTr("Birthday")
        acceptButtonText: qsTr("Set")
        rejectButtonText: qsTr("Cancel")

        // Year is kept: the picker has no way to say "no year", so an account that had one
        // without keeps whatever the dialog opened on. Clearing it altogether is the menu
        // item below, which is the only way to get back to no birthdate at all.
        onAccepted: root.account.setBirthdate(day, month, year)
    }

    // Re-seeds the fields whenever the server answers, which is also what makes a rejected
    // change snap back: nothing here moved when the request went out, so the field is still
    // showing the old value and this puts the authoritative one in either way.
    //
    // Skips whichever field has focus, so an answer arriving mid-sentence does not rewrite
    // what is being typed.
    Connections {
        target: root.account

        onChanged: {
            if (!firstNameField.activeFocus) firstNameField.text = root.account.firstName
            if (!lastNameField.activeFocus) lastNameField.text = root.account.lastName
            if (!usernameField.activeFocus) usernameField.text = root.account.username
        }

        onFullInfoChanged: {
            if (!bioField.activeFocus) bioField.text = root.account.bio
        }

        onFailed: appWindow.showInfoBanner(message)
    }

    Menu {
        id: menu

        MenuLayout {
            MenuItem {
                // Not qsTr: the pack has no key for this, and an absent key renders as the
                // key itself. English, like the bubble switches in Settings.
                text: "Remove birthday"
                visible: root.account.birthdateDay !== 0
                onClicked: root.account.clearBirthdate()
            }
        }
    }

    tools: ToolBarLayout {
        ToolIcon {
            platformIconId: "toolbar-back"
            onClicked: appWindow.pageStack.pop()
        }

        ToolIcon {
            platformIconId: "toolbar-view-menu"
            onClicked: menu.open()
        }
    }

    // The full-info half - bio and birthdate - is only as current as the last time anybody
    // asked, so ask on the way in. The `user` half needs nothing: it is live in the store.
    Component.onCompleted: root.account.load()

    QtObject {
        id: internal

        // Both names travel in one request, so this is one function rather than two that
        // would each undo the other's half.
        function commitName() {
            if (firstNameField.text === root.account.firstName && lastNameField.text === root.account.lastName)
                return;

            // Telegram requires a first name; an empty one is rejected, so it is worth
            // saying so here rather than letting the server answer with FIRSTNAME_INVALID.
            if (firstNameField.text === "") {
                firstNameField.text = root.account.firstName;
                return;
            }

            root.account.setName(firstNameField.text, lastNameField.text);
        }

        function openBirthdatePicker() {
            // An account with no birthdate opens the picker on something plausible rather
            // than on the epoch: day and month are what Telegram actually asks for.
            birthdatePicker.day = root.account.birthdateDay !== 0 ? root.account.birthdateDay : 1;
            birthdatePicker.month = root.account.birthdateMonth !== 0 ? root.account.birthdateMonth : 1;
            birthdatePicker.year = root.account.birthdateYear !== 0 ? root.account.birthdateYear : 1990;

            birthdatePicker.open();
        }
    }
}
