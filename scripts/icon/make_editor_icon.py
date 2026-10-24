#!/usr/bin/env python3
"""The level editor's app icon (CuckooEditor, the iPhone and iPad editor): the game's neon night on a blueprint grid,
a crate wall with a stack of eggs climbing it and a saw blade set in the street, and a glowing pencil drawing the
course's next wall.

    python3 scripts/icon/make_editor_icon.py      (renders with Google Chrome, or the system's WebKit without it; Pillow)

Writes platforms/ios_editor/Assets.xcassets (AppIcon.appiconset: 1024 universal + iOS 18 dark and tinted; the launch
screen's colour) and scripts/icon/out/editor-1024.png.
"""
import json
import os
import subprocess
import sys
import tempfile

from PIL import Image

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
OUT = os.path.join(ROOT, "scripts", "icon", "out")
ASSETS = os.path.join(ROOT, "platforms", "ios_editor", "Assets.xcassets")
CHROME = os.environ.get("CHROME", "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome")
S = 1024
NEON, HOT, VOLT, AMBER = "#29e7ff", "#ff2bd6", "#f4ff5a", "#ffc23a"


def svg(dark=False, tinted=False):
    import math
    street = 800
    grid = []
    for i in range(0, S + 1, 64):  # the blueprint over the sky: major lines every 64, minor every 16
        grid.append(f'<line x1="{i}" y1="0" x2="{i}" y2="{street}" class="maj"/>')
        if i <= street:
            grid.append(f'<line x1="0" y1="{i}" x2="{S}" y2="{i}" class="maj"/>')
        for k in (16, 32, 48):
            if i + k < S:
                grid.append(f'<line x1="{i + k}" y1="0" x2="{i + k}" y2="{street}" class="min"/>')
            if i + k < street:
                grid.append(f'<line x1="0" y1="{i + k}" x2="{S}" y2="{i + k}" class="min"/>')
    floor = []
    for i in range(-10, 11):  # the street's floor: lines converging on the horizon, as the game's icon has it
        floor.append(f'<line x1="{512 + i * 60}" y1="{street}" x2="{512 + i * 260}" y2="{S}" class="floor"/>')
    y, step = street + 14, 16
    while y < S:
        floor.append(f'<line x1="0" y1="{y:.0f}" x2="{S}" y2="{y:.0f}" class="floor"/>')
        step *= 1.35
        y += step
    crates = []
    for r in range(3):  # a crate wall, three high, each crate its neon edge and its X
        for c in range(2):
            x, y = 200 + c * 112, street - (r + 1) * 112
            crates.append(f'''<g transform="translate({x},{y})">
              <rect x="4" y="4" width="104" height="104" rx="10" fill="url(#crate)" stroke="{NEON}" stroke-width="5" filter="url(#glow)"/>
              <path d="M20 20 L92 92 M92 20 L20 92" stroke="{NEON}" stroke-opacity="0.45" stroke-width="6" stroke-linecap="round"/>
            </g>''')
    top = street - 3 * 112
    eggs = []
    for k in range(3):  # her stack on the wall's top: three eggs
        cy = top - 50 - k * 94
        eggs.append(f'<ellipse cx="312" cy="{cy}" rx="44" ry="52" fill="url(#egg)" stroke="#fff3dc" stroke-width="3" filter="url(#soft)"/>')
        eggs.append(f'<ellipse cx="298" cy="{cy - 20}" rx="11" ry="17" fill="#ffffff" opacity="0.8"/>')
    teeth = []
    for k in range(24):  # the saw: its top half above the street
        a = math.pi + k * math.pi / 12
        r = 96 if k % 2 == 0 else 78
        teeth.append(f"{760 + math.cos(a) * r:.1f},{street + math.sin(a) * r:.1f}")
    bg = "#06030e" if not dark else "#020106"
    return f'''<svg xmlns="http://www.w3.org/2000/svg" width="{S}" height="{S}" viewBox="0 0 {S} {S}">
<defs>
  <linearGradient id="sky" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="{bg}"/><stop offset="0.7" stop-color="#160a32"/><stop offset="0.78" stop-color="#2a0c44"/><stop offset="1" stop-color="#0a0418"/></linearGradient>
  <radialGradient id="horizon" cx="0.5" cy="0.78" r="0.55"><stop offset="0" stop-color="{HOT}" stop-opacity="0.5"/><stop offset="1" stop-color="{HOT}" stop-opacity="0"/></radialGradient>
  <linearGradient id="crate" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#1a3352"/><stop offset="1" stop-color="#0c1828"/></linearGradient>
  <radialGradient id="egg" cx="0.38" cy="0.32" r="0.8"><stop offset="0" stop-color="#ffffff"/><stop offset="0.55" stop-color="#fff3dc"/><stop offset="1" stop-color="#e9c99a"/></radialGradient>
  <linearGradient id="pencil" x1="0" y1="0" x2="1" y2="0"><stop offset="0" stop-color="{HOT}"/><stop offset="0.5" stop-color="#ff7ae6"/><stop offset="1" stop-color="{HOT}"/></linearGradient>
  <linearGradient id="blade" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="#ffffff"/><stop offset="1" stop-color="#9aa6b8"/></linearGradient>
  <linearGradient id="fade" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#05030b" stop-opacity="0"/><stop offset="1" stop-color="#05030b" stop-opacity="0.85"/></linearGradient>
  <filter id="glow" x="-50%" y="-50%" width="200%" height="200%"><feGaussianBlur stdDeviation="9" result="b"/><feMerge><feMergeNode in="b"/><feMergeNode in="SourceGraphic"/></feMerge></filter>
  <filter id="soft" x="-30%" y="-30%" width="160%" height="160%"><feGaussianBlur stdDeviation="3" result="b"/><feMerge><feMergeNode in="b"/><feMergeNode in="SourceGraphic"/></feMerge></filter>
  <filter id="bloom" x="-80%" y="-80%" width="260%" height="260%"><feGaussianBlur stdDeviation="22"/></filter>
  <clipPath id="above"><rect x="0" y="0" width="{S}" height="{street}"/></clipPath>
  <style>.maj{{stroke:{NEON};stroke-opacity:0.16;stroke-width:2}} .min{{stroke:{NEON};stroke-opacity:0.06;stroke-width:1}} .floor{{stroke:{HOT};stroke-opacity:0.55;stroke-width:3}}</style>
</defs>
<rect width="{S}" height="{S}" fill="url(#sky)"/>
<rect width="{S}" height="{S}" fill="url(#horizon)"/>
{''.join(grid)}
<g filter="url(#soft)">{''.join(floor)}</g>
<rect x="0" y="{street}" width="{S}" height="{S - street}" fill="url(#fade)"/>
<!-- the next wall, drawn dashed: where the pencil is going -->
<rect x="440" y="{street - 224}" width="112" height="224" rx="10" fill="{VOLT}" fill-opacity="0.08" stroke="{VOLT}" stroke-width="6" stroke-dasharray="22 14" filter="url(#glow)"/>
{''.join(crates)}
{''.join(eggs)}
<g clip-path="url(#above)">
  <circle cx="760" cy="{street}" r="130" fill="{HOT}" opacity="0.35" filter="url(#bloom)"/>
  <polygon points="{' '.join(teeth)}" fill="url(#blade)" stroke="{HOT}" stroke-width="5" filter="url(#glow)"/>
  <circle cx="760" cy="{street}" r="22" fill="{HOT}"/>
</g>
<line x1="0" y1="{street}" x2="{S}" y2="{street}" stroke="{NEON}" stroke-width="8" filter="url(#glow)"/>
<!-- the pencil -->
<g transform="translate(590,{street - 330}) rotate(38)">
  <ellipse cx="0" cy="-10" rx="70" ry="250" fill="{HOT}" opacity="0.28" filter="url(#bloom)"/>
  <rect x="-34" y="-250" width="68" height="300" rx="10" fill="url(#pencil)" stroke="#ffffff" stroke-opacity="0.6" stroke-width="4"/>
  <rect x="-34" y="-250" width="68" height="40" rx="10" fill="{AMBER}"/>
  <rect x="-34" y="-214" width="68" height="14" fill="#d9d9e6"/>
  <polygon points="-34,50 34,50 0,130" fill="#ffe3b8"/>
  <polygon points="-11,105 11,105 0,130" fill="{VOLT}" filter="url(#glow)"/>
  <line x1="-12" y1="-200" x2="-12" y2="40" stroke="#ffffff" stroke-opacity="0.45" stroke-width="6" stroke-linecap="round"/>
</g>
<circle cx="{590 - 130 * 0.616:.0f}" cy="{street - 330 + 130 * 0.788:.0f}" r="16" fill="{VOLT}" filter="url(#glow)"/>
</svg>'''


