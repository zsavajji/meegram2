import QtQuick 1.1
import com.nokia.meego 1.1

// A settings row that shows what something is set to: the value on top, and underneath it
// in caption grey the name of the field it belongs to. The value is the affordance - a row
// with something in it reads as something you can change - so there is no drill-down arrow,
// which is what separates this from DrillDownDelegate next to it.
//
// Stacked rather than side by side, and that is the whole point of the shape. Value beside
// label means the two compete for one row's width, and the loser elides: a long field name
// ("Numero di telefono") against a long value is a fight neither wins, and it is worse in
// every language that is not English. Stacked, each gets the full width.
//
// `placeholder` is what an empty value shows instead: "Set" reads as an invitation where a
// blank reads as broken.
ListItem {
    id: root

    property alias text: label.text
    property string value: ""
    property string placeholder: ""

    height: content.height + 32

    Column {
        id: content

        anchors {
            left: parent.left
            right: parent.right
            leftMargin: 12
            rightMargin: 12
            verticalCenter: parent.verticalCenter
        }

        spacing: 2

        Label {
            id: valueLabel

            width: parent.width
            elide: Text.ElideRight
            font.pixelSize: 26
            font.bold: true

            text: root.value !== "" ? root.value : root.placeholder

            // Greyed when it is standing in for nothing, so "Set" does not read as a value
            // somebody chose.
            color: root.value !== "" ? (theme.inverted ? "#ffffff" : "#000000") : appWindow.secondaryColor
        }

        Label {
            id: label

            width: parent.width
            elide: Text.ElideRight
            font.pixelSize: 20
            color: appWindow.secondaryColor
        }
    }
}
