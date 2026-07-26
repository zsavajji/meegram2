import QtQuick 1.1
import com.nokia.meego 1.1

Page {
    id: root

    // file:// URL of a photo that has finished downloading.
    property string source

    // The one page worth rotating: a landscape shot fills far more of the screen that
    // way round, and there is no layout here to disturb.
    orientationLock: PageOrientation.Automatic

    // Black rather than the theme background - anything lighter reads as a border
    // around the photo instead of as surround.
    Rectangle {
        anchors.fill: parent
        color: "black"
    }

    Flickable {
        id: flick

        anchors.fill: parent
        contentWidth: pinchArea.width
        contentHeight: pinchArea.height
        clip: true

        // Zoom resizes the Flickable's content instead of scaling the Image. Scaling
        // leaves contentWidth alone, so the pan limits stay at the unzoomed size and
        // most of a zoomed photo becomes unreachable; resizeContent keeps the two in
        // step and pins the zoom to the pinch centre. resizeContent, returnToBounds
        // and PinchArea are all QtQuick 1.1, which is what this app imports.
        PinchArea {
            id: pinchArea

            property real initialWidth
            property real initialHeight

            // 4x on an image that only has to cover a 480px screen is already well past
            // its real detail; past that it is only bigger, not clearer.
            property real maxZoom: 4.0

            width: Math.max(flick.contentWidth, flick.width)
            height: Math.max(flick.contentHeight, flick.height)

            onPinchStarted: {
                initialWidth = flick.contentWidth;
                initialHeight = flick.contentHeight;
            }

            onPinchUpdated: {
                if (initialWidth <= 0)
                    return;

                flick.contentX += pinch.previousCenter.x - pinch.center.x;
                flick.contentY += pinch.previousCenter.y - pinch.center.y;

                // Clamped against the fitted width, not the width this gesture started
                // at. pinch.minimumScale/maximumScale only bound a single gesture, so
                // repeated pinches would otherwise zoom without limit.
                var target = initialWidth * pinch.scale;
                var width = Math.max(flick.width, Math.min(flick.width * maxZoom, target));

                flick.resizeContent(width, initialHeight * (width / initialWidth), pinch.center);
            }

            onPinchFinished: flick.returnToBounds()

            Image {
                id: image

                anchors.fill: parent
                fillMode: Image.PreserveAspectFit
                asynchronous: true

                // Interpolating while a flick is in flight costs frames the SGX530 does
                // not have; the still image is what you actually look at.
                smooth: !flick.moving
                source: root.source
            }
        }
    }

    BusyIndicator {
        anchors.centerIn: parent
        running: visible
        visible: image.status === Image.Loading
        platformStyle: BusyIndicatorStyle { size: "large" }
    }

    ScrollDecorator {
        flickableItem: flick
    }

    tools: ToolBarLayout {
        ToolIcon {
            platformIconId: "toolbar-back"
            onClicked: appWindow.pageStack.pop()
        }
    }
}
