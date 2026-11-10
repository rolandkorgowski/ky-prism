"""Own-scene visual QA; test every nontransparent pixel against the 50% bound."""
import os
os.environ.update(QT_QPA_PLATFORM='offscreen', QT_QPA_PLATFORMTHEME='',
                  QT_QUICK_BACKEND='software', QT_STYLE_OVERRIDE='Fusion')
import json
import re
from pathlib import Path
from PyQt6.QtCore import QTimer, QUrl, QRect
from PyQt6.QtGui import QColor, QGuiApplication, QImage
from PyQt6.QtQuick import QQuickView, QQuickWindow

root = Path(__file__).resolve().parents[1]
(root / 'reports').mkdir(exist_ok=True)
version = json.loads((root / 'terminal-plugin/manifest.json').read_text())['version']
app = QGuiApplication([])
QQuickWindow.setDefaultAlphaBuffer(True)
view = QQuickView()
view.setResizeMode(QQuickView.ResizeMode.SizeRootObjectToView)
view.setSource(QUrl.fromLocalFile(str(root / 'terminal-plugin/WarpFlight.qml')))
if view.status() == QQuickView.Status.Error:
    raise SystemExit('Warp scene failed')
view.setColor(QColor('transparent'))
matrix = dict(name='default-matrix', ink='#e2ffe9', paper='#101b16', accent='#48d477')
jobs = [(1280,720,p,matrix) for p in (.12,.27,.55,.76,.88,.94,.985)]
jobs += [(w,h,p,matrix) for w,h in [(3840,2160),(7680,4320),(5120,1440),(1080,1920)] for p in (.76,.94)]
# Apply stock then user palette overrides, matching Commons.Color's hex-color
# parser and explicit-accent / ANSI fallbacks without switching the desktop.
palettes = {}
for folder in (Path('/usr/share/omarchy/themes'), Path.home()/'.config/omarchy/themes'):
    for path in sorted(folder.glob('*/colors.toml')):
        values = dict(re.findall(r'^\s*([A-Za-z0-9_-]+)\s*=\s*[\"\']?(#[0-9A-Fa-f]{6})', path.read_text(), re.M))
        palettes.setdefault(path.parent.name, {}).update(values)
for name, colors in sorted(palettes.items()):
    theme = dict(name=name, ink=colors.get('foreground',colors.get('color7','#cacccc')),
                 paper=colors.get('background',colors.get('color0','#101315')),
                 accent=colors.get('accent',colors.get('color4','#cacccc')))
    jobs.append((1280,720,.55,theme))
index = 0
results = []
errors = []


def contrast(a, b):
    def luminance(color):
        channels = [color.redF(), color.greenF(), color.blueF()]
        linear = [c/12.92 if c <= .04045 else ((c+.055)/1.055)**2.4 for c in channels]
        return sum(c*w for c,w in zip(linear,[.2126,.7152,.0722]))
    x,y = luminance(a),luminance(b)
    return (max(x,y)+.05)/(min(x,y)+.05)


def alpha_bounds(image):
    """Read alpha explicitly; window mask polarity/clear RGB must not affect QA."""
    rgba = image.convertToFormat(QImage.Format.Format_RGBA8888)
    assert rgba.hasAlphaChannel(), 'No alpha buffer in the capture'
    data = rgba.constBits().asstring(rgba.sizeInBytes())
    left, right, top, bottom = rgba.width(), -1, rgba.height(), -1
    for y in range(rgba.height()):
        start = y * rgba.bytesPerLine()
        alpha = data[start + 3:start + rgba.width() * 4:4]
        if any(alpha):
            xs = [x for x, value in enumerate(alpha) if value]
            left, right = min(left, xs[0]), max(right, xs[-1])
            top, bottom = min(top, y), y
    return QRect(left, top, right - left + 1, bottom - top + 1)


def render():
    global index
    if index == len(jobs):
        app.quit(); return
    w,h,phase,theme = jobs[index]
    view.resize(w,h)
    # The launcher now supplies only the theme background. Green foreground
    # bindings must remain intact for both light and dark palettes.
    view.rootObject().setProperty('paper',QColor(theme['paper']))
    view.rootObject().setProperty('progress', phase)
    view.show()
    def grab():
        global index
        try:
            image = view.grabWindow()
            assert not image.isNull(), 'Empty Qt capture'
            assert image.hasAlphaChannel(), ('Missing alpha surface', image.format())
            bounds = alpha_bounds(image)
            allowed = QRect(w // 4, h // 4, w // 2, h // 2)
            assert allowed.contains(bounds), (w,h,phase,bounds,allowed)
            assert not bounds.isEmpty(), (w,h,phase,'no visible glyphs')
            code_ink = view.rootObject().property('codeInk')
            ratio = contrast(code_ink,QColor(theme['paper']))
            foreground_ratio = contrast(view.rootObject().property('ink'),QColor(theme['paper']))
            assert ratio >= min(2.99,foreground_ratio-.01), (theme['name'],'low glyph contrast',ratio)
            assert code_ink.greenF() > max(code_ink.redF(),code_ink.blueF()), (theme['name'],'lost green hue')
            results.append(dict(width=w,height=h,phase=phase,
                                theme=theme['name'],codeInk=code_ink.name(),contrast=ratio,
                                visibleBounds=[bounds.x(),bounds.y(),bounds.width(),bounds.height()]))
            if phase == .88:
                assert image.save(str(root / f'reports/{version}-warp-peak.png'))
            if theme['name'] in ('matrix','catppuccin-latte','vaporwave'):
                assert image.save(str(root / f'reports/{version}-warp-{theme["name"]}.png'))
            index += 1; QTimer.singleShot(10,render)
        except Exception as error:
            # An exception escaping a Qt slot aborts PyQt. Report test failure
            # after the event loop returns instead of generating a core dump.
            errors.append(str(error)); app.quit()
    QTimer.singleShot(80,grab)


QTimer.singleShot(10,render)
QTimer.singleShot(60000,app.quit)
app.exec()
if errors:
    raise SystemExit('Warp render test failed: ' + errors[0])
assert index == len(jobs)
(root / f'reports/{version}-warp-bounds.json').write_text(json.dumps(results,indent=2)+'\n')
print(f'PASS {len(jobs)} rendered cases: 15 phases/sizes up to 8K and {len(palettes)} installed palettes; glyph contrast and central 50% bounds checked.')
