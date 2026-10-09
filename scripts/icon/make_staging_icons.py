#!/usr/bin/env python3
"""Staging app icons: the normal icons with a neon "STAGING" ribbon across the bottom-right corner, so the staging
build (separate app ID, installs next to the real one) is obvious on the home screen.

    python3 scripts/icon/make_staging_icons.py      (needs Pillow; run after scripts/icon/make_icons.py)

Writes platforms/ios/Assets.xcassets/AppIconStaging.appiconset (picked by CMake when CS_ENV=staging) and
platforms/android/app/src/staging/res/mipmap-* (the staging flavour's resources override main's).
"""
import json
import os
import shutil

from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
FONT = os.path.join(ROOT, "assets", "fonts", "ChakraPetch-Bold.ttf")


def ribbon(img, inset=0.0):
    """Draw the ribbon on an RGBA image. `inset` keeps it inside the adaptive-icon safe zone (Android foreground)."""
    w, h = img.size
    s = w
    band = Image.new("RGBA", (int(s * 2), int(s * 0.15)), (0, 0, 0, 0))
    d = ImageDraw.Draw(band)
    d.rectangle([0, 0, band.width, band.height], fill=(255, 43, 214, 255))
    d.rectangle([0, 0, band.width, int(band.height * 0.12)], fill=(255, 156, 238, 255))
    d.rectangle([0, int(band.height * 0.88), band.width, band.height], fill=(122, 0, 99, 255))
    font = ImageFont.truetype(FONT, int(band.height * 0.6))
    text = "STAGING"
    tw = d.textlength(text, font=font)
    d.text(((band.width - tw) / 2, band.height * 0.13), text, font=font, fill=(255, 255, 255, 255))
    band = band.rotate(45, resample=Image.BICUBIC, expand=True)
    glow = band.filter(ImageFilter.GaussianBlur(s * 0.02))
    # centre the band on a point near the bottom-right corner
    cx, cy = w * (0.81 - inset * 0.6), h * (0.81 - inset * 0.6)
    pos = (int(cx - band.width / 2), int(cy - band.height / 2))
    layer = Image.new("RGBA", img.size, (0, 0, 0, 0))
    layer.paste(glow, pos, glow)
    layer.paste(band, pos, band)
    out = img.convert("RGBA")
    out.alpha_composite(layer)
    return out


def ios():
    src = os.path.join(ROOT, "platforms", "ios", "Assets.xcassets", "AppIcon.appiconset")
    dst = os.path.join(ROOT, "platforms", "ios", "Assets.xcassets", "AppIconStaging.appiconset")
    os.makedirs(dst, exist_ok=True)
    contents = json.load(open(os.path.join(src, "Contents.json")))
    for im in contents["images"]:
        f = im.get("filename")
        if not f:
            continue
        img = Image.open(os.path.join(src, f))
        mode = img.mode
        out = ribbon(img)
        if mode in ("RGB", "L"):  # the light icon must stay opaque for App Store Connect
            out = out.convert("RGB")
        out.save(os.path.join(dst, f))
    json.dump(contents, open(os.path.join(dst, "Contents.json"), "w"), indent=2)
    print("wrote", dst)


def android():
    res = os.path.join(ROOT, "platforms", "android", "app", "src", "main", "res")
    out_res = os.path.join(ROOT, "platforms", "android", "app", "src", "staging", "res")
    for d in sorted(os.listdir(res)):
        if not d.startswith("mipmap"):
            continue
        for f in os.listdir(os.path.join(res, d)):
            src = os.path.join(res, d, f)
            os.makedirs(os.path.join(out_res, d), exist_ok=True)
            if f.endswith(".xml"):
                shutil.copy(src, os.path.join(out_res, d, f))
                continue
            if "background" in f or "monochrome" in f:
                continue  # unchanged layers come from main
            img = Image.open(src)
            # adaptive foreground: keep the ribbon inside the 66% safe zone so masks don't cut it off
            ribbon(img, inset=0.17 if "foreground" in f else 0.0).save(os.path.join(out_res, d, f))
    print("wrote", out_res)


if __name__ == "__main__":
    ios()
    android()
