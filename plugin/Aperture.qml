import QtQuick
import QtQuick.Shapes

Item {
    id: iris
    property real opening: 0.22
    property color ink: "black"
    property color paper: "transparent"
    property color accent: ink
    property bool detailed: true
    readonly property int bladeCount: 7
    function point(radius, angle) {
        return [128 + radius * Math.cos(angle), 128 + radius * Math.sin(angle)]
    }
    function blade(index) {
        var step = 2 * Math.PI / bladeCount
        var a = index * step - Math.PI / 2
        if (!detailed) {
            var left = point(91, a - 0.19), right = point(91, a + 0.19)
            var tip = point(43, a + 0.26)
            return "M " + left + " L " + right + " L " + tip + " Z"
        }
        var r = 5 + 91 * opening
        var p = point(109, a), q = point(109, a + step)
        var t = point(r, a + 1.30), u = point(r, a + 0.40)
        return "M " + p + " A 109 109 0 0 1 " + q + " L " + t + " Q 128 128 " + u + " Z"
    }
    Item {
        width: 256; height: 256
        scale: Math.min(iris.width, iris.height) / 256
        anchors.centerIn: parent
        // No disk behind the bar icon: only black contours and seven triangles.
        Rectangle { anchors.fill: parent; anchors.margins: 4; radius: width / 2; color: "transparent"; border.color: iris.ink; border.width: iris.detailed ? 1 : 3 }
        Rectangle { anchors.centerIn: parent; width: 224; height: width; radius: width / 2; color: "transparent"; border.color: iris.accent; border.width: iris.detailed ? 1.5 : 2; opacity: iris.detailed ? 0.7 : 1 }
        Rectangle { visible: iris.detailed; anchors.centerIn: parent; width: 200; height: width; radius: width / 2; color: "transparent"; border.color: iris.accent; border.width: 0.6; opacity: 0.4 }
        Repeater {
            model: iris.bladeCount
            Shape {
                required property int index
                anchors.fill: parent
                preferredRendererType: Shape.CurveRenderer
                ShapePath {
                    fillColor: iris.detailed ? Qt.tint(iris.paper, Qt.rgba(iris.ink.r, iris.ink.g, iris.ink.b, 0.13 + index * 0.015)) : iris.ink
                    strokeColor: iris.ink; strokeWidth: iris.detailed ? 0.6 : 0
                    PathSvg { path: iris.blade(index) }
                }
            }
        }
        // Engraved focus scale and seven mounting screws, built only for the reveal.
        Repeater {
            model: iris.detailed ? 56 : 0
            Rectangle {
                required property int index
                x: 127.6; y: 7; width: index % 8 === 0 ? 1.2 : 0.6; height: index % 8 === 0 ? 7 : 3
                color: iris.accent; opacity: index % 8 === 0 ? 0.9 : 0.4
                transform: Rotation { origin.x: 0.4; origin.y: 121; angle: index * 360 / 56 }
            }
        }
        Repeater {
            model: iris.detailed ? 7 : 0
            Rectangle {
                required property int index
                x: 126; y: 17; width: 4; height: 4; radius: 2
                color: "transparent"; border.color: iris.ink; border.width: 0.7
                transform: Rotation { origin.x: 2; origin.y: 111; angle: (index + 0.5) * 360 / 7 }
            }
        }
        Text {
            visible: iris.detailed; anchors.horizontalCenter: parent.horizontalCenter; y: 233
            text: "P R I S M  ·  1 : 1"; font.pixelSize: 5; color: iris.ink; opacity: 0.7
        }
    }
}
