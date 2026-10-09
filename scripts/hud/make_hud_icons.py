#!/usr/bin/env python3
"""HUD icon atlas: every full-colour sprite the HUD draws (coin spin, streak flame, chests, menu icons, close button).

    python3 scripts/hud/make_hud_icons.py       (needs Google Chrome)

Writes:
  assets/hud/hud_icons.png   COLS x ROWS grid of 192 x 192 px frames (RGBA, transparent background)
  src/core/HudIcons.h        enum class Icon (frame index per sprite) + the grid size, used by Game::hudSprite

Raster sprites: the coin-shop art (piles, chest, menu buttons, price buttons, the FREE frame, ...) comes from the
artist's sheet, cut out by scripts/hud/extract_shop_sprites.py into assets/hud/shop/*.png; png() embeds those in
their cells (with an optional baked glow). The 3x art is packed at 192 px per cell and the atlas mip chain serves
the 2x and 1x screens.

Style rules, shared by every icon so they read as one set: dark outline, gradient body lit from the top-left, a
darker bottom shade, a light rim on the top-left edge and a neon rim on the right, a soft gloss highlight, and a
baked neon glow behind. Icons are drawn in a 100 x 100 box and scaled to ~74% of the frame, leaving room for glow.
"""
import base64
import math
import os
import subprocess
import sys
import tempfile

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
OUT_PNG = os.path.join(ROOT, "assets", "hud", "hud_icons.png")
OUT_H = os.path.join(ROOT, "src", "core", "HudIcons.h")
SHOP = os.path.join(ROOT, "assets", "hud", "shop")
CHROME = os.environ.get("CHROME", "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome")
F = 192
COLS = 8

DEFS = """
<defs>
  <filter id="glow" x="-50%" y="-50%" width="200%" height="200%"><feGaussianBlur stdDeviation="5"/></filter>
  <filter id="glowS" x="-50%" y="-50%" width="200%" height="200%"><feGaussianBlur stdDeviation="2.2"/></filter>
  <filter id="soft" x="-40%" y="-40%" width="180%" height="180%"><feGaussianBlur stdDeviation="1.4"/></filter>
  <filter id="drop" x="-40%" y="-40%" width="180%" height="180%"><feGaussianBlur stdDeviation="3"/></filter>
  <radialGradient id="gloss" cx="50%" cy="40%" r="60%">
    <stop offset="0" stop-color="#ffffff" stop-opacity="0.95"/><stop offset="1" stop-color="#ffffff" stop-opacity="0"/>
  </radialGradient>
  <linearGradient id="shade" x1="0" y1="0" x2="0" y2="1">
    <stop offset="0.5" stop-color="#14001a" stop-opacity="0"/><stop offset="1" stop-color="#14001a" stop-opacity="0.5"/>
  </linearGradient>
  <!-- bodies: pink, cyan, gold, green, violet, orange, slate, red -->
  <radialGradient id="pink" cx="36%" cy="30%" r="80%"><stop offset="0" stop-color="#ffa3f0"/><stop offset="0.42" stop-color="#ff2bd6"/><stop offset="1" stop-color="#8a0071"/></radialGradient>
  <radialGradient id="cyan" cx="36%" cy="30%" r="80%"><stop offset="0" stop-color="#c9fbff"/><stop offset="0.42" stop-color="#29e7ff"/><stop offset="1" stop-color="#0a6f92"/></radialGradient>
  <radialGradient id="gold" cx="36%" cy="30%" r="80%"><stop offset="0" stop-color="#fff6c2"/><stop offset="0.38" stop-color="#ffd23a"/><stop offset="0.8" stop-color="#e08a00"/><stop offset="1" stop-color="#9a5400"/></radialGradient>
  <radialGradient id="green" cx="36%" cy="30%" r="80%"><stop offset="0" stop-color="#cbffe6"/><stop offset="0.42" stop-color="#2bff9a"/><stop offset="1" stop-color="#047a48"/></radialGradient>
  <radialGradient id="violet" cx="36%" cy="30%" r="80%"><stop offset="0" stop-color="#e2ccff"/><stop offset="0.42" stop-color="#9a5bff"/><stop offset="1" stop-color="#3d138f"/></radialGradient>
  <radialGradient id="orange" cx="36%" cy="30%" r="80%"><stop offset="0" stop-color="#ffe0b8"/><stop offset="0.42" stop-color="#ff8a1a"/><stop offset="1" stop-color="#9a3a00"/></radialGradient>
  <radialGradient id="slate" cx="36%" cy="30%" r="80%"><stop offset="0" stop-color="#6d5a8f"/><stop offset="0.5" stop-color="#2e2246"/><stop offset="1" stop-color="#120a22"/></radialGradient>
  <radialGradient id="red" cx="36%" cy="30%" r="80%"><stop offset="0" stop-color="#ffb3c0"/><stop offset="0.42" stop-color="#ff2b4a"/><stop offset="1" stop-color="#8a0018"/></radialGradient>
  <linearGradient id="goldRim" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="#fffbe0"/><stop offset="0.5" stop-color="#ffc93a"/><stop offset="1" stop-color="#b86a00"/></linearGradient>
  <linearGradient id="goldEdge" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#d98b00"/><stop offset="1" stop-color="#6e3a00"/></linearGradient>
  <radialGradient id="goldFace" cx="40%" cy="34%" r="75%"><stop offset="0" stop-color="#ffe98a"/><stop offset="0.55" stop-color="#f7b500"/><stop offset="1" stop-color="#b56600"/></radialGradient>
  <linearGradient id="glassFill" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#5a5a8c" stop-opacity="0.50"/><stop offset="1" stop-color="#1a1636" stop-opacity="0.62"/></linearGradient>
  <filter id="pngGlow" x="-30%" y="-30%" width="160%" height="160%"><feGaussianBlur stdDeviation="7"/></filter>
  <filter id="glow6" x="-30%" y="-30%" width="160%" height="160%"><feGaussianBlur stdDeviation="6"/></filter>
  <linearGradient id="holo" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="#ff2bd6"/><stop offset="0.5" stop-color="#f4ff5a"/><stop offset="1" stop-color="#29e7ff"/></linearGradient>
  <radialGradient id="fireOut" cx="50%" cy="75%" r="70%"><stop offset="0" stop-color="#ffd23a"/><stop offset="0.55" stop-color="#ff6a2b"/><stop offset="1" stop-color="#ff2bd6"/></radialGradient>
  <radialGradient id="fireIn" cx="50%" cy="80%" r="70%"><stop offset="0" stop-color="#ffffff"/><stop offset="0.5" stop-color="#fff3a0"/><stop offset="1" stop-color="#ffb21a"/></radialGradient>
  <radialGradient id="beam" cx="50%" cy="100%" r="100%"><stop offset="0" stop-color="#ffffff" stop-opacity="0.95"/><stop offset="0.5" stop-color="#29e7ff" stop-opacity="0.45"/><stop offset="1" stop-color="#29e7ff" stop-opacity="0"/></radialGradient>
</defs>"""

