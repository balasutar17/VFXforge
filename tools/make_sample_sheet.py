"""Draws the sample sprite sheet that ships with VFX Forge.

A toon flame in eight frames, 4 across and 2 down, 256 pixels a frame, in the
flat, outlined style of hand-painted game effects. Run it again to remake
app/samples/toon-flame-4x2.png:

    python3 tools/make_sample_sheet.py
"""
import math
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter

CELL = 256
COLS, ROWS = 4, 2
SUPER = 4  # drawn this many times larger, then shrunk, for smooth edges

OUTLINE = (122, 29, 10, 255)
BODY = (255, 116, 26, 255)
MIDDLE = (255, 196, 58, 255)
CORE = (255, 246, 196, 255)


def flame(phase, scale, lean, height):
    """The outline of a flame as points, centred at the bottom middle."""
    pts = []
    n = 90
    for i in range(n + 1):
        t = i / n  # 0 at the left base, round the tip, 1 at the right base
        a = math.pi * t
        side = -math.cos(a)  # -1 left, 1 right
        up = math.sin(a)
        # A round belly that narrows into a licking tip.
        width = 0.30 * scale * (1.0 - 0.75 * up ** 3)
        y = -height * scale * (up ** 1.6)
        sway = lean * (up ** 2) * math.sin(phase + up * 2.4) * scale
        wobble = 0.03 * scale * math.sin(phase * 2 + t * 18)
        x = side * width * (1 + 0.15 * math.sin(phase + t * 6)) + sway + wobble
        pts.append((x, y))
    # The rounded bottom.
    for i in range(1, 30):
        a = math.pi * i / 30
        pts.append((0.30 * scale * math.cos(a), 0.16 * scale * math.sin(a)))
    return pts


def place(points, cx, cy, size):
    return [(cx + x * size, cy + y * size) for x, y in points]


def frame(index):
    s = CELL * SUPER
    img = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    phase = index / (COLS * ROWS) * 2 * math.pi
    cx, cy, size = s / 2, s * 0.84, s * 0.92
    height = 0.76 + 0.05 * math.sin(phase * 2)
    lean = 0.10

    d.polygon(place(flame(phase, 1.08, lean, height), cx, cy + size * 0.015, size), fill=OUTLINE)
    d.polygon(place(flame(phase, 1.0, lean, height), cx, cy, size), fill=BODY)
    d.polygon(place(flame(phase + 0.7, 0.70, lean * 0.8, height * 0.82), cx, cy, size), fill=MIDDLE)
    d.polygon(place(flame(phase + 1.3, 0.40, lean * 0.6, height * 0.62), cx, cy, size), fill=CORE)

    # A small piece breaking off the top, rising as the loop goes on.
    rise = (index % 4) / 4
    bx = cx + math.sin(phase) * size * 0.08
    by = cy - size * (0.72 + 0.14 * rise)
    r = size * (0.055 - 0.03 * rise)
    if r > 2:
        d.ellipse((bx - r * 1.35, by - r * 1.35, bx + r * 1.35, by + r * 1.35), fill=OUTLINE)
        d.ellipse((bx - r, by - r, bx + r, by + r), fill=BODY)

    return img.resize((CELL, CELL), Image.LANCZOS)


def main():
    sheet = Image.new("RGBA", (CELL * COLS, CELL * ROWS), (0, 0, 0, 0))
    for i in range(COLS * ROWS):
        sheet.paste(frame(i), ((i % COLS) * CELL, (i // COLS) * CELL))
    out = Path(__file__).resolve().parent.parent / "app" / "samples" / "toon-flame-4x2.png"
    sheet.save(out, optimize=True)
    print(out, out.stat().st_size, "bytes")


if __name__ == "__main__":
    main()
