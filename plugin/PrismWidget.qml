pragma ComponentBehavior: Bound
import QtQuick
import Quickshell
import Quickshell.Io
import Quickshell.Wayland
import qs.Ui as Ui
import qs.Commons as Commons

Ui.BarWidget {
    id: root
    moduleName: "kylen.prism"
    implicitWidth: button.implicitWidth
    implicitHeight: button.implicitHeight
    property bool animating: false
    property var targetScreen: null
    property string message: "ky-prism · zdjęcia"
    function launch() {
        process.command = ["timeout", "8", Quickshell.env("HOME") + "/.local/bin/omarchy-prism-open"]
        process.running = true
    }
    Ui.BarIconButton {
        id: button
        bar: root.bar
        active: root.animating || process.running
        tooltipText: root.message
        iconComponent: Component { PrismMark { ink: Commons.Color.foreground } }
        onPressed: function(mouseButton) {
            if (mouseButton !== Qt.LeftButton || root.animating || process.running) return
            root.targetScreen = button.QsWindow.window ? button.QsWindow.window.screen : null
            root.launch()
            if (root.setting("animate", true) !== false) root.animating = true
        }
        Accessible.name: "Otwórz przeglądarkę zdjęć ky-prism"
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
                WlrLayershell.namespace: "kylen-prism-aperture"
                WlrLayershell.layer: WlrLayer.Overlay
                WlrLayershell.keyboardFocus: WlrKeyboardFocus.None
                mask: Region {}
                property real progress: 0
                Aperture {
                    anchors.centerIn: parent
                    width: Math.min(parent.width * 0.55, parent.height * 0.65, 800)
                    height: width
                    ink: Commons.Color.foreground
                    paper: Commons.Color.background
                    accent: Commons.Color.accent
                    opening: Math.min(1, overlay.progress * 1.3)
                    rotation: -18 + overlay.progress * 42
                    opacity: Math.min(1, overlay.progress * 8, (1 - overlay.progress) * 5)
                    scale: 0.82 + overlay.progress * 0.2
                }
                NumberAnimation {
                    id: sequence
                    target: overlay; property: "progress"; from: 0; to: 1; duration: 720
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
            root.message = code === 0 ? "ky-prism · zdjęcia" : "ky-prism: " + errorOutput.text.trim().slice(0, 300)
        }
    }
}
