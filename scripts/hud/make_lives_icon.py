#!/usr/bin/env python3
"""Lives icon sprite sheet for the HUD: a glossy neon heart and the frames its life-lost animation uses.

    python3 scripts/hud/make_lives_icon.py       (needs Google Chrome)

Writes assets/hud/lives_icons.png: 6 frames of 192 x 192 px in one row (1152 x 192, RGBA). Frame order is mirrored
by LivesFrame in src/core/GameHud.cpp:
  0 full heart (with baked glow)   1 left half   2 right half   3 crack overlay   4 empty glass heart
  5 close button (cartoon X)
"""
import os
import subprocess
import sys
import tempfile

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
OUT = os.path.join(ROOT, "assets", "hud", "lives_icons.png")
CHROME = os.environ.get("CHROME", "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome")
F = 192  # frame size

# heart in a 100 x 100 box; drawn at 1.36x centred in each frame, leaving room for the glow
HEART = "M50 90 C 20 68, 5 52, 5 33 C 5 18, 16 7, 30 7 C 39 7, 46 12, 50 20 C 54 12, 61 7, 70 7 C 84 7, 95 18, 95 33 C 95 52, 80 68, 50 90 Z"
CRACK = [(50, 20), (43, 33), (55, 45), (44, 57), (54, 70), (50, 90)]
crack_d = "M" + " L".join(f"{x} {y}" for x, y in CRACK)
left_clip = "M-10 -10 L50 -10 " + " ".join(f"L{x} {y}" for x, y in CRACK) + " L50 110 L-10 110 Z"
right_clip = "M110 -10 L50 -10 " + " ".join(f"L{x} {y}" for x, y in CRACK) + " L50 110 L110 110 Z"


def heart_body(glow=True, broken_edge=None):
    g = f'<path d="{HEART}" fill="#ff2bd6" filter="url(#glow)" opacity="0.85"/>' if glow else ""
    edge = f'<path d="{crack_d}" fill="none" stroke="#5a0a48" stroke-width="2.4" stroke-linejoin="round"/>' if broken_edge else ""
    return f"""{g}
      <path d="{HEART}" fill="url(#body)"/>
      <path d="{HEART}" fill="url(#shade)"/>
      <path d="{HEART}" fill="none" stroke="url(#rim)" stroke-width="2.2"/>
      <path d="{HEART}" fill="none" stroke="#29e7ff" stroke-width="1.4" opacity="0.55" clip-path="url(#rightRim)"/>
      <ellipse cx="33" cy="25" rx="13" ry="8" transform="rotate(-28 33 25)" fill="url(#gloss)"/>
      <circle cx="69" cy="22" r="2.6" fill="#ffffff" opacity="0.8"/>
      {edge}"""


DEFS = f"""
<defs>
  <radialGradient id="body" cx="36%" cy="30%" r="78%">
    <stop offset="0" stop-color="#ff9cee"/><stop offset="0.38" stop-color="#ff2bd6"/>
    <stop offset="0.78" stop-color="#c2109f"/><stop offset="1" stop-color="#7a0063"/>
  </radialGradient>
  <linearGradient id="shade" x1="0" y1="0" x2="0" y2="1">
    <stop offset="0.55" stop-color="#3b0030" stop-opacity="0"/><stop offset="1" stop-color="#3b0030" stop-opacity="0.55"/>
  </linearGradient>
  <linearGradient id="rim" x1="0" y1="0" x2="1" y2="1">
    <stop offset="0" stop-color="#ffe3fa"/><stop offset="0.5" stop-color="#ff7ae6" stop-opacity="0.5"/><stop offset="1" stop-color="#ff2bd6" stop-opacity="0.2"/>
  </linearGradient>
  <radialGradient id="gloss" cx="50%" cy="40%" r="60%">
    <stop offset="0" stop-color="#ffffff" stop-opacity="0.95"/><stop offset="1" stop-color="#ffffff" stop-opacity="0"/>
  </radialGradient>
  <linearGradient id="glass" x1="0" y1="0" x2="0" y2="1">
    <stop offset="0" stop-color="#3a2552" stop-opacity="0.75"/><stop offset="1" stop-color="#140a22" stop-opacity="0.75"/>
  </linearGradient>
  <filter id="glow" x="-40%" y="-40%" width="180%" height="180%"><feGaussianBlur stdDeviation="5"/></filter>
  <filter id="soft" x="-40%" y="-40%" width="180%" height="180%"><feGaussianBlur stdDeviation="1.6"/></filter>
  <radialGradient id="btn" cx="38%" cy="30%" r="80%">
    <stop offset="0" stop-color="#ff9cf0"/><stop offset="0.45" stop-color="#ff2bd6"/><stop offset="1" stop-color="#99007e"/>
  </radialGradient>
  <filter id="drop" x="-40%" y="-40%" width="180%" height="180%"><feGaussianBlur stdDeviation="3"/></filter>
  <clipPath id="leftHalf"><path d="{left_clip}"/></clipPath>
  <clipPath id="rightHalf"><path d="{right_clip}"/></clipPath>
  <clipPath id="rightRim"><rect x="62" y="0" width="40" height="100"/></clipPath>
</defs>"""


