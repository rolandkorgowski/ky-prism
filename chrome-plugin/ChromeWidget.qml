pragma ComponentBehavior: Bound
import QtQuick
import Quickshell
import Quickshell.Io
import Quickshell.Wayland
import qs.Ui as Ui
import qs.Commons as Commons

Ui.BarWidget {
    id: root
    moduleName: "kylen.prism-chrome"
    implicitWidth: button.implicitWidth
    implicitHeight: button.implicitHeight
    property bool animating: false
    property var targetScreen: null
    property string message: "Google Chrome"
    function launch() {
        process.command = ["timeout", "8", Quickshell.env("HOME") + "/.local/bin/omarchy-prism-chrome"]
        process.running = true
    }
    Ui.BarIconButton {
        id: button
        bar: root.bar
        active: root.animating || process.running
        tooltipText: root.message
        iconComponent: Component { ChromeMark { ink: Commons.Color.foreground } }
        onPressed: function(mouseButton) {
            if (mouseButton !== Qt.LeftButton || root.animating || process.running) return
            root.targetScreen = button.QsWindow.window ? button.QsWindow.window.screen : null
            root.launch()
            if (root.setting("animate", true) !== false) root.animating = true
        }
        Accessible.name: "Otwórz Google Chrome"
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
                WlrLayershell.namespace: "kylen-chrome-spin"
                WlrLayershell.layer: WlrLayer.Overlay
                WlrLayershell.keyboardFocus: WlrKeyboardFocus.None
                mask: Region {}
                property real progress: 0
                ChromeMark {
                    anchors.centerIn: parent
                    width: Math.min(parent.width * 0.3, parent.height * 0.42, 420)
                    height: width
                    colored: true
                    rotation: overlay.progress * 540 - 90
                    opacity: Math.min(1, overlay.progress * 8, (1 - overlay.progress) * 5)
                    scale: 0.65 + overlay.progress * 0.45
                }
                NumberAnimation {
                    id: sequence
                    target: overlay; property: "progress"; from: 0; to: 1; duration: 660
                    easing.type: Easing.InOutCubic
                    onFinished: root.animating = false
                }
                Component.onCompleted: sequence.start()
            }
        }
    }
    Process {
        id: process
        stderr: StdioCollector { id: errorOutput; waitForEnd: true }
        onExited: function(code) {
            root.message = code === 0 ? "Google Chrome" : "Chrome: " + errorOutput.text.trim().slice(0, 300)
        }
    }
}
