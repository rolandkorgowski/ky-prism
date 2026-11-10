pragma ComponentBehavior: Bound
import QtQuick
import Quickshell
import Quickshell.Io
import Quickshell.Wayland
import qs.Ui as Ui
import qs.Commons as Commons

Ui.BarWidget {
    id: root
    moduleName: "kylen.prism-terminal"
    implicitWidth: button.implicitWidth
    implicitHeight: button.implicitHeight
    property string message: "Otwórz terminal Alacritty"
    property bool animating: false
    property bool launched: false
    property var targetScreen: null
    function launch() {
        if (launched) return
        launched = true
        process.command = ["timeout", "8", Quickshell.env("HOME") + "/.local/bin/omarchy-prism-terminal"]
        process.running = true
    }
    function finish() {
        // Also launch if the display disappears before animation completes.
        launch()
        animating = false
    }
    Ui.BarIconButton {
        id: button
        bar: root.bar
        text: ""
        tooltipText: root.message
        active: root.animating || process.running
        Accessible.name: "Otwórz terminal Alacritty"
        onPressed: function(mouseButton) {
            if (mouseButton !== Qt.LeftButton || root.animating || process.running) return
            root.launched = false
            root.targetScreen = button.QsWindow.window ? button.QsWindow.window.screen : null
            if (root.setting("animate", true) !== false && root.targetScreen) root.animating = true
            else root.launch()
        }
    }
    Loader {
        active: root.animating
        sourceComponent: Component {
            PanelWindow {
                id: overlay
                screen: root.targetScreen
                visible: true
                color: "transparent"
                anchors { top: true; bottom: true; left: true; right: true }
                exclusionMode: ExclusionMode.Ignore
                WlrLayershell.namespace: "kylen-terminal-matrix"
                WlrLayershell.layer: WlrLayer.Overlay
                WlrLayershell.keyboardFocus: WlrKeyboardFocus.None
                mask: Region {}
                property real progress: 0
                onScreenChanged: { if (!screen && root.animating) root.finish() }
                WarpFlight {
                    anchors.fill: parent
                    progress: overlay.progress
                    paper: Commons.Color.background
                }
                NumberAnimation {
                    id: sequence
                    target: overlay; property: "progress"; from: 0; to: 1; duration: 1450
                    easing.type: Easing.Linear
                    onFinished: root.finish()
                }
                Component.onCompleted: sequence.start()
            }
        }
        onStatusChanged: { if (status === Loader.Error) root.finish() }
    }
    // Bounded recovery; unloaded with the widget, never active while idle.
    Timer { interval: 2100; running: root.animating; onTriggered: root.finish() }
    Process {
        id: process
        stderr: StdioCollector { id: errors; waitForEnd: true }
        onExited: function(code) {
            root.message = code === 0 ? "Otwórz terminal Alacritty" : "Alacritty: " + errors.text.trim().slice(0, 300)
        }
    }
}
