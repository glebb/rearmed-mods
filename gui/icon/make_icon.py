#!/usr/bin/env python3
"""Draws the app's icon: AppIcon.svg, and AppIcon.icns from it (needs rsvg-convert, `brew install librsvg`).

    gui/icon/make_icon.py          writes gui/icon/AppIcon.svg and gui/icon/AppIcon.icns
    gui/icon/make_icon.py --svg    the SVG only

RSMods' icon is a faceted silver pick with a syringe, the DLL injected into the Windows game. Here the pick is on macOS's
rounded square, on the traces of an Apple Silicon chip, as RS Re-ARMed runs the game natively on arm64, and the gear is
the settings app's. The build uses the .icns in this folder, so building needs neither this script nor rsvg-convert.
"""
import math
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
S = 1024  # the canvas; macOS's grid puts the rounded square at 100..924


def squircle(cx, cy, half, n=5.0, steps=720):
    """macOS's rounded square is close to a superellipse |x|^n + |y|^n = 1."""
    pts = []
    for i in range(steps):
        t = 2 * math.pi * i / steps
        c, s = math.cos(t), math.sin(t)
        x = half * math.copysign(abs(c) ** (2 / n), c)
        y = half * math.copysign(abs(s) ** (2 / n), s)
        pts.append((cx + x, cy + y))
    return pts


def bezier(p0, p1, p2, p3, steps):
    out = []
    for i in range(steps):
        t = i / steps
        u = 1 - t
        out.append((u**3 * p0[0] + 3 * u * u * t * p1[0] + 3 * u * t * t * p2[0] + t**3 * p3[0],
                    u**3 * p0[1] + 3 * u * u * t * p1[1] + 3 * u * t * t * p2[1] + t**3 * p3[1]))
    return out


def pick_outline(cx, top, w, h, steps=60):
    """A guitar pick: a wide rounded top, the sides narrowing to a rounded tip. Clockwise from the top middle."""
    hw = w / 2
    bot = top + h
    segs = [
        ((cx, top), (cx + hw * 0.55, top), (cx + hw * 0.98, top + h * 0.04), (cx + hw, top + h * 0.20)),
        ((cx + hw, top + h * 0.20), (cx + hw * 1.02, top + h * 0.38), (cx + hw * 0.55, top + h * 0.74), (cx + hw * 0.16, bot - h * 0.03)),
        ((cx + hw * 0.16, bot - h * 0.03), (cx + hw * 0.07, bot + h * 0.005), (cx - hw * 0.07, bot + h * 0.005), (cx - hw * 0.16, bot - h * 0.03)),
        ((cx - hw * 0.16, bot - h * 0.03), (cx - hw * 0.55, top + h * 0.74), (cx - hw * 1.02, top + h * 0.38), (cx - hw, top + h * 0.20)),
        ((cx - hw, top + h * 0.20), (cx - hw * 0.98, top + h * 0.04), (cx - hw * 0.55, top), (cx, top)),
    ]
    pts = []
    for seg in segs:
        pts += bezier(*seg, steps)
    return pts


def path(pts, close=True):
    d = "M%.1f %.1f " % pts[0] + " ".join("L%.1f %.1f" % p for p in pts[1:])
    return d + (" Z" if close else "")


def mix(a, b, t):
    t = max(0.0, min(1.0, t))
    ca = [int(a[i:i + 2], 16) for i in (1, 3, 5)]
    cb = [int(b[i:i + 2], 16) for i in (1, 3, 5)]
    return "#%02x%02x%02x" % tuple(round(x + (y - x) * t) for x, y in zip(ca, cb))


