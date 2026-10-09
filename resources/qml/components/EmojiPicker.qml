import QtQuick 1.1
import com.nokia.meego 1.1
import MyComponent 1.0

// The emoji grid that takes the keyboard's place under the composer. Knows nothing
// about where what it picks ends up - the caller owns the text field, and owns
// lowering the keyboard before setting `open`, since only it knows which field.
Rectangle {
    id: root

    property bool open: false

    signal picked(string unicode)

    // -1 until first opened, so the categories are not built during construction -
    // every chat page would pay for a panel most never show.
    property int currentTab: -1

    height: 260
    // Column skips invisible children entirely, so a closed panel costs no layout -
    // and the grid never has to lay itself out at zero height.
    visible: open
    clip: true
    color: appWindow.panelColor

    onOpenChanged: {
        if (open && currentTab === -1)
            showTab(0);
    }

    function showTab(index) {
        currentTab = index;

        var tab = tabs.categories[index];

        grid.show(tab.first, tab.last);
        // A new tab starts at its top, the way a GridView handed a new model did.
        gridFlick.contentY = 0;
    }

    Row {
        id: tabs

        width: parent.width
        height: 48

        // Inclusive ranges over Emoji::Category. Emotion and People share a tab, and
        // so do Objects and Symbols: the icon font has no glyph for either second
        // half, and Telegram groups them the same way. Ranges rather than a list of
        // ids because the categories are contiguous in Emoji::emojis() - a merged tab
        // is two neighbours joined.
        property variant categories: [
            { icon: icons.smile, first: 0, last: 1 },
            { icon: icons.animals, first: 2, last: 2 },
            { icon: icons.eats, first: 3, last: 3 },
            { icon: icons.car, first: 4, last: 4 },
            { icon: icons.sport, first: 5, last: 5 },
            { icon: icons.lamp, first: 6, last: 7 },
            { icon: icons.flag, first: 8, last: 8 }
        ]

        Repeater {
            model: tabs.categories

            Item {
                width: tabs.width / tabs.categories.length
                height: tabs.height

                Label {
                    anchors.centerIn: parent
                    text: modelData.icon
                    font.family: icons.fontFamily
                    font.pixelSize: 32
                    color: root.currentTab === index ? appWindow.accentColor : appWindow.iconColor
                }

                MouseArea {
                    anchors.fill: parent
                    onClicked: root.showTab(index)
                }
            }
        }
    }

    // One painted item for the whole category, in a Flickable for the platform's
    // scrolling - see EmojiGrid.cpp. It sizes itself, 48px cells, as many to a row as fit.
    Flickable {
        id: gridFlick

        anchors {
            left: parent.left
            right: parent.right
            top: tabs.bottom
            bottom: parent.bottom
        }
        contentWidth: width
        contentHeight: grid.height
        clip: true

        EmojiGrid {
            id: grid

            width: gridFlick.width
            onPicked: root.picked(unicode)
        }
    }
}