OUTLINE = "#1a0420"


def glossy(shape, fill, rim="#29e7ff", glow=None, gloss=(34, 28, 16, 9, -25), outline=OUTLINE, shadow=True):
    """A shape drawn in the shared glossy style. `shape` is an SVG element string with the attribute FILL where the fill goes."""
    s = ""
    if shadow:
        s += shape.replace("FILL", f'fill="#000000" opacity="0.45" filter="url(#drop)" transform="translate(0 4)"')
    if glow:
        s += shape.replace("FILL", f'fill="{glow}" opacity="0.8" filter="url(#glow)"')
    s += shape.replace("FILL", f'fill="{outline}" stroke="{outline}" stroke-width="8" stroke-linejoin="round"')
    s += shape.replace("FILL", f'fill="url(#{fill})"')
    s += shape.replace("FILL", 'fill="url(#shade)"')
    s += shape.replace("FILL", f'fill="none" stroke="{rim}" stroke-width="1.6" opacity="0.7"')
    gx, gy, rx, ry, rot = gloss
    s += f'<ellipse cx="{gx}" cy="{gy}" rx="{rx}" ry="{ry}" transform="rotate({rot} {gx} {gy})" fill="url(#gloss)" opacity="0.8"/>'
    return s


def symbol(d, width=10, color="#ffffff", outline=OUTLINE, cap="round", fill=False):
    """A fat cartoon symbol (white with a dark outline), drawn on top of a glossy body."""
    if fill:
        return (f'<path d="{d}" fill="{outline}" stroke="{outline}" stroke-width="{width}" stroke-linejoin="round"/>'
                f'<path d="{d}" fill="{color}"/>')
    return (f'<path d="{d}" fill="none" stroke="{outline}" stroke-width="{width + 7}" stroke-linecap="{cap}" stroke-linejoin="round"/>'
            f'<path d="{d}" fill="none" stroke="{color}" stroke-width="{width}" stroke-linecap="{cap}" stroke-linejoin="round"/>')


def disc_badge(fill, rim, glow, inner):
    return glossy('<circle cx="50" cy="50" r="40" FILL/>', fill, rim, glow, gloss=(36, 30, 17, 9, -24)) + inner