def facets(outline, cx, cy, light=(-0.55, -0.83)):
    """A cut stone: a table in the middle, and two rows of triangles from it to the outline, each lit by its slope."""
    k = 10
    n = len(outline)
    start = n // 2  # the tip, so that the facets lie the same on both sides
    anchors = [outline[(start + i * n // k) % n] for i in range(k)]
    mids = [outline[(start + i * n // k + n // (2 * k)) % n] for i in range(k)]
    tcx, tcy = cx, cy - 50
    # the table is wide at the top and short towards the tip, so the facets there don't run to slivers
    table = [(tcx + (x - cx) * 0.42, tcy + (y - cy) * (0.34 if y < cy else 0.16)) for x, y in anchors]
    dark, pale = "#3a4f78", "#f3f7ff"
    out = []

    def shade(poly, bias):
        mx = sum(p[0] for p in poly) / len(poly) - tcx
        my = sum(p[1] for p in poly) / len(poly) - tcy
        r = math.hypot(mx, my) or 1
        lit = -(mx / r * light[0] + my / r * light[1])  # facing the light: 1
        out.append((poly, mix(dark, pale, 0.52 + 0.40 * lit + bias)))

    for i in range(k):
        j = (i + 1) % k
        shade([table[i], table[j], mids[i]], 0.10)
        shade([table[i], anchors[i], mids[i]], -0.06)
        shade([table[j], mids[i], anchors[j]], -0.12)
    out.append((table, "url(#table)"))
    return out


def gear(cx, cy, r_out, r_root, teeth, steps=8):
    """A gear's outline: flat-topped teeth with sloped flanks."""
    pts = []
    pitch = 2 * math.pi / teeth
    for i in range(teeth):
        a = i * pitch - math.pi / 2
        for t, r in ((-0.27, r_root), (-0.17, r_out), (0.17, r_out), (0.27, r_root)):
            pts.append((cx + r * math.cos(a + t * pitch), cy + r * math.sin(a + t * pitch)))
        for s in range(1, steps):
            b = a + (0.27 + 0.46 * s / steps) * pitch
            pts.append((cx + r_root * math.cos(b), cy + r_root * math.sin(b)))
    return pts


def traces(cx, cy):
    """Lines to the pick, as on a chip: in from the edges, one bend of 45 degrees, a pad at the far end."""
    out = []
    for side in (-1, 1):
        for i, y in enumerate((300, 370, 440, 510, 580, 650, 720)):
            x0 = cx + side * 420
            bend = cx + side * (330 - 18 * abs(i - 3))
            dy = (y - cy) * 0.18
            x1 = bend - side * 60
            out.append([(x0, y), (bend, y), (x1, y - dy)])
    for i, x in enumerate((360, 440, 584, 664)):
        out.append([(x, 100), (x, 190 + 12 * abs(i - 1.5))])
    return out


def svg():
    cx, cy = 512, 512
    body = path(squircle(cx, cy, 412))
    pick = pick_outline(cx - 34, 214, 520, 600)
    pcx, pcy = cx - 34, 214 + 600 * 0.42
    gx, gy = 708, 708
    g = []
    w = g.append
    w('<svg xmlns="http://www.w3.org/2000/svg" width="%d" height="%d" viewBox="0 0 %d %d">' % (S, S, S, S))
    w('<defs>')
    w('<linearGradient id="bg" x1="0" y1="0" x2="0" y2="1">'
      '<stop offset="0" stop-color="#253a6e"/><stop offset="0.55" stop-color="#141f45"/>'
      '<stop offset="1" stop-color="#0a1029"/></linearGradient>')
    w('<radialGradient id="glow" cx="%d" cy="%d" r="380" gradientUnits="userSpaceOnUse">'
      '<stop offset="0" stop-color="#4fc3ff" stop-opacity="0.45"/>'
      '<stop offset="1" stop-color="#4fc3ff" stop-opacity="0"/></radialGradient>' % (pcx, pcy))
    w('<linearGradient id="rim" x1="0" y1="0" x2="0" y2="1">'
      '<stop offset="0" stop-color="#ffffff" stop-opacity="0.28"/>'
      '<stop offset="0.12" stop-color="#ffffff" stop-opacity="0"/></linearGradient>')
    w('<linearGradient id="table" x1="0" y1="0" x2="1" y2="1">'
      '<stop offset="0" stop-color="#ffffff"/><stop offset="1" stop-color="#b9cbea"/></linearGradient>')
    w('<linearGradient id="pickbase" x1="0" y1="0" x2="0" y2="1">'
      '<stop offset="0" stop-color="#9fb4d8"/><stop offset="1" stop-color="#4a6190"/></linearGradient>')
    w('<linearGradient id="sheen" x1="0.1" y1="0" x2="0.7" y2="0.8">'
      '<stop offset="0" stop-color="#ffffff" stop-opacity="0.55"/>'
      '<stop offset="0.45" stop-color="#ffffff" stop-opacity="0.06"/>'
      '<stop offset="1" stop-color="#ffffff" stop-opacity="0"/></linearGradient>')
    w('<linearGradient id="steel" x1="0" y1="0" x2="0" y2="1">'
      '<stop offset="0" stop-color="#fbfcfe"/><stop offset="0.5" stop-color="#c3cad6"/>'
      '<stop offset="1" stop-color="#7d8697"/></linearGradient>')
    w('<linearGradient id="steelin" x1="0" y1="0" x2="0" y2="1">'
      '<stop offset="0" stop-color="#6f7888"/><stop offset="1" stop-color="#d9dee6"/></linearGradient>')
    w('<linearGradient id="hub" x1="0" y1="0" x2="0" y2="1">'
      '<stop offset="0" stop-color="#f4f6f9"/><stop offset="1" stop-color="#9aa3b2"/></linearGradient>')
    w('<clipPath id="body"><path d="%s"/></clipPath>' % body)
    w('<clipPath id="pick"><path d="%s"/></clipPath>' % path(pick))
    w('<filter id="shadow" x="-20%" y="-20%" width="140%" height="140%">'
      '<feGaussianBlur in="SourceAlpha" stdDeviation="14"/><feOffset dy="12"/>'
      '<feComponentTransfer><feFuncA type="linear" slope="0.45"/></feComponentTransfer>'
      '<feMerge><feMergeNode/><feMergeNode in="SourceGraphic"/></feMerge></filter>')
    w('<filter id="lift" x="-20%" y="-20%" width="140%" height="140%">'
      '<feGaussianBlur in="SourceAlpha" stdDeviation="10"/><feOffset dy="10"/>'
      '<feComponentTransfer><feFuncA type="linear" slope="0.55"/></feComponentTransfer>'
      '<feMerge><feMergeNode/><feMergeNode in="SourceGraphic"/></feMerge></filter>')
    w('</defs>')

    # the rounded square, with the drop shadow macOS's icons have
    w('<g filter="url(#shadow)"><path d="%s" fill="url(#bg)"/></g>' % body)
    w('<g clip-path="url(#body)">')
    w('<circle cx="%d" cy="%d" r="380" fill="url(#glow)"/>' % (pcx, pcy))
    w('<g fill="none" stroke="#6fd0ff" stroke-opacity="0.30" stroke-width="7" stroke-linecap="round" stroke-linejoin="round">')
    ends = []
    for t in traces(pcx, pcy):
        w('<path d="%s"/>' % path(t, close=False))
        ends.append(t[-1])
    w('</g><g fill="#0f1838" stroke="#6fd0ff" stroke-opacity="0.45" stroke-width="6">')
    for x, y in ends:
        w('<circle cx="%.1f" cy="%.1f" r="11"/>' % (x, y))
    w('</g>')
    w('<path d="%s" fill="url(#rim)"/>' % body)
    w('</g>')

    # the pick, a cut stone as RSMods' is
    w('<g filter="url(#lift)">')
    w('<path d="%s" fill="url(#pickbase)"/>' % path(pick))
    w('<g clip-path="url(#pick)" stroke-linejoin="round">')
    for poly, fill in facets(pick, pcx, pcy):
        w('<path d="%s" fill="%s" stroke="#e8f0ff" stroke-opacity="0.35" stroke-width="2.5"/>' % (path(poly), fill))
    w('<path d="%s" fill="url(#sheen)"/>' % path(pick))
    w('</g>')
    w('<path d="%s" fill="none" stroke="#f5f8ff" stroke-width="8" stroke-opacity="0.9"/>' % path(pick))
    w('<path d="%s" fill="none" stroke="#2a3a5e" stroke-width="3" stroke-opacity="0.6" transform="translate(%.1f %.1f) scale(0.985) translate(%.1f %.1f)"/>'
      % (path(pick), pcx, pcy, -pcx, -pcy))
    w('</g>')

    # the gear, the settings app's
    w('<g filter="url(#lift)">')
    w('<path d="%s" fill="url(#steel)" stroke="#5d6676" stroke-width="3"/>' % path(gear(gx, gy, 168, 138, 12)))
    w('<circle cx="%d" cy="%d" r="112" fill="url(#steelin)"/>' % (gx, gy))
    w('<circle cx="%d" cy="%d" r="98" fill="url(#steel)"/>' % (gx, gy))
    w('<circle cx="%d" cy="%d" r="54" fill="#4b5465"/>' % (gx, gy))
    w('<circle cx="%d" cy="%d" r="44" fill="url(#hub)"/>' % (gx, gy))
    w('</g>')
    w('</svg>')
    return "\n".join(g) + "\n"


def icns(svg_path, out):
    rsvg = shutil.which("rsvg-convert")
    if not rsvg:
        sys.exit("rsvg-convert is needed for the .icns: brew install librsvg")
    tmp = tempfile.mkdtemp()
    iconset = os.path.join(tmp, "AppIcon.iconset")
    os.mkdir(iconset)
    for size in (16, 32, 128, 256, 512):
        for scale in (1, 2):
            name = "icon_%dx%d%s.png" % (size, size, "@2x" if scale == 2 else "")
            px = size * scale
            subprocess.run([rsvg, "-w", str(px), "-h", str(px), svg_path, "-o", os.path.join(iconset, name)], check=True)
    subprocess.run(["iconutil", "-c", "icns", iconset, "-o", out], check=True)
    shutil.rmtree(tmp)


if __name__ == "__main__":
    svg_path = os.path.join(HERE, "AppIcon.svg")
    with open(svg_path, "w") as f:
        f.write(svg())
    print(svg_path)
    if "--svg" not in sys.argv:
        out = os.path.join(HERE, "AppIcon.icns")
        icns(svg_path, out)
        print(out)
