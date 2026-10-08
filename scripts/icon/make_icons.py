#!/usr/bin/env python3
"""Cuckoo Stack app icon: one vector design rendered to every iOS and Android icon asset.

    python3 scripts/icon/make_icons.py          (needs Google Chrome for rendering, Pillow for resizing)

Writes:
  platforms/ios/Assets.xcassets/AppIcon.appiconset   1024 single-size universal icon + iOS 18 dark and tinted variants
  platforms/android/app/src/main/res/mipmap-*        legacy square + round PNGs (API 24-25), adaptive layers (API 26+)
  platforms/android/app/src/main/res/mipmap-anydpi-v26/ic_launcher(.xml|_round.xml)  adaptive icon + themed (API 33)
  platforms/android/play_store_icon_512.png         Google Play listing icon
  scripts/icon/out/                                 1024 masters (preview / store marketing)
"""
import json
import os
import shutil
import subprocess
import sys
import tempfile

from PIL import Image, ImageDraw

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
OUT = os.path.join(ROOT, "scripts", "icon", "out")
CHROME = os.environ.get("CHROME", "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome")
S = 1024  # master size

# game palette (src/core: --neon, --hot, --volt, ink)
NEON, HOT, VOLT = "#29e7ff", "#ff2bd6", "#f4ff5a"


def background():
    grid = []
    horizon = 700
    for i in range(-9, 10):  # perspective floor lines converging on the horizon
        grid.append(f'<line x1="{512 + i * 34}" y1="{horizon}" x2="{512 + i * 190}" y2="1024"/>')
    y, step = horizon + 10, 14
    while y < 1024:
        grid.append(f'<line x1="0" y1="{y:.0f}" x2="1024" y2="{y:.0f}"/>')
        step *= 1.32
        y += step
    towers = ""
    for x, w, h in [(40, 120, 330), (175, 90, 250), (760, 110, 300), (880, 120, 380)]:
        towers += f'<rect x="{x}" y="{horizon - h}" width="{w}" height="{h}" fill="#1a0b3a"/>'
        for wy in range(horizon - h + 26, horizon - 20, 46):
            for wx in range(x + 16, x + w - 20, 34):
                if (wx * 7 + wy * 3) % 5 < 2:
                    c = [NEON, HOT, "#ffcf8a"][(wx + wy) % 3]
                    towers += f'<rect x="{wx}" y="{wy}" width="16" height="20" fill="{c}" opacity="0.55"/>'
    return f"""
    <defs>
      <radialGradient id="sky" cx="50%" cy="38%" r="75%">
        <stop offset="0" stop-color="#4a1580"/><stop offset="0.55" stop-color="#1c0840"/><stop offset="1" stop-color="#0a0418"/>
      </radialGradient>
      <linearGradient id="haze" x1="0" y1="0" x2="0" y2="1">
        <stop offset="0" stop-color="{HOT}" stop-opacity="0"/><stop offset="1" stop-color="{HOT}" stop-opacity="0.55"/>
      </linearGradient>
      <linearGradient id="floor" x1="0" y1="0" x2="0" y2="1">
        <stop offset="0" stop-color="#2a0a4a"/><stop offset="1" stop-color="#07030f"/>
      </linearGradient>
    </defs>
    <rect width="1024" height="1024" fill="url(#sky)"/>
    {towers}
    <rect y="560" width="1024" height="140" fill="url(#haze)"/>
    <rect y="700" width="1024" height="324" fill="url(#floor)"/>
    <g stroke="{NEON}" stroke-width="3" opacity="0.38">{''.join(grid)}</g>
    <rect y="697" width="1024" height="6" fill="{HOT}" opacity="0.9"/>
    """


def sparkle(x, y, r, color):
    return (f'<path d="M{x} {y - r} Q{x} {y} {x + r} {y} Q{x} {y} {x} {y + r} Q{x} {y} {x - r} {y} Q{x} {y} {x} {y - r}Z" '
            f'fill="{color}" filter="url(#glowS)"/>')


