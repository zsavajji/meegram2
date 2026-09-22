import QtQuick 1.1
import com.nokia.meego 1.1

// A settings row that shows what something is set to: label on the left, value on the
// right in caption grey. The value is the affordance - a row with something in it reads as
// something you can change - so there is no drill-down arrow, which is what separates this
// from DrillDownDelegate next to it.
//
// `placeholder` is what an empty value shows instead: "Set" reads as an invitation where a
// blank reads as broken.
ListItem {
    id: root

    property alias text: label.text
    property string value: ""
    property string placeholder: ""

    Label {
        id: label

        anchors {
            left: parent.left
            leftMargin: 12
            right: valueLabel.left
            rightMargin: 12
            verticalCenter: parent.verticalCenter
        }

        elide: Text.ElideRight
        font.pixelSize: 26
        font.bold: true
    }

    Label {
        id: valueLabel

        anchors {
            right: parent.right
            rightMargin: 16
            verticalCenter: parent.verticalCenter
        }

        // Capped so a long value cannot squeeze the label it belongs to off the row; the
        // label elides too, so both give way rather than one winning.
        width: Math.min(implicitWidth, root.width * 0.55)

        elide: Text.ElideRight
        horizontalAlignment: Text.AlignRight
        font.pixelSize: 22
        color: appWindow.secondaryColor

        text: root.value !== "" ? root.value : root.placeholder
    }
}
