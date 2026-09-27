import QtQuick 2.0
import Sailfish.Silica 1.0

// The top of the stack, big enough to read at a glance: the panel itself is
// too wide for a cover, and at this size its font is a blur
CoverBackground {
    id: cover

    CoverPlaceholder {
        visible: calculator.depth === 0
        text: "r8 One"
        icon.source: "/usr/share/icons/hicolor/86x86/apps/harbour-r8-one.png"
    }

    CoverActionList {
        enabled: calculator.depth > 0

        CoverAction {
            iconSource: "image://theme/icon-s-clipboard"
            onTriggered: Clipboard.text = calculator.stack[0]
        }
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
        // Only the top few are shown; this says how many there are in all
        text: calculator.depth > calculator.stack.length ? calculator.depth + " on stack" : ""
    }

    // Between the count and the copy action, which is there whenever this is.
    // The home screen draws the action over the cover scaled down in the
    // switcher, min(itemSizeSmall, width / 2) tall at that size: at the
    // cover's own size that is up to half its width, and coverActionArea does
    // not know it.
    Item {
        id: rows
        visible: calculator.depth > 0
        anchors {
            top: level.bottom
            left: parent.left
            right: parent.right
            bottom: parent.bottom
            leftMargin: Theme.paddingLarge
            rightMargin: Theme.paddingLarge
            topMargin: Theme.paddingSmall
            bottomMargin: cover.width / 2 + Theme.paddingSmall
        }

        // As the panel has them: level 1 at the bottom, nearest the input line
        Column {
            id: column
            width: parent.width
            anchors.bottom: parent.bottom
            spacing: Theme.paddingSmall

            Repeater {
                model: calculator.stack.length

                Item {
                    readonly property int levelNumber: calculator.stack.length - index
                    width: parent.width
                    height: Math.min(Theme.fontSizeLarge, (rows.height - 3 * column.spacing) / 4)

                    Label {
                        id: number
                        anchors.verticalCenter: parent.verticalCenter
                        text: levelNumber + ":"
                        color: Theme.secondaryColor
                        font.pixelSize: Theme.fontSizeExtraSmall
                    }

                    Label {
                        anchors {
                            left: number.right
                            right: parent.right
                            leftMargin: Theme.paddingSmall
                            verticalCenter: parent.verticalCenter
                        }
                        height: parent.height
                        text: calculator.stack[levelNumber - 1]
                        color: levelNumber === 1 ? Theme.primaryColor : Theme.secondaryColor
                        horizontalAlignment: Text.AlignRight
                        verticalAlignment: Text.AlignVCenter
                        // A long float, a complex or an array shrinks to fit the
                        // row, then fades: there is no end of a number safe to drop
                        fontSizeMode: Text.Fit
                        font.pixelSize: Theme.fontSizeLarge
                        minimumPixelSize: Theme.fontSizeTiny
                        truncationMode: TruncationMode.Fade
                    }
                }
            }
        }
    }
}
