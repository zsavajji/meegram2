import QtQuick 1.1
import com.nokia.meego 1.1
import com.nokia.extras 1.1
import MyComponent 1.0
import "components"

// The signed-in user's own profile. The page *shows*; editing happens in a sheet with a
// Save button, which is what the platform reserves for a form - a settings page saves live
// and has no button, a composer has Cancel and a verb and no Back. This is the second: the
// fields go to Telegram, and a half-typed name is not something to send on a focus change.
//
// Nothing is written optimistically. Save sends the change and the row still shows what the
// account says; it moves when TDLib answers, so a rejected username leaves the old one on
// screen with the reason in a banner rather than a name that is not really yours.
Page {
    id: root

    orientationLock: PageOrientation.LockPortrait

    property variant account: appManager.account

    // Telegram's own limit for a bio. Enforced by hand because TextArea has no
    // maximumLength - that is TextField, which wraps a TextInput; this wraps a TextEdit.
    // Assigning it is not a warning, it stops the page loading.
    property int maxBioLength: 70

    TopBar {
        id: header
        title: qsTr("Account")
    }

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

            ValueDelegate {
                text: qsTr("FirstName")
                // Both names in one row: TDLib takes them in one request, and they are one
                // thing to everybody except the API.
                value: (root.account.firstName + " " + root.account.lastName).replace(/^ +| +$/g, "")
                placeholder: qsTr("Set")

                onClicked: internal.edit("name")
            }

            ValueDelegate {
                text: qsTr("UserBio")
                value: root.account.bio
                placeholder: qsTr("Set")

                onClicked: internal.edit("bio")
            }

            SectionHeader { text: qsTr("Account") }

            ValueDelegate {
                text: qsTr("Username")
                // The "@" is decoration for display: TDLib takes and returns the bare name.
                value: root.account.username !== "" ? "@" + root.account.username : ""
                placeholder: qsTr("Set")

                onClicked: internal.edit("username")
            }

            // A date is a picker, never a typed field - the platform has a dialog for it and
            // the guidelines are explicit about not asking anyone to type one.
            ValueDelegate {
                text: qsTr("Birthday")
                value: root.account.birthdateText
                placeholder: qsTr("Set")

                onClicked: internal.openBirthdatePicker()
            }

            // Read-only, and it says so when tapped rather than looking dead. Changing the
            // number is a code flow of its own - sendPhoneNumberCode, then a code screen,
            // then checkPhoneNumberCode - and it is deliberately not built.
            // ponytail: shows the number, does not change it; the upgrade is a second page
            // modelled on CodeEnterPage.
            ValueDelegate {
                text: qsTr("PhoneNumber")
                value: root.account.phoneNumber !== "" ? "+" + root.account.phoneNumber : ""

                // Not qsTr: the pack has no key for this, and an absent key renders as the
                // key itself.
                onClicked: appWindow.showInfoBanner("Not possible on this device, sorry!")
            }

            SectionHeader { text: qsTr("LogOut") }

            // An action, not a setting, so a plain row - and irreversible, which is the case
            // the N9 guidelines reserve a query dialog for.
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

    // One sheet for all three fields rather than three sheets: they differ by which inputs
    // are shown and what Save sends, and everything else - the bar, the buttons, the
    // keyboard handling - is identical. `internal.editing` says which.
    Sheet {
        id: editSheet

        acceptButtonText: qsTr("Save")
        rejectButtonText: qsTr("Cancel")

        title: Label {
            anchors.centerIn: parent
            font.pixelSize: 28
            color: "white"
            text: internal.editing === "name" ? qsTr("FirstName")
                : internal.editing === "bio" ? qsTr("UserBio")
                                             : qsTr("Username")
        }

        content: Item {
            anchors.fill: parent

            Column {
                anchors {
                    left: parent.left
                    right: parent.right
                    top: parent.top
                    margins: 16
                }

                spacing: 12

                // Both name fields, shown together: one request carries them, so one sheet
                // should edit them.
                Label {
                    visible: internal.editing === "name"
                    text: qsTr("FirstName")
                    font.pixelSize: 20
                    color: appWindow.secondaryColor
                }

                TextField {
                    id: firstNameField

                    visible: internal.editing === "name"
                    width: parent.width
                    maximumLength: 64
                }

                Label {
                    visible: internal.editing === "name"
                    text: qsTr("LastName")
                    font.pixelSize: 20
                    color: appWindow.secondaryColor
                }

                TextField {
                    id: lastNameField

                    visible: internal.editing === "name"
                    width: parent.width
                    maximumLength: 64
                }

                TextArea {
                    id: bioField

                    visible: internal.editing === "bio"
                    width: parent.width
                    height: 140
                    wrapMode: TextEdit.Wrap

                    onTextChanged: if (text.length > root.maxBioLength) text = text.substring(0, root.maxBioLength)
                }

                Label {
                    visible: internal.editing === "bio"
                    width: parent.width
                    horizontalAlignment: Text.AlignRight
                    font.pixelSize: 20
                    color: appWindow.secondaryColor
                    // Counts down rather than up: the limit is the thing worth knowing.
                    text: (root.maxBioLength - bioField.text.length) + ""
                }

                TextField {
                    id: usernameField

                    visible: internal.editing === "username"
                    width: parent.width
                    maximumLength: 32
                    placeholderText: qsTr("Username")

                    // Exactly as typed. ImhNoAutoUppercase says "do not capitalise for me"
                    // and ImhPreferLowercase asks the keyboard to *open* unshifted rather
                    // than in Abc mode, which is what was putting a capital on the first
                    // letter. Neither restricts anything: shift still works and a typed
                    // capital survives.
                    //
                    // Deliberately **not** ImhLowercaseOnly, which forces the case and is a
                    // different thing from not changing it.
                    inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText | Qt.ImhPreferLowercase
                }

                Label {
                    visible: internal.editing === "username"
                    width: parent.width
                    wrapMode: Text.WordWrap
                    font.pixelSize: 20
                    color: appWindow.secondaryColor
                    // Not qsTr: no pack key says this, and an absent key renders as itself.
                    text: "Letters, numbers and underscores. Leave it empty to remove it."
                }
            }
        }

        onAccepted: internal.save()
    }

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

        // The defaults are currentYear - 1 to currentYear + 20, which is a range for
        // booking things rather than for a birthday: without these, a year set below is
        // clamped forward and the picker cannot reach anybody's.
        minimumYear: 1900
        // Plain JS: the dialog's own dateTime helper is internal to it.
        maximumYear: new Date().getFullYear()

        // Year is kept: the picker has no way to say "no year", so an account that had one
        // without keeps whatever the dialog opened on. Clearing it altogether is the menu
        // item, which is the only way back to no birthdate at all.
        onAccepted: root.account.setBirthdate(day, month, year)
    }

    // The rows are bound straight to the account, so they follow a change on their own.
    // This is here for the failures: a rejected request leaves the row showing the old
    // value, which is correct, and this says why.
    Connections {
        target: root.account

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

        // Which field the sheet is editing: "name", "bio" or "username".
        property string editing: ""

        function edit(what) {
            editing = what;

            // Seeded on the way in, every time. The fields are deliberately not bound to
            // the account - a binding would rewrite what is being typed the moment an
            // unrelated update touched it - so this is what puts the current value in.
            if (what === "name") {
                firstNameField.text = root.account.firstName;
                lastNameField.text = root.account.lastName;
            } else if (what === "bio") {
                bioField.text = root.account.bio;
            } else {
                usernameField.text = root.account.username;
            }

            editSheet.open();
        }

        function save() {
            if (editing === "name") {
                // Telegram requires a first name and rejects an empty one, so it is worth
                // saying so here rather than letting the server answer FIRSTNAME_INVALID.
                if (firstNameField.text === "") {
                    // Not qsTr: no pack key says this.
                    appWindow.showInfoBanner("A first name is required.");
                    return;
                }

                if (firstNameField.text !== root.account.firstName || lastNameField.text !== root.account.lastName)
                    root.account.setName(firstNameField.text, lastNameField.text);
            } else if (editing === "bio") {
                if (bioField.text !== root.account.bio)
                    root.account.setBio(bioField.text);
            } else {
                if (usernameField.text !== root.account.username)
                    root.account.setUsername(usernameField.text);
            }
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