# ---------------------------------------------------------------- coin (8-frame spin)
def coin(angle_deg):
    """Holographic gold data-coin, rotating about its vertical axis. 0 deg = face-on."""
    c = math.cos(math.radians(angle_deg))
    sx = max(abs(c), 0.08)
    side = 1 if math.sin(math.radians(angle_deg)) >= 0 else -1
    thick = 7.5 * (1 - abs(c)) + 1.5  # visible edge thickness grows as the coin turns edge-on
    ticks = "".join(
        f'<line x1="{50 + 41.5 * math.cos(a)}" y1="{50 + 41.5 * math.sin(a)}" x2="{50 + 44 * math.cos(a)}" y2="{50 + 44 * math.sin(a)}" '
        f'stroke="#7a4300" stroke-width="1.4" opacity="0.7"/>'
        for a in [i * math.pi / 18 for i in range(36)])
    # circuit traces from the hex core out to the inner ring, ending in vias
    traces = ""
    for a0, bend in [(-60, 20), (0, -18), (60, 18), (120, -20), (180, 18), (240, -18)]:
        a = math.radians(a0)
        b = math.radians(a0 + bend)
        x1, y1 = 50 + 15 * math.cos(a), 50 + 15 * math.sin(a)
        x2, y2 = 50 + 23 * math.cos(a), 50 + 23 * math.sin(a)
        x3, y3 = 50 + 30 * math.cos(b), 50 + 30 * math.sin(b)
        traces += (f'<path d="M{x1:.1f} {y1:.1f} L{x2:.1f} {y2:.1f} L{x3:.1f} {y3:.1f}" fill="none" stroke="#7a3d00" stroke-width="3.4" stroke-linecap="round" stroke-linejoin="round"/>'
                   f'<path d="M{x1:.1f} {y1:.1f} L{x2:.1f} {y2:.1f} L{x3:.1f} {y3:.1f}" fill="none" stroke="#29e7ff" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round"/>'
                   f'<circle cx="{x3:.1f}" cy="{y3:.1f}" r="2.6" fill="#7a3d00"/><circle cx="{x3:.1f}" cy="{y3:.1f}" r="1.6" fill="#c9fbff"/>')
    hexp = " ".join(f"{50 + 14 * math.cos(math.radians(30 + 60 * i)):.2f},{50 + 14 * math.sin(math.radians(30 + 60 * i)):.2f}" for i in range(6))
    hexi = " ".join(f"{50 + 10.5 * math.cos(math.radians(30 + 60 * i)):.2f},{50 + 10.5 * math.sin(math.radians(30 + 60 * i)):.2f}" for i in range(6))
    egg = "M50 41.5 C 54.2 41.5 56.6 47.5 56.6 51.5 C 56.6 55.6 53.6 58.4 50 58.4 C 46.4 58.4 43.4 55.6 43.4 51.5 C 43.4 47.5 45.8 41.5 50 41.5 Z"
    face = f"""
      <circle cx="50" cy="50" r="45" fill="url(#goldRim)"/>
      <circle cx="50" cy="50" r="45" fill="none" stroke="#5a2e00" stroke-width="2"/>
      {ticks}
      <circle cx="50" cy="50" r="38" fill="#8a4c00"/>
      <circle cx="50" cy="50" r="36.5" fill="url(#goldFace)"/>
      <circle cx="50" cy="50" r="36.5" fill="url(#shade)" opacity="0.6"/>
      {traces}
      <polygon points="{hexp}" fill="#2a1200" stroke="#5a2e00" stroke-width="1.5"/>
      <polygon points="{hexi}" fill="none" stroke="url(#holo)" stroke-width="2.2" filter="url(#glowS)"/>
      <polygon points="{hexi}" fill="none" stroke="url(#holo)" stroke-width="1.6"/>
      <path d="{egg}" fill="#29e7ff" filter="url(#glowS)" opacity="0.9"/>
      <path d="{egg}" fill="#e8fdff"/>
      <path d="M8 50 A42 42 0 0 0 92 50" fill="none" stroke="#ff2bd6" stroke-width="2" opacity="0.55"/>
      <ellipse cx="33" cy="26" rx="17" ry="8" transform="rotate(-30 33 26)" fill="url(#gloss)" opacity="0.85"/>
      <circle cx="70" cy="22" r="2.4" fill="#ffffff" opacity="0.9"/>"""
    # the rim: the coin's silhouette stacked from the back face to the front face, so turning shows a solid edge
    edge_dx = side * thick
    steps = 10
    rim = "".join(
        f'<ellipse cx="{50 - edge_dx * 0.5 + edge_dx * i / steps:.2f}" cy="50" rx="{45 * sx:.2f}" ry="45" fill="url(#goldEdge)"/>'
        for i in range(steps + 1))
    return f"""
      <ellipse cx="50" cy="55" rx="{44 * sx + 4 + thick * 0.5}" ry="44" fill="#000000" opacity="0.4" filter="url(#drop)"/>
      <ellipse cx="50" cy="50" rx="{46 * sx + 3 + thick * 0.5}" ry="46" fill="#ffc21a" opacity="0.55" filter="url(#glow)"/>
      {rim}
      <g transform="translate({50 - edge_dx * 0.5} 50) scale({sx} 1) translate(-50 -50)">{face}</g>"""


