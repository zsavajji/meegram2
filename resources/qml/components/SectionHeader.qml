import QtQuick 1.1
import com.nokia.meego 1.1
import "UIConstants.js" as UI

// A group header for a settings list: a small caption sitting on a hairline.
//
// Deliberately not the platform's `image://theme/meegotouch-groupheader` band. The N9 UX
// guidelines call for a thin rule rather than a grey band, and on a page that already
// carries a TopBar a second full-width band reads as a second toolbar.
//
// Caption grey rather than foreground: this is a label about the rows, not a row. It is
// the same colour and the same 0.5-opacity hairline the profile page's field groups use,
// so the two look like one app.
Item {
    property alias text: label.text

    // The Column this sits in has no width of its own until its parent lays out, so take
    // it from the parent the way every other row on the page does.
    width: parent ? parent.width : 0
    height: label.paintedHeight + UI.PADDING_DOUBLE * 2 + 8

    Label {
        id: label

        anchors {
            left: parent.left
            leftMargin: 12
            right: parent.right
            rightMargin: 12
            bottom: rule.top
            bottomMargin: 6
        }

        elide: Text.ElideRight
        font.pixelSize: UI.FONT_LSMALL
        color: appWindow.secondaryColor
    }

    Rectangle {
        id: rule

        anchors {
            left: parent.left
            right: parent.right
            bottom: parent.bottom
        }

        height: 1
        opacity: 0.5
        color: appWindow.separatorColor
    }
}