def render(svg_text, out_png):
    if not os.path.exists(CHROME): # (the system's WebKit: render_svg.swift)
        with tempfile.TemporaryDirectory() as d:
            src = os.path.join(d, "icon.svg")
            with open(src, "w") as f:
                f.write(svg_text)
            subprocess.run(["swift", os.path.join(os.path.dirname(__file__), "render_svg.swift"), src, out_png, str(S)], check=True)
            img = Image.open(out_png)
            if img.size != (S, S):
                img.resize((S, S), Image.LANCZOS).save(out_png)
        return
    with tempfile.TemporaryDirectory() as d:
        src = os.path.join(d, "icon.html")
        with open(src, "w") as f:
            f.write(f'<html><body style="margin:0;background:#000">{svg_text}</body></html>')
        subprocess.run([CHROME, "--headless=new", "--disable-gpu", "--hide-scrollbars", f"--window-size={S},{S}",
                        f"--screenshot={out_png}", "--default-background-color=00000000", "file://" + src], check=True, capture_output=True)


def main():
    os.makedirs(OUT, exist_ok=True)
    icon_dir = os.path.join(ASSETS, "AppIcon.appiconset")
    os.makedirs(icon_dir, exist_ok=True)
    master = os.path.join(OUT, "editor-1024.png")
    render(svg(), master)
    Image.open(master).convert("RGB").save(os.path.join(icon_dir, "AppIcon-1024.png"))
    dark = os.path.join(OUT, "editor-1024-dark.png")
    render(svg(dark=True), dark)
    Image.open(dark).convert("RGB").save(os.path.join(icon_dir, "AppIcon-1024-dark.png"))
    Image.open(master).convert("L").convert("RGB").save(os.path.join(icon_dir, "AppIcon-1024-tinted.png"))
    with open(os.path.join(icon_dir, "Contents.json"), "w") as f:
        json.dump({"images": [
            {"filename": "AppIcon-1024.png", "idiom": "universal", "platform": "ios", "size": "1024x1024"},
            {"appearances": [{"appearance": "luminosity", "value": "dark"}], "filename": "AppIcon-1024-dark.png", "idiom": "universal", "platform": "ios", "size": "1024x1024"},
            {"appearances": [{"appearance": "luminosity", "value": "tinted"}], "filename": "AppIcon-1024-tinted.png", "idiom": "universal", "platform": "ios", "size": "1024x1024"},
        ], "info": {"author": "xcode", "version": 1}}, f, indent=2)
    with open(os.path.join(ASSETS, "Contents.json"), "w") as f:
        json.dump({"info": {"author": "xcode", "version": 1}}, f, indent=2)
    launch = os.path.join(ASSETS, "LaunchBackground.colorset")
    os.makedirs(launch, exist_ok=True)
    with open(os.path.join(launch, "Contents.json"), "w") as f:
        json.dump({"colors": [{"color": {"color-space": "srgb", "components": {"alpha": "1.000", "red": "0.024", "green": "0.012", "blue": "0.055"}}, "idiom": "universal"}],
                   "info": {"author": "xcode", "version": 1}}, f, indent=2)
    print("wrote", icon_dir)


if __name__ == "__main__":
    main()
