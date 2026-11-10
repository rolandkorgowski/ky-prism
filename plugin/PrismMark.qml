import QtQuick
import QtQuick.Shapes

Item {
    id: mark
    property color ink: "black"
    implicitWidth: 24
    implicitHeight: 24
    Shape {
        width: 24; height: 24; anchors.centerIn: parent
        scale: Math.min(mark.width, mark.height) / 24
        preferredRendererType: Shape.CurveRenderer
        ShapePath {
            fillColor: "transparent"; strokeColor: mark.ink; strokeWidth: 1.35
            capStyle: ShapePath.RoundCap; joinStyle: ShapePath.RoundJoin
            PathSvg { path: "M 8 4 L 5 4 Q 3 4 3 6 L 3 18 Q 3 20 5 20 L 19 20 Q 21 20 21 18 L 21 15 M 12 4 L 19 4 Q 21 4 21 6 L 21 10 M 3 16 L 8 11 L 12 15 L 15 12 L 21 18" }
        }
        ShapePath {
            fillColor: mark.ink; strokeWidth: 0
            PathSvg { path: "M 17.8 8 A 1.3 1.3 0 1 1 15.2 8 A 1.3 1.3 0 1 1 17.8 8 Z" }
        }
    }
}