def foreground(mono=False, sparkles=True):
    """Hen on a two-egg stack, centred on (512, 512), ~700 px tall. mono: flat white silhouette (themed/tinted)."""
    W = "#ffffff"
    if mono:  # white silhouette with the visor and egg rings cut out in black (luminance carries the detail)
        egg = body = head = W
        visor, beak, comb, tail = "#000000", W, W, W
    else:
        egg, body, head = "url(#eggG)", "url(#henG)", "url(#henG)"
        visor, beak, comb, tail = "#0b0716", "#ffb020", HOT, HOT
    rings = """
      <ellipse cx="512" cy="792" rx="114" ry="26" fill="none" stroke="#000" stroke-width="14"/>
      <ellipse cx="512" cy="578" rx="114" ry="26" fill="none" stroke="#000" stroke-width="14"/>""" if mono else f"""
      <ellipse cx="512" cy="792" rx="114" ry="26" fill="none" stroke="{NEON}" stroke-width="16" filter="url(#glow)"/>
      <ellipse cx="512" cy="792" rx="114" ry="26" fill="none" stroke="#e8fdff" stroke-width="6"/>
      <ellipse cx="512" cy="578" rx="114" ry="26" fill="none" stroke="{HOT}" stroke-width="16" filter="url(#glow)"/>
      <ellipse cx="512" cy="578" rx="114" ry="26" fill="none" stroke="#ffe3fa" stroke-width="6"/>"""
    visor_glow = "" if mono else f"""
      <rect x="606" y="232" width="122" height="20" rx="10" fill="{NEON}" filter="url(#glow)"/>
      <rect x="616" y="237" width="100" height="10" rx="5" fill="#e8fdff"/>"""
    back_glow = "" if mono else f'<ellipse cx="512" cy="540" rx="300" ry="390" fill="{HOT}" opacity="0.22" filter="url(#bigGlow)"/>'
    pad = "" if mono else f'<ellipse cx="512" cy="905" rx="190" ry="34" fill="{NEON}" opacity="0.55" filter="url(#glow)"/>'
    spark = ""
    if sparkles and not mono:
        spark = sparkle(210, 250, 34, VOLT) + sparkle(820, 460, 26, NEON) + sparkle(255, 640, 20, HOT) + sparkle(800, 170, 18, "#ffffff")
    eggs = f"""
      <ellipse cx="512" cy="790" rx="112" ry="128" fill="{egg}"/>
      <ellipse cx="512" cy="566" rx="112" ry="128" fill="{egg}"/>"""
    hen = f"""
      <path d="M332 330 Q250 250 268 190 Q312 240 352 262 Q318 190 360 150 Q378 228 404 262 Z" fill="{tail}"/>
      <ellipse cx="478" cy="352" rx="178" ry="128" fill="{body}"/>
      <circle cx="636" cy="250" r="96" fill="{head}"/>
      <circle cx="604" cy="152" r="26" fill="{comb}"/><circle cx="642" cy="140" r="30" fill="{comb}"/><circle cx="680" cy="156" r="24" fill="{comb}"/>
      <path d="M720 268 L790 290 L720 314 Z" fill="{beak}"/>
      <rect x="596" y="218" width="142" height="48" rx="24" fill="{visor}"/>
      {visor_glow}
      <path d="M420 352 Q470 300 560 334 Q520 410 430 398 Z" fill="{'#ffffff' if mono else '#d9d2f2'}"/>
      <rect x="456" y="440" width="16" height="44" rx="8" fill="{beak}"/>
      <rect x="532" y="440" width="16" height="44" rx="8" fill="{beak}"/>"""
    defs = "" if mono else f"""
    <defs>
      <radialGradient id="eggG" cx="38%" cy="32%" r="80%">
        <stop offset="0" stop-color="#ffffff"/><stop offset="0.6" stop-color="#efe8ff"/><stop offset="1" stop-color="#b9a8e6"/>
      </radialGradient>
      <radialGradient id="henG" cx="40%" cy="30%" r="85%">
        <stop offset="0" stop-color="#ffffff"/><stop offset="0.7" stop-color="#f1ecff"/><stop offset="1" stop-color="#c3b6ea"/>
      </radialGradient>
    </defs>"""
    return defs + back_glow + pad + eggs + rings + hen + spark