# ---------------------------------------------------------------- UI frames (9-slice, drawn in raw 192 px cells, tinted in game)
def tech_frame():
    """Neon tech frame: chamfered top-left / bottom-right corners, double line, outer glow, dark glass inside."""
    m, c, r = 18, 22, 9
    d = f"M{m + c} {m} L{F - m - r} {m} Q{F - m} {m} {F - m} {m + r} L{F - m} {F - m - c} L{F - m - c} {F - m} L{m + r} {F - m} Q{m} {F - m} {m} {F - m - r} L{m} {m + c} Z"
    i = 6
    di = f"M{m + c + i * 0.4} {m + i} L{F - m - r - i * 0.3} {m + i} Q{F - m - i} {m + i} {F - m - i} {m + r + i * 0.3} L{F - m - i} {F - m - c - i * 0.4} L{F - m - c - i * 0.4} {F - m - i} L{m + r + i * 0.3} {F - m - i} Q{m + i} {F - m - i} {m + i} {F - m - r - i * 0.3} L{m + i} {m + c + i * 0.4} Z"
    return (f'<path d="{d}" fill="#0a0a1e" fill-opacity="0.80"/>'
            f'<path d="{d}" fill="none" stroke="#ffffff" stroke-width="8" opacity="0.55" filter="url(#glow6)"/>'
            f'<path d="{d}" fill="none" stroke="#ffffff" stroke-width="3.2"/>'
            f'<path d="{di}" fill="none" stroke="#ffffff" stroke-width="1.2" opacity="0.35"/>'
            # corner accents along the chamfers (they sit in the 9-slice corners, so they never stretch)
            f'<path d="M{m - 2} {m + c - 7} L{m + c - 7} {m - 2}" stroke="#ffffff" stroke-width="4" stroke-linecap="round"/>'
            f'<path d="M{F - m + 2} {F - m - c + 7} L{F - m - c + 7} {F - m + 2}" stroke="#ffffff" stroke-width="4" stroke-linecap="round"/>')


def glass_frame():
    m, r = 12, 26
    return (f'<rect x="{m}" y="{m}" width="{F - 2 * m}" height="{F - 2 * m}" rx="{r}" fill="url(#glassFill)"/>'
            f'<rect x="{m}" y="{m}" width="{F - 2 * m}" height="{F - 2 * m}" rx="{r}" fill="none" stroke="#ffffff" stroke-width="1.8" opacity="0.38"/>'
            f'<path d="M{m + r} {m + 3} L{F - m - r} {m + 3}" stroke="#ffffff" stroke-width="1.4" opacity="0.35" stroke-linecap="round"/>')


def pill_frame():
    m = 8
    h = F - 2 * m
    return (f'<rect x="{m}" y="{m}" width="{h}" height="{h}" rx="{h / 2}" fill="url(#glassFill)"/>'
            f'<rect x="{m}" y="{m}" width="{h}" height="{h}" rx="{h / 2}" fill="none" stroke="#ffffff" stroke-width="2" opacity="0.45"/>'
            f'<path d="M{m + h * 0.3} {m + 5} L{m + h * 0.7} {m + 5}" stroke="#ffffff" stroke-width="2" opacity="0.4" stroke-linecap="round"/>')


def flame(k):
    w = [0, 2.5, -2, 1.5][k]   # tip sway
    h = [0, -3, 1.5, -1.5][k]  # tip height
    outer = f"M50 {8 + h} C {60 + w} 24, 80 36, 78 60 C 77 79, 64 92, 50 92 C 36 92, 23 79, 22 60 C 21 45, 31 38, 34 26 C 39 34, 41 38, 44 40 C 44 28, {46 + w} 16, 50 {8 + h} Z"
    inner = f"M50 {40 + h * 0.6} C {56 + w * 0.6} 50, 66 58, 64 70 C 63 81, 57 86, 50 86 C 43 86, 37 81, 36 70 C 35 62, 42 56, 44 50 C 46 55, 48 57, 49 58 C 48 50, {49 + w * 0.5} 45, 50 {40 + h * 0.6} Z"
    core = "M50 62 C 55 68, 58 72, 57 77 C 56 82, 53 84, 50 84 C 47 84, 44 82, 43 77 C 42 72, 45 68, 50 62 Z"
    return (f'<path d="{outer}" fill="#000000" opacity="0.4" filter="url(#drop)" transform="translate(0 4)"/>'
            f'<path d="{outer}" fill="#ff2bd6" opacity="0.85" filter="url(#glow)"/>'
            f'<path d="{outer}" fill="{OUTLINE}" stroke="{OUTLINE}" stroke-width="7" stroke-linejoin="round"/>'
            f'<path d="{outer}" fill="url(#fireOut)"/>'
            f'<path d="{outer}" fill="none" stroke="#ffe0fa" stroke-width="1.5" opacity="0.6"/>'
            f'<path d="{inner}" fill="url(#fireIn)"/>'
            f'<path d="{core}" fill="#ffffff" opacity="0.9"/>'
            f'<ellipse cx="36" cy="54" rx="5" ry="11" transform="rotate(-18 36 54)" fill="url(#gloss)" opacity="0.6"/>')


