import QtQuick 1.1
import com.nokia.meego 1.1

Page {
    id: root

    // file:// URL of a photo that has finished downloading.
    property string source

    // The full-size File behind that photo, or null when the caller has nothing more than
    // what it passed as source - which is also what hides Save. The bubble only ever
    // downloaded the size that covers the screen, so this usually still has to be fetched;
    // source is what fills the page in the meantime.
    property QtObject original: null

    property bool fullSizeReady: original !== null && original.isDownloadingCompleted

    // Fetched on opening, not on saving: this is the page where the difference between
    // 480px and the real photo is the entire point.
    //
    // From both, because this is imperative rather than a binding and the order is not
    // ours to pick: pageStack.push assigns the properties around createObject, and which
    // side of completion they land on is its business, not this page's.
    Component.onCompleted: fetchOriginal()
    onOriginalChanged: fetchOriginal()

    function fetchOriginal() {
        if (original && original.canBeDownloaded && !original.isDownloadingActive && !original.isDownloadingCompleted)
            appManager.downloadFile(original.id, 1, 0, 0, false);
    }

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
                // Nothing but the backdrop once the real photo has decoded. Two
                // full-screen images drawn on top of each other is overdraw this GPU
                // notices, and they cover exactly the same rectangle.
                visible: !fullImage.visible
            }

            Image {
                id: fullImage

                anchors.fill: parent
                fillMode: Image.PreserveAspectFit
                asynchronous: true
                smooth: !flick.moving

                // ponytail: decoded at its natural size, which is the point - but a 1280px
                // photo is about 6MB as a pixmap and a 2560px one four times that. If a
                // huge one ever kills the app, sourceSize.width is the clamp.
                source: root.fullSizeReady ? "file://" + root.original.localPath : ""

                // Shown on decode, not on assignment: swapping the moment the file lands
                // leaves the page blank for as long as the decode takes.
                visible: status === Image.Ready
            }
        }
    }

    BusyIndicator {
        anchors.centerIn: parent
        running: visible
        // Also while the full size is on its way: the preview is already on screen by
        // then, so without this the page looks finished for the whole download.
        visible: !fullImage.visible && (image.status === Image.Loading || root.original !== null)
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

        // A button rather than a ToolIcon: Harmattan has no save glyph in its toolbar
        // set, and a wrong icon says less than the word does. The download it may have
        // to wait for is appWindow's problem, and its banner reports the result - this
        // page stays open either way.
        ToolButton {
            text: qsTr("Save")
            visible: root.original !== null
            onClicked: appWindow.saveOriginal(root.original, "")
        }
    }
}
