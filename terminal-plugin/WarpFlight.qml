pragma ComponentBehavior: Bound
import QtQuick

// A bounded scene: fixed glyph pool, analytic flight, GPU transforms and a
// small optional glow texture. The parent Loader destroys everything at rest.
Item {
    id: scene
    property real progress: 0
    property color paper: "#101b16"
    property color ink: paper.hslLightness > 0.55 ? "#075232" : "#b7f4ce"
    property color accent: paper.hslLightness > 0.55 ? "#167846" : "#41bf70"
    property bool glowEnabled: true
    // Palette bindings change only with the theme, not with animation progress.
    readonly property color codeInk: readableInk(accent, ink, paper)
    readonly property color echoInk: Qt.darker(codeInk, 1.65)
    readonly property color shadowInk: Qt.alpha(paper, 0.78)
    readonly property real phase: Math.max(0, Math.min(1, progress))
    readonly property real fade: ramp(0, 0.07, phase) * (1 - ramp(0.91, 1, phase))
    readonly property int particleCount: 324
    readonly property real effectWidth: width * 0.5
    readonly property real effectHeight: height * 0.5
    readonly property real travel: distanceAt(phase)
    readonly property real thrust: 0.72 // Stable, restrained trails; no speed or intensity ramp.
    // Resolve installed fonts once per scene, never during the flight. No
    // downloaded font or frame-by-frame glyph mutation is needed.
    readonly property var availableFonts: Qt.fontFamilies()
    readonly property string matrixFont: availableFonts.indexOf("Noto Sans Mono CJK JP") >= 0 ? "Noto Sans Mono CJK JP" : "monospace"
    readonly property string terminalFont: availableFonts.indexOf("JetBrainsMono Nerd Font") >= 0 ? "JetBrainsMono Nerd Font" : "monospace"
    readonly property int glyphBasePixels: 12
    function luminance(c) {
        function linear(v) { return v <= 0.04045 ? v / 12.92 : Math.pow((v + 0.055) / 1.055, 2.4) }
        return 0.2126 * linear(c.r) + 0.7152 * linear(c.g) + 0.0722 * linear(c.b)
    }
    function contrast(a, b) {
        const x = luminance(a), y = luminance(b)
        return (Math.max(x, y) + 0.05) / (Math.min(x, y) + 0.05)
    }
    function readableInk(color, foreground, background) {
        // Preserve green across themes; both candidate colors are green.
        // Only the shade changes for legibility against the theme background.
        for (let step = 0; step <= 8; ++step) {
            const candidate = Qt.tint(color, Qt.alpha(foreground, step / 8))
            if (contrast(candidate, background) >= 3) return candidate
        }
        return foreground
    }
    function ramp(a, b, v) {
        const t = Math.max(0, Math.min(1, (v - a) / (b - a)))
        return t * t * (3 - 2 * t)
    }
    function distanceAt(t) {
        // A steady cruise through a populated depth field, not an explosion.
        // Perspective conveys approach without a speed or slow-motion ramp;
        // neither the flight speed nor trail/blur strength builds up over time.
        t = Math.max(0, Math.min(1, t))
        return 2.4 * t
    }
    function hash(n) {
        const x = Math.sin(n * 127.1 + 311.7) * 43758.5453
        return x - Math.floor(x)
    }
    function isTerminal(seed) { return hash(seed * 31 + 5) < 0.24 }
    function glyph(seed) {
        const symbols = isTerminal(seed) ? "0123456789<>[]{}:/#$=+" : "ｱｲｳｴｵｶｷｸｹｺｻｼｽｾｿﾀﾁﾂﾃﾄﾅﾆﾇﾈﾉﾊﾋﾌﾍﾎﾏﾐﾑﾒﾓﾔﾕﾖﾗﾘﾙﾚﾛﾜﾝ"
        return symbols.charAt(Math.floor(hash(seed * 19 + 7) * symbols.length))
    }
    Item {
        id: region
        objectName: "warpRegion"
        width: scene.effectWidth; height: scene.effectHeight
        anchors.centerIn: parent
        clip: true // Hard guarantee: no glow, trail or glyph exceeds 50% per axis.
        opacity: scene.fade
        readonly property real unit: Math.min(width / 960, height / 540)
        readonly property size glowSize: Qt.size(Math.max(1, Math.min(768, width * 0.5)), Math.max(1, Math.min(432, height * 0.5)))
        Loader {
            anchors.fill: parent
            active: scene.glowEnabled && GraphicsInfo.api !== GraphicsInfo.Software
            sourceComponent: Component {
                Item {
                    ShaderEffectSource {
                        id: capture
                        sourceItem: particles
                        sourceRect: Qt.rect(0, 0, region.width, region.height)
                        hideSource: false
                        visible: false
                        live: true
                        textureSize: region.glowSize
                        format: ShaderEffectSource.RGBA8
                        recursive: false
                        samples: 0
                    }
                    ShaderEffect {
                        anchors.fill: parent
                        property var sourceTexture: capture
                        property color tint: scene.codeInk
                        property vector2d texelStep: Qt.vector2d(1 / region.glowSize.width, 1 / region.glowSize.height)
                        property real intensity: 0.10 + scene.thrust * 0.12
                        property real motion: scene.thrust
                        fragmentShader: "WarpGlow.frag.qsb"
                        // Blur is intentionally low resolution; sharp glyphs
                        // are drawn separately, without framebuffer sampling.
                        layer.enabled: true
                        layer.textureSize: region.glowSize
                        layer.smooth: true
                    }
                }
            }
        }
        Item {
            id: particles
            anchors.fill: parent
            Repeater {
                model: scene.particleCount
                Item {
                    id: flight
                    required property int index
                    readonly property real seed: scene.hash(index + 9)
                    readonly property real angle: index * 2.39996323 + (scene.hash(index + 301) - 0.5) * 0.38
                    readonly property real birth: scene.hash(index + 151) * 0.08 - 0.05
                    readonly property real origin: scene.distanceAt(birth)
                    // Constant depth offsets preserve each particle's velocity.
                    readonly property real age: Math.max(0, scene.travel - origin + scene.hash(index + 911))
                    readonly property real depth: age - Math.floor(age)
                    readonly property real ray: 0.025 + 0.17 * Math.sqrt(scene.hash(index + 71))
                    readonly property real dx: Math.cos(angle)
                    readonly property real dy: Math.sin(angle)
                    readonly property real headRadius: ray / (1.035 - depth)
                    readonly property real edge: 1 - scene.ramp(0.72, 0.98, headRadius)
                    readonly property real pixelAngle: Math.atan2(dy * region.height, dx * region.width)
                    opacity: scene.ramp(birth, birth + 0.07, scene.phase) * scene.ramp(0, 0.045, depth)
                    Rectangle {
                        readonly property real tailRadius: flight.ray / (1.035 - Math.max(0, flight.depth - 0.095 * scene.thrust))
                        readonly property real pathScale: 0.46 * Math.sqrt(Math.pow(flight.dx * region.width, 2) + Math.pow(flight.dy * region.height, 2))
                        width: Math.min(region.width * 0.12, Math.max(0, (flight.headRadius - tailRadius) * pathScale))
                        height: Math.max(0.5, region.unit * 0.55)
                        x: region.width / 2 + flight.dx * flight.headRadius * region.width * 0.46 - Math.cos(flight.pixelAngle) * width
                        y: region.height / 2 + flight.dy * flight.headRadius * region.height * 0.46 - Math.sin(flight.pixelAngle) * width
                        rotation: flight.pixelAngle * 180 / Math.PI
                        transformOrigin: Item.TopLeft
                        radius: height / 2
                        antialiasing: true
                        opacity: flight.edge * scene.thrust * 0.18
                        gradient: Gradient {
                            orientation: Gradient.Horizontal
                            GradientStop { position: 0; color: "transparent" }
                            GradientStop { position: 1; color: scene.codeInk }
                        }
                    }
                    Repeater {
                        model: 3 // One sharp head and two increasingly faint code echoes.
                        Text {
                            id: code
                            required property int index
                            readonly property int glyphSeed: flight.index * 3 + index
                            readonly property bool terminalGlyph: scene.isTerminal(glyphSeed)
                            readonly property real sliceDepth: Math.max(0, flight.depth - index * (0.007 + scene.thrust * 0.015))
                            readonly property real radius: flight.ray / (1.035 - sliceDepth)
                            readonly property real px: flight.dx * radius * region.width * 0.46
                            readonly property real py: flight.dy * radius * region.height * 0.46
                            readonly property real edge: 1 - scene.ramp(0.72, 0.98, radius)
                            x: region.width / 2 + px - width / 2
                            y: region.height / 2 + py - height / 2
                            scale: region.unit * (0.16 + 0.88 / (1.7 - sliceDepth * 1.35))
                            transformOrigin: Item.Center
                            text: scene.glyph(glyphSeed)
                            font.family: terminalGlyph ? scene.terminalFont : scene.matrixFont
                            font.pixelSize: scene.glyphBasePixels + Math.floor(flight.seed * 3)
                            font.weight: Font.Normal
                            font.kerning: false
                            color: index === 2 ? scene.echoInk : index === 0 && flight.index % 6 === 0 ? scene.ink : scene.codeInk
                            // Faint distant code, a crisp near head and a dark
                            // rear echo give depth without another render pass.
                            opacity: edge * (0.25 + 0.75 * scene.ramp(0.03, 0.83, sliceDepth)) * (index === 0 ? 0.88 : index === 1 ? 0.18 : 0.09) * scene.ramp(0, 0.035, sliceDepth)
                            style: index === 0 ? Text.Raised : Text.Normal
                            styleColor: scene.shadowInk
                            // Mirrored katakana gives Matrix its characteristic
                            // shape; terminal punctuation remains readable.
                            transform: Scale { origin.x: code.width / 2; xScale: code.terminalGlyph ? 1 : -1 }
                            renderType: Text.QtRendering
                        }
                    }
                }
            }
        }
    }
}