# ---------------------------------------------------------------- data-cache chest (closed / cracking / open)
def chest(stage):
    base = "M14 50 L86 50 L82 88 C 82 90, 80 92, 78 92 L22 92 C 20 92, 18 90, 18 88 Z"
    lid_closed = "M12 34 C 12 22, 24 16, 50 16 C 76 16, 88 22, 88 34 L88 50 L12 50 Z"
    out = ""
    if stage == 2:  # light pours out of the open chest
        out += '<path d="M18 50 L2 0 L98 0 L82 50 Z" fill="url(#beam)" opacity="0.9"/>'
    out += glossy(f'<path d="{base}" FILL/>', "violet", "#29e7ff", "#9a5bff", gloss=(30, 62, 10, 5, -10))
    # neon trims
    out += ('<path d="M18 62 L82 62" stroke="#1a0420" stroke-width="6"/><path d="M18 62 L82 62" stroke="#29e7ff" stroke-width="2.4"/>'
            '<path d="M30 52 L30 90 M70 52 L70 90" stroke="#1a0420" stroke-width="5"/><path d="M30 52 L30 90 M70 52 L70 90" stroke="#ff2bd6" stroke-width="2"/>')
    if stage == 2:
        out += '<g transform="translate(2 -14) rotate(-22 14 40)">'
    elif stage == 1:
        out += '<g transform="translate(0 -3) rotate(-3 14 50)">'
    else:
        out += '<g>'
    out += glossy(f'<path d="{lid_closed}" FILL/>', "pink", "#29e7ff", None, gloss=(32, 26, 16, 6, -12), shadow=False)
    out += ('<path d="M12 40 L88 40" stroke="#1a0420" stroke-width="6"/><path d="M12 40 L88 40" stroke="#f4ff5a" stroke-width="2.4"/>'
            '</g>')
    # lock plate (glowing hex)
    if stage < 2:
        out += ('<rect x="40" y="42" width="20" height="22" rx="5" fill="#1a0420"/>'
                '<rect x="43" y="45" width="14" height="16" rx="3.5" fill="url(#gold)"/>'
                '<circle cx="50" cy="51" r="2.6" fill="#1a0420"/><rect x="49" y="52" width="2" height="5" fill="#1a0420"/>')
    if stage == 1:  # light leaking out of the seam
        out += ('<path d="M14 49 L86 49" stroke="#ffffff" stroke-width="3" filter="url(#glowS)"/>'
                '<path d="M30 49 L22 30 M50 49 L50 26 M70 49 L78 30" stroke="#c9fbff" stroke-width="2.4" stroke-linecap="round" filter="url(#glowS)"/>')
    if stage == 2:  # coins inside
        for cx, cy in [(38, 50), (54, 47), (66, 52), (46, 54)]:
            out += (f'<ellipse cx="{cx}" cy="{cy}" rx="9" ry="5" fill="#7a4300"/><ellipse cx="{cx}" cy="{cy - 1}" rx="8" ry="4.2" fill="url(#gold)"/>')
    return out


def check():
    return disc_badge("green", "#c9fbff", "#2bff9a", symbol("M30 51 L44 65 L71 36", 11))


def lock():
    shackle = "M34 46 L34 34 C 34 22, 66 22, 66 34 L66 46"
    return (symbol(shackle, 7, color="#c9c2dd")
            + glossy('<rect x="22" y="44" width="56" height="44" rx="10" FILL/>', "gold", "#fffbe0", None, gloss=(36, 52, 14, 5, -10))
            + '<circle cx="50" cy="63" r="6" fill="#3b1d00"/><rect x="47.5" y="64" width="5" height="12" rx="2" fill="#3b1d00"/>')


def rocket():
    body = "M50 8 C 64 20, 68 40, 66 66 L34 66 C 32 40, 36 20, 50 8 Z"
    out = ('<path d="M38 66 C 40 80, 46 90, 50 96 C 54 90, 60 80, 62 66 Z" fill="url(#fireOut)" filter="url(#glowS)"/>'
           '<path d="M42 66 C 44 76, 48 84, 50 88 C 52 84, 56 76, 58 66 Z" fill="url(#fireIn)"/>')
    out += glossy('<path d="M34 50 L20 70 L36 66 Z" FILL/>', "pink", "#ffe0fa", None, gloss=(26, 62, 3, 2, 0), shadow=False)
    out += glossy('<path d="M66 50 L80 70 L64 66 Z" FILL/>', "pink", "#ffe0fa", None, gloss=(74, 62, 3, 2, 0), shadow=False)
    out += glossy(f'<path d="{body}" FILL/>', "cyan", "#ffffff", "#29e7ff", gloss=(44, 30, 5, 12, 8))
    out += '<circle cx="50" cy="38" r="9" fill="#1a0420"/><circle cx="50" cy="38" r="6.5" fill="url(#violet)"/><circle cx="48" cy="36" r="2.2" fill="#ffffff" opacity="0.9"/>'
    return out


def egg_bolt():
    egg = "M50 8 C 70 8, 84 40, 84 60 C 84 80, 68 92, 50 92 C 32 92, 16 80, 16 60 C 16 40, 30 8, 50 8 Z"
    bolt = "M56 22 L36 56 L50 56 L44 80 L66 44 L52 44 Z"
    return glossy(f'<path d="{egg}" FILL/>', "cyan", "#ffffff", "#29e7ff", gloss=(36, 30, 14, 9, -20)) + symbol(bolt, 4, color="#f4ff5a", fill=True)


