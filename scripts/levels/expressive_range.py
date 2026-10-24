#!/usr/bin/env python3
"""The generators' expressive range, side by side (Smith and Whitehead, PCG 2010): build/level_range's traits of many
generated courses, the classic generator's and the rhythm-beat generator's, drawn as 2D histograms (linearity across,
leniency up: how straight and how harsh), with their pacing under each. Where the dots crowd, the generator's habit;
an empty region, what it can't make.

    python3 scripts/levels/expressive_range.py [--from 31] [--to 50] [--seeds 10] [--out docs/images/expressive-range.png]
"""
import argparse
import json
import os
import subprocess

from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))


def run(args, classic):
    cmd = [os.path.join(ROOT, "build", "level_range"), "--from", str(args.lo), "--to", str(args.hi), "--seeds", str(args.seeds)]
    out = subprocess.run(cmd + (["--classic"] if classic else []), capture_output=True, text=True, check=True).stdout
    return [json.loads(line) for line in out.splitlines() if line.startswith("{")]


def panel(draw, x0, y0, size, rows, title, colour, font, harsh):
    bins = 12
    grid = [[0] * bins for _ in range(bins)]
    for r in rows:
        i = min(bins - 1, int(r["linearity"] * bins))
        j = min(bins - 1, int(min(1.0, -r["leniency"] / harsh) * bins))
        grid[j][i] += 1
    top = max(1, max(max(row) for row in grid))
    cell = size / bins
    draw.rectangle([x0, y0, x0 + size, y0 + size], fill=(14, 10, 24))
    for j in range(bins):
        for i in range(bins):
            if grid[j][i]:
                k = grid[j][i] / top
                c = tuple(int(10 + (v - 10) * (0.25 + 0.75 * k)) for v in colour)
                draw.rectangle([x0 + i * cell + 1, y0 + size - (j + 1) * cell + 1, x0 + (i + 1) * cell - 1, y0 + size - j * cell - 1], fill=c)
    draw.text((x0, y0 - 34), title, fill=colour, font=font)
    draw.text((x0, y0 + size + 6), "linearity →", fill=(200, 200, 220), font=font)
    draw.text((x0 - 70, y0 + size / 2), "harsher\n   ↑", fill=(200, 200, 220), font=font)
    pace = sorted(r["pacing"] for r in rows)
    distinct = len({r.get("pieces", "") for r in rows})
    draw.text((x0, y0 + size + 30), "pacing: %.2f s a moment (median); %d courses" % (pace[len(pace) // 2], len(rows)), fill=(200, 200, 220), font=font)
    if rows[0].get("pieces"):
        draw.text((x0, y0 + size + 54), "%d distinct piece sequences" % distinct, fill=(200, 200, 220), font=font)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--from", dest="lo", type=int, default=31)
    ap.add_argument("--to", dest="hi", type=int, default=50)
    ap.add_argument("--seeds", type=int, default=10)
    ap.add_argument("--out", default=os.path.join(ROOT, "docs", "images", "expressive-range.png"))
    args = ap.parse_args()
    classic, beats = run(args, True), run(args, False)
    img = Image.new("RGB", (1000, 560), (6, 4, 12))
    draw = ImageDraw.Draw(img)
    try:
        font = ImageFont.truetype("/System/Library/Fonts/Supplemental/Arial.ttf", 18)
    except OSError:
        font = ImageFont.load_default()
    harsh = max(-r["leniency"] for r in classic + beats) * 1.001 # (one scale for both)
    panel(draw, 90, 70, 380, classic, "Classic generator (a wall at a time)", (41, 231, 255), font, harsh)
    panel(draw, 590, 70, 380, beats, "Rhythm-beat generator", (255, 43, 214), font, harsh)
    img.save(args.out)
    print("wrote", args.out)


if __name__ == "__main__":
    main()