def frame(i, content):
    s = F / 100 * 0.72  # heart spans ~72% of the frame
    off = (F - 100 * s) / 2
    return f'<g transform="translate({i * F + off} {off + 2}) scale({s})">{content}</g>'


frames = [
    frame(0, heart_body(glow=True)),
    frame(1, f'<g clip-path="url(#leftHalf)">{heart_body(glow=False, broken_edge=True)}</g>'),
    frame(2, f'<g clip-path="url(#rightHalf)">{heart_body(glow=False, broken_edge=True)}</g>'),
    frame(3, f'<path d="{crack_d}" fill="none" stroke="#ffffff" stroke-width="5" stroke-linejoin="round" filter="url(#soft)" opacity="0.9"/>'
             f'<path d="{crack_d}" fill="none" stroke="#ffffff" stroke-width="1.8" stroke-linejoin="round"/>'
             f'<path d="M55 45 L66 41 M44 57 L35 62" stroke="#ffffff" stroke-width="1.3" stroke-linecap="round"/>'),
    frame(4, f'<path d="{HEART}" fill="url(#glass)"/>'
             f'<path d="{HEART}" fill="none" stroke="#9c8bb8" stroke-width="2.2" stroke-dasharray="0" opacity="0.85"/>'
             f'<ellipse cx="33" cy="25" rx="12" ry="7" transform="rotate(-28 33 25)" fill="url(#gloss)" opacity="0.35"/>'),
    # 5: cartoon close button: chunky glossy pink disc, dark outline, neon rim, fat white X with a dark outline
    frame(5, '<circle cx="50" cy="55" r="45" fill="#000000" opacity="0.5" filter="url(#drop)"/>'
             '<circle cx="50" cy="50" r="46" fill="#29e7ff" opacity="0.85"/>'
             '<circle cx="50" cy="50" r="43" fill="#2a0624"/>'
             '<circle cx="50" cy="50" r="38" fill="url(#btn)"/>'
             '<path d="M14 50 A36 36 0 0 0 86 50 A36 30 0 0 1 14 50 Z" fill="#3b0030" opacity="0.28"/>'
             '<path d="M22 40 A30 30 0 0 1 72 22" fill="none" stroke="#ffe0fa" stroke-width="3" stroke-linecap="round" opacity="0.9"/>'
             '<ellipse cx="40" cy="31" rx="18" ry="9" transform="rotate(-22 40 31)" fill="url(#gloss)" opacity="0.75"/>'
             '<path d="M35 35 L65 65 M65 35 L35 65" stroke="#3b0634" stroke-width="19" stroke-linecap="round"/>'
             '<path d="M35 35 L65 65 M65 35 L35 65" stroke="#ffffff" stroke-width="11" stroke-linecap="round"/>'
             '<path d="M36.5 34.5 L50 48" stroke="#ffffff" stroke-width="4" stroke-linecap="round" opacity="0.9"/>'),
]

svg = f'<svg xmlns="http://www.w3.org/2000/svg" width="{F * 6}" height="{F}" viewBox="0 0 {F * 6} {F}">{DEFS}{"".join(frames)}</svg>'


def main():
    if not os.path.exists(CHROME):
        sys.exit("Google Chrome not found (set CHROME=/path/to/chrome)")
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with tempfile.TemporaryDirectory() as tmp:
        path = os.path.join(tmp, "lives.svg")
        open(path, "w").write(svg)
        subprocess.run([CHROME, "--headless=new", "--disable-gpu", "--hide-scrollbars", "--force-device-scale-factor=1",
                        f"--window-size={F * 6},{F}", "--default-background-color=00000000", f"--screenshot={OUT}",
                        "file://" + path], check=True, capture_output=True)
    print("wrote", OUT)


if __name__ == "__main__":
    main()