def shop():
    bag = "M18 36 L82 36 L78 88 C 78 91, 76 92, 74 92 L26 92 C 24 92, 22 91, 22 88 Z"
    return (symbol("M36 40 L36 28 C 36 14, 64 14, 64 28 L64 40", 6, color="#ffd6f7")
            + glossy(f'<path d="{bag}" FILL/>', "pink", "#29e7ff", "#ff2bd6", gloss=(34, 46, 12, 6, -8))
            + '<circle cx="50" cy="64" r="15" fill="#1a0420"/><circle cx="50" cy="64" r="12.5" fill="url(#gold)"/>'
            + '<circle cx="50" cy="64" r="8" fill="none" stroke="#9a5400" stroke-width="1.6"/><ellipse cx="46" cy="59" rx="5" ry="2.6" fill="#ffffff" opacity="0.7"/>')


def locker():
    hanger = "M50 30 C 50 24, 58 24, 58 18 C 58 12, 50 10, 46 14"
    return (symbol(hanger, 5, color="#e6e0f5")
            + glossy('<path d="M50 30 L88 62 C 92 66, 90 72, 84 72 L16 72 C 10 72, 8 66, 12 62 Z" FILL/>', "cyan", "#ffffff", "#29e7ff", gloss=(40, 48, 14, 4, -40))
            + '<path d="M22 66 L78 66" stroke="#0a6f92" stroke-width="3" opacity="0.6"/>')


def target():
    return (glossy('<circle cx="50" cy="52" r="40" FILL/>', "red", "#ffe0e6", "#ff2b4a")
            + '<circle cx="50" cy="52" r="28" fill="#fff0f3"/><circle cx="50" cy="52" r="20" fill="url(#red)"/>'
            + '<circle cx="50" cy="52" r="11" fill="#fff0f3"/><circle cx="50" cy="52" r="5" fill="url(#red)"/>'
            + symbol("M52 50 L84 18", 4, color="#f4ff5a")
            + '<path d="M80 10 L90 8 L88 18 L82 22 Z" fill="#29e7ff" stroke="#1a0420" stroke-width="2.4" stroke-linejoin="round"/>')


def trophy():
    cup = "M26 14 L74 14 L72 40 C 70 56, 60 62, 50 62 C 40 62, 30 56, 28 40 Z"
    return (symbol("M28 22 C 12 22, 12 44, 30 46 M72 22 C 88 22, 88 44, 70 46", 5, color="#ffd23a")
            + glossy('<path d="M40 60 L60 60 L62 76 L38 76 Z" FILL/>', "gold", "#fffbe0", None, gloss=(46, 66, 4, 2, 0), shadow=False)
            + glossy('<rect x="28" y="76" width="44" height="14" rx="4" FILL/>', "violet", "#29e7ff", None, gloss=(40, 80, 8, 2, 0))
            + glossy(f'<path d="{cup}" FILL/>', "gold", "#fffbe0", "#ffc21a", gloss=(38, 26, 8, 12, 0))
            + symbol("M50 24 L53.5 33 L63 33 L55.5 38.5 L58.5 48 L50 42 L41.5 48 L44.5 38.5 L37 33 L46.5 33 Z", 2.5, color="#ffffff", fill=True))


def share():
    return disc_badge("cyan", "#ffffff", "#29e7ff", symbol("M50 64 L50 26 M36 38 L50 24 L64 38 M30 52 L30 72 L70 72 L70 52", 7))


def gift():
    box = '<rect x="16" y="44" width="68" height="46" rx="6" FILL/>'
    lid = '<rect x="10" y="32" width="80" height="16" rx="5" FILL/>'
    return (symbol("M50 32 C 40 14, 24 18, 30 28 C 34 34, 46 32, 50 32 C 54 32, 66 34, 70 28 C 76 18, 60 14, 50 32", 5, color="#2bff9a")
            + glossy(box, "pink", "#29e7ff", "#ff2bd6", gloss=(32, 56, 10, 4, 0))
            + glossy(lid, "pink", "#ffe0fa", None, gloss=(30, 37, 14, 3, 0), shadow=False)
            + '<rect x="44" y="32" width="12" height="58" fill="#1a0420"/><rect x="46" y="32" width="8" height="58" fill="url(#green)"/>')


def star():
    d = "M50 8 L61 36 L91 37 L67 56 L76 86 L50 69 L24 86 L33 56 L9 37 L39 36 Z"
    return glossy(f'<path d="{d}" FILL/>', "gold", "#fffbe0", "#ffc21a", gloss=(40, 34, 12, 7, -20))


def bolt():
    d = "M60 6 L24 56 L46 56 L38 94 L78 40 L56 40 Z"
    return glossy(f'<path d="{d}" FILL/>', "gold", "#fffbe0", "#f4ff5a", gloss=(44, 34, 5, 12, 30))


def bell():
    body = "M50 12 C 68 12, 74 28, 74 46 L76 66 L86 76 L14 76 L24 66 L26 46 C 26 28, 32 12, 50 12 Z"
    return (glossy('<circle cx="50" cy="82" r="9" FILL/>', "gold", "#fffbe0", None, gloss=(47, 79, 3, 2, 0), shadow=False)
            + glossy(f'<path d="{body}" FILL/>', "gold", "#fffbe0", "#ffc21a", gloss=(38, 30, 7, 13, 10)))


