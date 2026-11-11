#!/usr/bin/env python3
"""Draws the logo of the Discord application "RS Re-ARMed" (the name Discord shows as "Playing ..."): DiscordLogo.svg
and DiscordLogo.png, 1024 x 1024 (needs rsvg-convert, `brew install librsvg`).

    gui/icon/make_discord_logo.py

The app icon's pick and chip traces (make_icon.py), with no gear and no rounded square: Discord cuts the picture to
its own shapes, a circle in some places, so the background fills the canvas and the pick stays inside that circle.
The same file is the application's icon and its Rich Presence art asset.
"""
import os
import shutil
import subprocess
import sys

from make_icon import HERE, S, facets, path, pick_outline


def traces(cx, cy):
    """Lines to the pick from all four edges, as on a chip: one bend of 45 degrees, a pad at the far end."""
    out = []
    for side in (-1, 1):
        for i, y in enumerate((232, 312, 392, 472, 552, 632, 712, 792)):
            x0 = cx + side * 512
            bend = cx + side * (408 - 12 * abs(i - 3.5))
            dy = (y - cy) * 0.20
            out.append([(x0, y), (bend, y), (bend - side * 64, y - dy)])
    for i, x in enumerate((332, 422, 602, 692)):
        out.append([(x, 0), (x, 118 + 14 * abs(i - 1.5))])
        out.append([(x, S), (x, S - 96 - 26 * (1.5 - abs(i - 1.5)))])
    return out


def svg():
    cx = S // 2
    pick = pick_outline(cx, 196, 548, 632)
    pcx, pcy = cx, 196 + 632 * 0.42
    g = []
    w = g.append
    w('<svg xmlns="http://www.w3.org/2000/svg" width="%d" height="%d" viewBox="0 0 %d %d">' % (S, S, S, S))
    w('<defs>')
    w('<linearGradient id="bg" x1="0" y1="0" x2="0" y2="1">'
      '<stop offset="0" stop-color="#253a6e"/><stop offset="0.55" stop-color="#141f45"/>'
      '<stop offset="1" stop-color="#0a1029"/></linearGradient>')
    w('<radialGradient id="glow" cx="%d" cy="%d" r="460" gradientUnits="userSpaceOnUse">'
      '<stop offset="0" stop-color="#4fc3ff" stop-opacity="0.50"/>'
      '<stop offset="1" stop-color="#4fc3ff" stop-opacity="0"/></radialGradient>' % (pcx, pcy))
    w('<linearGradient id="table" x1="0" y1="0" x2="1" y2="1">'
      '<stop offset="0" stop-color="#ffffff"/><stop offset="1" stop-color="#b9cbea"/></linearGradient>')
    w('<linearGradient id="pickbase" x1="0" y1="0" x2="0" y2="1">'
      '<stop offset="0" stop-color="#9fb4d8"/><stop offset="1" stop-color="#4a6190"/></linearGradient>')
    w('<linearGradient id="sheen" x1="0.1" y1="0" x2="0.7" y2="0.8">'
      '<stop offset="0" stop-color="#ffffff" stop-opacity="0.55"/>'
      '<stop offset="0.45" stop-color="#ffffff" stop-opacity="0.06"/>'
      '<stop offset="1" stop-color="#ffffff" stop-opacity="0"/></linearGradient>')
    w('<clipPath id="pick"><path d="%s"/></clipPath>' % path(pick))
    w('<filter id="lift" x="-20%" y="-20%" width="140%" height="140%">'
      '<feGaussianBlur in="SourceAlpha" stdDeviation="14"/><feOffset dy="14"/>'
      '<feComponentTransfer><feFuncA type="linear" slope="0.6"/></feComponentTransfer>'
      '<feMerge><feMergeNode/><feMergeNode in="SourceGraphic"/></feMerge></filter>')
    w('</defs>')

    w('<rect width="%d" height="%d" fill="url(#bg)"/>' % (S, S))
    w('<circle cx="%d" cy="%d" r="460" fill="url(#glow)"/>' % (pcx, pcy))
    w('<g fill="none" stroke="#6fd0ff" stroke-opacity="0.34" stroke-width="8" stroke-linecap="round" stroke-linejoin="round">')
    ends = []
    for t in traces(pcx, pcy):
        w('<path d="%s"/>' % path(t, close=False))
        ends.append(t[-1])
    w('</g><g fill="#0f1838" stroke="#6fd0ff" stroke-opacity="0.5" stroke-width="7">')
    for x, y in ends:
        w('<circle cx="%.1f" cy="%.1f" r="13"/>' % (x, y))
    w('</g>')

    w('<g filter="url(#lift)">')
    w('<path d="%s" fill="url(#pickbase)"/>' % path(pick))
    w('<g clip-path="url(#pick)" stroke-linejoin="round">')
    for poly, fill in facets(pick, pcx, pcy):
        w('<path d="%s" fill="%s" stroke="#e8f0ff" stroke-opacity="0.35" stroke-width="2.5"/>' % (path(poly), fill))
    w('<path d="%s" fill="url(#sheen)"/>' % path(pick))
    w('</g>')
    w('<path d="%s" fill="none" stroke="#f5f8ff" stroke-width="9" stroke-opacity="0.9"/>' % path(pick))
    w('</g>')
    w('</svg>')
    return "\n".join(g) + "\n"


if __name__ == "__main__":
    svg_path = os.path.join(HERE, "DiscordLogo.svg")
    with open(svg_path, "w") as f:
        f.write(svg())
    print(svg_path)
    rsvg = shutil.which("rsvg-convert")
    if not rsvg:
        sys.exit("rsvg-convert is needed for the .png: brew install librsvg")
    png = os.path.join(HERE, "DiscordLogo.png")
    subprocess.run([rsvg, "-w", str(S), "-h", str(S), svg_path, "-o", png], check=True)
    print(png)
