import QtQuick 2.0
import Sailfish.Silica 1.0

// The top of the stack, big enough to read at a glance: the panel itself is
// too wide for a cover, and at this size its font is a blur
CoverBackground {
    CoverPlaceholder {
        visible: calculator.depth === 0
        text: "r8 One"
        icon.source: "/usr/share/icons/hicolor/86x86/apps/harbour-r8-one.png"
    }

    Label {
        id: level
        anchors {
            top: parent.top
            left: parent.left
            right: parent.right
            margins: Theme.paddingLarge
        }
        horizontalAlignment: Text.AlignRight
        color: Theme.secondaryColor
        font.pixelSize: Theme.fontSizeSmall
        truncationMode: TruncationMode.Fade
        // The top is the one shown; this says how many there are in all
        text: calculator.depth > 1 ? calculator.depth + " on stack" : ""
    }

    Label {
        visible: calculator.depth > 0
        anchors {
            top: level.bottom
            left: parent.left
            right: parent.right
            bottom: parent.bottom
            margins: Theme.paddingLarge
        }
        text: calculator.top
        color: Theme.primaryColor
        horizontalAlignment: Text.AlignRight
        verticalAlignment: Text.AlignVCenter
        // A long float, a complex or an array wraps and shrinks to fit
        // rather than being cut off: there is no end of a number safe to drop
        wrapMode: Text.WrapAnywhere
        fontSizeMode: Text.Fit
        font.pixelSize: Theme.fontSizeHuge
        minimumPixelSize: Theme.fontSizeExtraSmall
    }
}