def replay():
    cam = '<rect x="10" y="28" width="58" height="46" rx="10" FILL/>'
    lens = '<path d="M68 42 L90 30 L90 72 L68 60 Z" FILL/>'
    return (glossy(lens, "slate", "#29e7ff", None, gloss=(80, 40, 3, 6, 0))
            + glossy(cam, "slate", "#29e7ff", "#29e7ff", gloss=(26, 36, 12, 5, -10))
            + '<circle cx="28" cy="51" r="9" fill="#ff2b4a" filter="url(#glowS)"/><circle cx="28" cy="51" r="7" fill="url(#red)"/>'
            + '<circle cx="26" cy="49" r="2.2" fill="#ffffff" opacity="0.9"/>'
            + '<rect x="42" y="44" width="18" height="4" rx="2" fill="#29e7ff" opacity="0.8"/><rect x="42" y="54" width="12" height="4" rx="2" fill="#29e7ff" opacity="0.5"/>')


def plus():
    return disc_badge("green", "#c9fbff", "#2bff9a", symbol("M50 30 L50 70 M30 50 L70 50", 11))


def back():
    return disc_badge("cyan", "#ffffff", "#29e7ff", symbol("M58 30 L38 50 L58 70", 11))


def settings():
    # a chunky 8-tooth gear: one closed outline (teeth + body), glossy cyan, dark hub
    pts = []
    teeth, r_out, r_in = 8, 44, 34
    for i in range(teeth):
        a0 = 2 * math.pi * i / teeth
        w_root, w_tip = 0.25, 0.17  # half-widths (radians) at the root and the tip of each tooth
        for ang, r in ((a0 - w_root, r_in), (a0 - w_tip, r_out), (a0 + w_tip, r_out), (a0 + w_root, r_in)):
            pts.append(f"{50 + r * math.cos(ang):.2f} {50 + r * math.sin(ang):.2f}")
        mid = a0 + math.pi / teeth
        pts.append(f"{50 + r_in * math.cos(mid):.2f} {50 + r_in * math.sin(mid):.2f}")
    d = "M" + " L".join(pts) + " Z"
    return (glossy(f'<path d="{d}" FILL/>', "cyan", "#ffffff", "#29e7ff", gloss=(36, 32, 14, 8, -30))
            + '<circle cx="50" cy="50" r="15" fill="#1a0420"/><circle cx="50" cy="50" r="11" fill="url(#slate)"/>'
            + '<circle cx="47" cy="47" r="3" fill="#ffffff" opacity="0.5"/>')


def png(name, span=1, pad=8, fill=False, glow=0.0):
    """A sprite from assets/hud/shop/<name>.png placed in its cell(s) in raw cell pixels. fill=True stretches it to
    the padded cell (9-slice frames, 2:1 buttons); otherwise it keeps its aspect, centred. glow bakes a soft halo of
    the sprite's own colours behind it at that opacity."""
    data = base64.b64encode(open(os.path.join(SHOP, name + ".png"), "rb").read()).decode()
    w, h = F * span - 2 * pad, F - 2 * pad
    aspect = "none" if fill else "xMidYMid meet"
    img = f'x="{pad}" y="{pad}" width="{w}" height="{h}" preserveAspectRatio="{aspect}" href="data:image/png;base64,{data}"'
    halo = f'<image {img} filter="url(#pngGlow)" opacity="{glow}"/>' if glow > 0 else ""
    return halo + f"<image {img}/>"


