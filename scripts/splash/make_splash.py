#!/usr/bin/env python3
"""Opening splash: the Aerosphere Games logo, centred on the logo's own background colour.

    python3 scripts/splash/make_splash.py      (needs Pillow + numpy)

Source: assets/splash/aerosphere_logo_source.jpg (dark logo). The background colour is measured from the image edges,
JPEG noise around it is snapped to that exact colour (soft blend, so glows fade cleanly), and the logo is cropped to its
content plus a margin. Outputs:
  iOS      platforms/ios/Assets.xcassets/SplashLogo.imageset   @1x/@2x/@3x for a 520 pt maximum width
           platforms/ios/Assets.xcassets/SplashBackground.colorset   the splash background colour
  Android  res/values/splash_colors.xml                        @color/splash_bg
  Android  res/drawable-nodpi/splash_logo.png                  (in-app overlay, scaled by the ImageView)
           res/drawable-xxxhdpi/splash_logo_window.png         (launch window background, 300 dp wide)
"""
import json
import os

import numpy as np
from PIL import Image

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
SRC = os.path.join(ROOT, "assets", "splash", "aerosphere_logo_source.jpg")
IOS_MAX_PT = 520          # logo width cap in points (iPad / landscape)
ANDROID_WINDOW_DP = 300   # logo width on the Android 7-11 launch window


def prepare():
    a = np.asarray(Image.open(SRC).convert("RGB")).astype(np.float32)
    edges = np.concatenate([a[:40].reshape(-1, 3), a[-40:].reshape(-1, 3), a[:, :40].reshape(-1, 3), a[:, -40:].reshape(-1, 3)])
    bg = np.round(np.median(edges, axis=0))
    # snap near-background pixels to the exact colour; blend over a small band so glows stay smooth
    d = np.abs(a - bg).sum(axis=2, keepdims=True)
    w = np.clip((d - 6.0) / 14.0, 0.0, 1.0)
    a = bg + (a - bg) * w
    ys, xs = np.where(d[..., 0] > 30)
    pad = int(0.04 * (xs.max() - xs.min()))
    box = (max(0, xs.min() - pad), max(0, ys.min() - pad), min(a.shape[1], xs.max() + pad + 1), min(a.shape[0], ys.max() + pad + 1))
    return Image.fromarray(np.clip(a, 0, 255).astype(np.uint8)).crop(box), tuple(int(c) for c in bg)


def width(img, w):
    return img.resize((w, round(img.height * w / img.width)), Image.LANCZOS)


def main():
    logo, bg = prepare()
    hexbg = "#%02X%02X%02X" % bg
    print("logo", logo.size, "background", hexbg)
    cs_dir = os.path.join(ROOT, "platforms", "ios", "Assets.xcassets", "SplashBackground.colorset")
    os.makedirs(cs_dir, exist_ok=True)
    with open(os.path.join(cs_dir, "Contents.json"), "w") as f:
        json.dump({"colors": [{"color": {"color-space": "srgb", "components": {
            "red": f"0x{bg[0]:02X}", "green": f"0x{bg[1]:02X}", "blue": f"0x{bg[2]:02X}", "alpha": "1.000"}}, "idiom": "universal"}],
            "info": {"author": "xcode", "version": 1}}, f, indent=2)
    ios = os.path.join(ROOT, "platforms", "ios", "Assets.xcassets", "SplashLogo.imageset")
    os.makedirs(ios, exist_ok=True)
    images = []
    for scale in (1, 2, 3):
        name = f"SplashLogo@{scale}x.png"
        width(logo, min(logo.width, IOS_MAX_PT * scale)).save(os.path.join(ios, name), optimize=True)
        images.append({"filename": name, "idiom": "universal", "scale": f"{scale}x"})
    with open(os.path.join(ios, "Contents.json"), "w") as f:
        json.dump({"images": images, "info": {"author": "xcode", "version": 1}}, f, indent=2)

    res = os.path.join(ROOT, "platforms", "android", "app", "src", "main", "res")
    for d in ("drawable-nodpi", "drawable-xxxhdpi"):
        os.makedirs(os.path.join(res, d), exist_ok=True)
    width(logo, min(logo.width, 1600)).save(os.path.join(res, "drawable-nodpi", "splash_logo.png"), optimize=True)
    win = width(logo, ANDROID_WINDOW_DP * 4)
    win.save(os.path.join(res, "drawable-xxxhdpi", "splash_logo_window.png"), optimize=True)
    with open(os.path.join(res, "values", "splash_colors.xml"), "w") as f:
        f.write(f'<?xml version="1.0" encoding="utf-8"?>\n<resources>\n    <color name="splash_bg">{hexbg}</color>\n</resources>\n')
    with open(os.path.join(res, "values", "splash_dimens.xml"), "w") as f:
        f.write('<?xml version="1.0" encoding="utf-8"?>\n<resources>\n'
                f'    <dimen name="splash_window_logo_w">{ANDROID_WINDOW_DP}dp</dimen>\n'
                f'    <dimen name="splash_window_logo_h">{round(ANDROID_WINDOW_DP * logo.height / logo.width)}dp</dimen>\n'
                '</resources>\n')
    print("splash assets written")


if __name__ == "__main__":
    main()