FILTERS = """
<defs>
  <filter id="glow" x="-50%" y="-50%" width="200%" height="200%"><feGaussianBlur stdDeviation="9"/></filter>
  <filter id="glowS" x="-80%" y="-80%" width="260%" height="260%"><feGaussianBlur stdDeviation="2" result="b"/>
    <feMerge><feMergeNode in="b"/><feMergeNode in="SourceGraphic"/></feMerge></filter>
  <filter id="bigGlow" x="-50%" y="-50%" width="200%" height="200%"><feGaussianBlur stdDeviation="60"/></filter>
</defs>"""


def svg(body):
    return f'<svg xmlns="http://www.w3.org/2000/svg" width="{S}" height="{S}" viewBox="0 0 1024 1024">{FILTERS}{body}</svg>'


def group(content, scale):
    """Scale foreground about the canvas centre (Android keeps it inside the 66/108 safe zone)."""
    t = 512 * (1 - scale)
    return f'<g transform="translate({t} {t}) scale({scale})">{content}</g>'


VARIANTS = {
    # iOS: artwork fills the square; the system applies the rounded mask
    "ios_any": svg(background() + group(foreground(), 0.92)),
    "ios_dark": svg(group(foreground(), 0.92)),                                  # transparent: system supplies the dark ground
    "ios_tinted": svg(group(foreground(mono=True, sparkles=False), 0.92)),       # grayscale: system applies the tint
    # Android adaptive (108 dp canvas, 72 dp visible, 66 dp safe): foreground scaled into the safe circle
    "android_bg": svg(background()),
    "android_fg": svg(group(foreground(), 0.72)),  # artwork half-extent ~415 px * 0.72 < 313 px safe radius
    "android_mono": svg(group(foreground(mono=True, sparkles=False), 0.72)),
    # legacy launcher / Play: full artwork, slightly smaller so the round mask doesn't clip the comb
    "legacy": svg(background() + group(foreground(), 0.84)),
}


def render(name, markup, tmp):
    path = os.path.join(tmp, name + ".svg")
    with open(path, "w") as f:
        f.write(markup)
    png = os.path.join(OUT, name + ".png")
    subprocess.run([CHROME, "--headless=new", "--disable-gpu", "--hide-scrollbars", "--force-device-scale-factor=1",
                    f"--window-size={S},{S}", "--default-background-color=00000000", f"--screenshot={png}", "file://" + path],
                   check=True, capture_output=True)
    img = Image.open(png).convert("RGBA")
    if img.size != (S, S):
        sys.exit(f"{name}: Chrome rendered {img.size}, expected {S}x{S}")
    return img


def resized(img, px):
    return img.resize((px, px), Image.LANCZOS)


def opaque(img):
    """App Store / Play icons must not have alpha."""
    bg = Image.new("RGB", img.size, (10, 4, 24))
    bg.paste(img, mask=img.split()[3])
    return bg


def round_mask(img):
    m = Image.new("L", (img.width * 4, img.height * 4), 0)
    ImageDraw.Draw(m).ellipse((0, 0, m.width - 1, m.height - 1), fill=255)
    m = m.resize(img.size, Image.LANCZOS)
    out = img.copy()
    out.putalpha(m)
    return out