# order defines the Icon enum; frames of one animation are consecutive
ICONS = [("Coin", [coin(a) for a in (0, 22.5, 45, 67.5, 90, 112.5, 135, 157.5)]),
         ("Flame", [flame(k) for k in range(4)]),
         ("ChestClosed", [chest(0)]), ("ChestCrack", [chest(1)]), ("ChestOpen", [chest(2)]),
         ("Check", [check()]), ("Lock", [lock()]), ("Rocket", [rocket()]), ("EggBolt", [egg_bolt()]),
         ("Shop", [shop()]), ("Locker", [locker()]), ("Missions", [target()]), ("Trophy", [trophy()]),
         ("Share", [share()]), ("AdBadge", [png("tv_ad", pad=26, glow=0.5)], {"raw": True}), ("NoAds", [png("tv_noads", pad=22, glow=0.4)], {"raw": True}), ("Gift", [gift()]),
         ("Star", [star()]), ("Bolt", [bolt()]), ("Bell", [bell()]), ("Replay", [replay()]), ("Plus", [plus()]),
         ("Close", [png("close", pad=14, glow=0.45)], {"raw": True}), ("Back", [back()]), ("Settings", [settings()]),
         ("CoinGreen", [png("coin_green", pad=16, glow=0.75)], {"raw": True}),
         ("Chest3D", [png("chest", pad=10, glow=0.6)], {"raw": True}),
         ("MenuReplay", [png("menu_replay", pad=12, glow=0.55)], {"raw": True}),
         ("MenuShop", [png("menu_shop", pad=8, glow=0.55)], {"raw": True}),
         ("MenuLocker", [png("menu_locker", pad=10, glow=0.55)], {"raw": True}),
         ("MenuMissions", [png("menu_missions", pad=12, glow=0.55)], {"raw": True}),
         ("MenuFrame", [png("menu_frame", pad=12, glow=0.55)], {"raw": True}),
         ("FrameTech", [tech_frame()], {"raw": True}), ("FrameGlass", [glass_frame()], {"raw": True}),
         ("FramePill", [pill_frame()], {"raw": True}),
         ("Pile300", [png("pile_300", 2, 12, glow=0.45)], {"span": 2, "raw": True}),
         ("Pile1000", [png("pile_1000", 2, 10, glow=0.45)], {"span": 2, "raw": True}),
         ("Pile1800", [png("pile_1800", 2, 8, glow=0.45)], {"span": 2, "raw": True}),
         ("Pile4000", [png("pile_4000", 2, 6, glow=0.45)], {"span": 2, "raw": True}),
         ("Pile9000", [png("pile_9000", 2, 6, glow=0.45)], {"span": 2, "raw": True}),
         # 3-slice / 9-slice sprites, 2:1 in two cells; drawn with hudNineSlice(slice 0.5, corner = height / 2)
         ("FrameFree", [png("frame_free", 2, 10, fill=True, glow=0.7)], {"span": 2, "raw": True}),
         ("FrameNeon", [png("frame_neon", 2, 10, fill=True, glow=0.7)], {"span": 2, "raw": True}),
         ("BtnBlue", [png("btn_blue", 2, 6, fill=True)], {"span": 2, "raw": True}),
         ("BtnGold", [png("btn_gold", 2, 6, fill=True)], {"span": 2, "raw": True}),
         ("BtnDark", [png("btn_dark", 2, 6, fill=True)], {"span": 2, "raw": True}),
         ("RibbonBest", [png("ribbon_best", 2, 4)], {"span": 2, "raw": True})]


def place(i, content, scale=0.74, span=1, raw=False):
    x, y = (i % COLS) * F, (i // COLS) * F
    if raw:  # drawn in the cell's own 192 px coordinates (9-slice frames)
        return f'<g transform="translate({x} {y})">{content}</g>'
    s = F / 100 * scale
    offx, offy = (F * span - 100 * span * s) / 2, (F - 100 * s) / 2
    return f'<g transform="translate({x + offx} {y + offy}) scale({s})">{content}</g>'


def main():
    if not os.path.exists(CHROME):
        sys.exit("Google Chrome not found (set CHROME=/path/to/chrome)")
    frames, enum, idx = [], [], 0
    for item in ICONS:
        name, art = item[0], item[1]
        opts = item[2] if len(item) > 2 else {}
        span = opts.get("span", 1)
        if idx % COLS + span > COLS:  # wide sprites never wrap across rows
            idx += COLS - idx % COLS
        enum.append((name, idx, len(art), span))
        for a in art:
            scale = 0.66 if name.startswith("Chest") else 0.9 if span > 1 else 0.74
            frames.append(place(idx, a, scale, span, opts.get("raw", False)))
            idx += span
    rows = (idx + COLS - 1) // COLS
    W, H = COLS * F, rows * F
    svg = f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}">{DEFS}{"".join(frames)}</svg>'
    os.makedirs(os.path.dirname(OUT_PNG), exist_ok=True)
    with tempfile.TemporaryDirectory() as tmp:
        path = os.path.join(tmp, "icons.svg")
        open(path, "w").write(svg)
        subprocess.run([CHROME, "--headless=new", "--disable-gpu", "--hide-scrollbars", "--force-device-scale-factor=1",
                        f"--window-size={W},{H}", "--default-background-color=00000000", f"--screenshot={OUT_PNG}",
                        "file://" + path], check=True, capture_output=True)
    lines = ["// Generated by scripts/hud/make_hud_icons.py; do not edit. Frame indices into assets/hud/hud_icons.png.",
             "#pragma once", "", "namespace cs {", "",
             f"constexpr int kIconCols = {COLS}, kIconRows = {rows};", "",
             "enum class Icon : int {"]
    for name, i, n, span in enum:
        lines.append(f"    {name} = {i},")
    lines += ["};", ""]
    for name, i, n, span in enum:
        if n > 1:
            lines.append(f"constexpr int k{name}Frames = {n};")
    lines += ["", "// sprites wider than one cell (coin piles, shop buttons and frames)", "constexpr int iconSpan(Icon i) {", "    switch (i) {"]
    for name, i, n, span in enum:
        if span > 1:
            lines.append(f"    case Icon::{name}: return {span};")
    lines += ["    default: return 1;", "    }", "}"]
    lines += ["", "} // namespace cs", ""]
    open(OUT_H, "w").write("\n".join(lines))
    print("wrote", OUT_PNG, f"({W}x{H}, {idx} frames) and", OUT_H)


if __name__ == "__main__":
    main()
