import QtQuick 2.0
import Sailfish.Silica 1.0
import rs.r8.one 1.0

// The whole case, keypad included: it is the calculator's, not Silica's
Page {
    allowedOrientations: Orientation.Portrait
    backNavigation: false

    QdosView {
        anchors.fill: parent
        machine: calculator
        focus: true
    }
}
