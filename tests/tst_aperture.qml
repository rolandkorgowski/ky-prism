import QtQuick
import QtTest
import "../plugin"
import "../chrome-plugin"
TestCase {
    name: "PrismAperture"
    Aperture { id: iris; width: 256; height: 256 }
    ChromeMark { id: chrome; width: 256; height: 256 }
    function test_chrome() {
        for (var size of [24, 420, 4320, 7680]) { chrome.width=size; chrome.height=size; chrome.colored=true; compare(chrome.width, size); chrome.colored=false }
    }
    function test_geometry() {
        compare(iris.bladeCount, 7)
        for (var detail of [false, true]) {
        iris.detailed = detail
        for (var size of [24, 256, 800, 4320]) {
            iris.width = size; iris.height = size
            for (var step = 0; step <= 100; ++step) {
                iris.opening = step / 100
                for (var i = 0; i < iris.bladeCount; ++i) {
                    var path = iris.blade(i)
                    verify(path.indexOf("NaN") === -1 && path.indexOf("undefined") === -1)
                    verify(path.startsWith("M ") && path.endsWith(" Z"))
                    verify(iris.blade(i).indexOf(" L ") !== -1)
                }
            }
        }
        }
    }
}
