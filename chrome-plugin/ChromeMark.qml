import QtQuick
import QtQuick.Shapes
Item {
    id: mark
    property color ink: "white"
    property bool colored: false
    property real ringThickness: 6.5
    implicitWidth: 24; implicitHeight: 24
    Shape {
        width: 100; height: 100; anchors.centerIn: parent
        scale: Math.min(mark.width, mark.height) / 100
        preferredRendererType: Shape.CurveRenderer
        visible: mark.colored
        ShapePath {
            strokeWidth: 0; fillColor: "#EA4335"
            PathSvg { path: "M 50 0 A 50 50 0 0 1 93.3 25 L 50 25 A 25 25 0 0 0 28.35 62.5 L 6.7 25 A 50 50 0 0 1 50 0 Z" }
        }
        ShapePath {
            strokeWidth: 0; fillColor: "#FBBC05"
            PathSvg { path: "M 93.3 25 A 50 50 0 0 1 50 100 L 71.65 62.5 A 25 25 0 0 0 50 25 Z" }
        }
        ShapePath {
            strokeWidth: 0; fillColor: "#34A853"
            PathSvg { path: "M 50 100 A 50 50 0 0 1 6.7 25 L 28.35 62.5 A 25 25 0 0 0 71.65 62.5 Z" }
        }
        ShapePath {
            strokeWidth: 3; strokeColor: mark.colored ? "white" : "transparent"
            fillColor: mark.colored ? "#4285F4" : "transparent"
            PathSvg { path: "M 73.5 50 A 23.5 23.5 0 1 1 26.5 50 A 23.5 23.5 0 1 1 73.5 50 Z" }
        }
    }
    Shape {
        width:100; height:100; anchors.centerIn:parent
        scale: Math.min(mark.width,mark.height)/100
        preferredRendererType: Shape.CurveRenderer
        visible: !mark.colored
        ShapePath {
            fillColor:"transparent";strokeColor:mark.ink;strokeWidth:mark.ringThickness
            PathSvg { path:"M 94 50 A 44 44 0 1 1 6 50 A 44 44 0 1 1 94 50 Z" }
        }
        ShapePath {
            fillColor:"transparent";strokeColor:mark.ink;strokeWidth:4.5
            PathSvg { path:"M 69.5 50 A 19.5 19.5 0 1 1 30.5 50 A 19.5 19.5 0 1 1 69.5 50 Z M 50 30.5 L 88 30.5 M 66.9 59.75 L 47.9 92.65 M 33.1 59.75 L 14.1 26.85" }
        }
    }
}
