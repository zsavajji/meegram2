import QtQuick 1.1
import com.nokia.meego 1.1

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
    color: "white"

    onOpenChanged: {
        if (open && currentTab === -1)
            showTab(0);
    }

    function showTab(index) {
        currentTab = index;

        var tab = tabs.categories[index];
        var list = [];

        for (var c = tab.first; c <= tab.last; ++c)
            list = list.concat(utils.emojiCategory(c));

        grid.model = list;
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
                    color: root.currentTab === index ? "#0077A8" : "#505050"
                }

                MouseArea {
                    anchors.fill: parent
                    onClicked: root.showTab(index)
                }
            }
        }
    }

    GridView {
        id: grid

        anchors {
            left: parent.left
            right: parent.right
            top: tabs.bottom
            bottom: parent.bottom
        }
        // Ten to a row in portrait, on 32px assets. Below the platform's touch target,
        // but a grid of full-size buttons fits twenty emoji on screen and turns picking
        // one into scrolling.
        cellWidth: 48
        cellHeight: 48
        clip: true

        delegate: Item {
            width: grid.cellWidth
            height: grid.cellHeight

            Image {
                anchors.centerIn: parent
                width: 32
                height: 32
                sourceSize.width: 32
                sourceSize.height: 32
                source: "qrc:/emoji/" + modelData.filename
                asynchronous: true
            }

            MouseArea {
                anchors.fill: parent
                onClicked: root.picked(modelData.unicode)
            }
        }
    }
}
