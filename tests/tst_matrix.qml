import QtQuick
import QtTest
import "../terminal-plugin"
TestCase {
    name: "MatrixTerminal"
    MatrixTerminal { id: scene; width: 1280; height: 720 }
    function test_phases_and_sizes() {
        for (let size of [[1280,720],[3840,2160],[7680,4320],[5120,1440],[1080,1920]]) {
            scene.width = size[0]; scene.height = size[1]
            compare(scene.effectWidth, size[0] * .5)
            compare(scene.effectHeight, size[1] * .5)
            let region = findChild(scene, "warpRegion")
            verify(region !== null)
            verify(region.clip)
            for (let step = 0; step <= 100; ++step) {
                scene.progress = step / 100
                verify(Number.isFinite(scene.phase))
                verify(scene.phase >= 0 && scene.phase <= 1)
                verify(scene.fade >= 0 && scene.fade <= 1)
            }
        }
        compare(scene.fade, 0)
    }
    function test_constant_fast_velocity() {
        let previous = -1
        for (let i = 0; i <= 1000; ++i) {
            let distance = scene.distanceAt(i / 1000)
            verify(distance >= previous)
            verify(Number.isFinite(distance))
            previous = distance
        }
        // No acceleration or old slow-motion interval may reappear.
        function speed(t) { return (scene.distanceAt(t + .00001) - scene.distanceAt(t - .00001)) / .00002 }
        let initialSpeed = speed(.001)
        verify(initialSpeed > 2.3)
        for (let frame = 2; frame < 999; ++frame) {
            let nextSpeed = speed(frame / 1000)
            verify(Math.abs(nextSpeed - initialSpeed) < .000001, "Warp speed must be constant")
        }
        compare(scene.distanceAt(1), 2.4)
        let blurStrength = scene.thrust
        for (let step = 0; step <= 100; ++step) {
            scene.progress = step / 100
            compare(scene.thrust, blurStrength)
        }
        scene.progress = 0; compare(scene.fade, 0)
        scene.progress = 1; compare(scene.fade, 0)
    }
    function test_glyphs() {
        verify(scene.matrixFont.length > 0)
        verify(scene.terminalFont.length > 0)
        compare(scene.glyphBasePixels, 12)
        let terminalCount = 0
        for (let i = 0; i < scene.particleCount * 3; ++i) {
            let glyph = scene.glyph(i)
            compare(glyph.length, 1)
            verify(glyph !== "\n")
            compare(glyph, scene.glyph(i))
            verify(scene.hash(i) >= 0 && scene.hash(i) < 1)
            if (scene.isTerminal(i)) ++terminalCount
        }
        verify(terminalCount > scene.particleCount * 3 * .15)
        verify(terminalCount < scene.particleCount * 3 * .35)
    }
}