def main():
    if not os.path.exists(CHROME):
        sys.exit("Google Chrome not found (set CHROME=/path/to/chrome)")
    os.makedirs(OUT, exist_ok=True)
    with tempfile.TemporaryDirectory() as tmp:
        imgs = {k: render(k, v, tmp) for k, v in VARIANTS.items()}

    # ---- iOS: single-size universal AppIcon (Xcode 14+, all iPhone/iPad sizes derived) + iOS 18 appearances
    ios = os.path.join(ROOT, "platforms", "ios", "Assets.xcassets", "AppIcon.appiconset")
    os.makedirs(ios, exist_ok=True)
    opaque(imgs["ios_any"]).save(os.path.join(ios, "AppIcon-1024.png"))
    imgs["ios_dark"].save(os.path.join(ios, "AppIcon-1024-dark.png"))
    tinted = imgs["ios_tinted"].convert("LA").convert("RGBA")
    tinted.save(os.path.join(ios, "AppIcon-1024-tinted.png"))
    with open(os.path.join(ios, "Contents.json"), "w") as f:
        json.dump({"images": [
            {"filename": "AppIcon-1024.png", "idiom": "universal", "platform": "ios", "size": "1024x1024"},
            {"appearances": [{"appearance": "luminosity", "value": "dark"}], "filename": "AppIcon-1024-dark.png",
             "idiom": "universal", "platform": "ios", "size": "1024x1024"},
            {"appearances": [{"appearance": "luminosity", "value": "tinted"}], "filename": "AppIcon-1024-tinted.png",
             "idiom": "universal", "platform": "ios", "size": "1024x1024"}],
            "info": {"author": "xcode", "version": 1}}, f, indent=2)
    with open(os.path.join(ROOT, "platforms", "ios", "Assets.xcassets", "Contents.json"), "w") as f:
        json.dump({"info": {"author": "xcode", "version": 1}}, f, indent=2)

    # ---- Android
    res = os.path.join(ROOT, "platforms", "android", "app", "src", "main", "res")
    # themed icon (API 33): only alpha matters, so the black cut-outs become holes
    lum = imgs["android_mono"].convert("L")
    alpha = Image.composite(lum, Image.new("L", lum.size, 0), imgs["android_mono"].split()[3])
    mono_alpha = Image.new("RGBA", lum.size, (255, 255, 255, 0))
    mono_alpha.putalpha(alpha)
    densities = {"mdpi": 1, "hdpi": 1.5, "xhdpi": 2, "xxhdpi": 3, "xxxhdpi": 4}
    for d, k in densities.items():
        folder = os.path.join(res, f"mipmap-{d}")
        os.makedirs(folder, exist_ok=True)
        legacy = resized(imgs["legacy"], int(48 * k))
        rounded = legacy.copy()
        # square legacy icon with the standard rounded corners
        m = Image.new("L", (legacy.width * 4, legacy.height * 4), 0)
        ImageDraw.Draw(m).rounded_rectangle((0, 0, m.width - 1, m.height - 1), radius=int(m.width * 0.18), fill=255)
        rounded.putalpha(m.resize(legacy.size, Image.LANCZOS))
        rounded.save(os.path.join(folder, "ic_launcher.png"))
        round_mask(legacy).save(os.path.join(folder, "ic_launcher_round.png"))
        resized(imgs["android_bg"], int(108 * k)).save(os.path.join(folder, "ic_launcher_background.png"))
        resized(imgs["android_fg"], int(108 * k)).save(os.path.join(folder, "ic_launcher_foreground.png"))
        resized(mono_alpha, int(108 * k)).save(os.path.join(folder, "ic_launcher_monochrome.png"))
    anydpi = os.path.join(res, "mipmap-anydpi-v26")
    os.makedirs(anydpi, exist_ok=True)
    adaptive = """<?xml version="1.0" encoding="utf-8"?>
<adaptive-icon xmlns:android="http://schemas.android.com/apk/res/android">
    <background android:drawable="@mipmap/ic_launcher_background" />
    <foreground android:drawable="@mipmap/ic_launcher_foreground" />
    <monochrome android:drawable="@mipmap/ic_launcher_monochrome" />
</adaptive-icon>
"""
    for n in ("ic_launcher.xml", "ic_launcher_round.xml"):
        with open(os.path.join(anydpi, n), "w") as f:
            f.write(adaptive)
    opaque(resized(imgs["legacy"], 512)).save(os.path.join(ROOT, "platforms", "android", "play_store_icon_512.png"))
    print("icons written")


if __name__ == "__main__":
    main()
