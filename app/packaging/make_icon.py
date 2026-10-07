"""Draws the VFX Forge app icon and writes it in the formats each platform wants.

Run:  python3 make_icon.py
Needs Pillow and NumPy. The outputs are committed, so builds never run this.
"""
import math

import numpy as np
from PIL import Image, ImageDraw

SIZE = 1024


def glow(canvas, cx, cy, radius, color, strength):
    """Adds a soft round light to a float RGB canvas."""
    y, x = np.mgrid[0:SIZE, 0:SIZE].astype(np.float32)
    d2 = ((x - cx) ** 2 + (y - cy) ** 2) / float(radius * radius)
    falloff = np.clip(1.0 - d2, 0.0, 1.0) ** 2 * strength
    for channel in range(3):
        canvas[..., channel] += falloff * color[channel]


def main():
    light = np.zeros((SIZE, SIZE, 3), dtype=np.float32)

    # The forge: a hot core low in the frame.
    core = (512, 640)
    glow(light, *core, 520, (1.00, 0.36, 0.05), 0.55)
    glow(light, *core, 300, (1.00, 0.55, 0.12), 0.9)
    glow(light, *core, 130, (1.00, 0.85, 0.55), 1.3)

    # Sparks thrown up and outward, shrinking and cooling as they travel.
    sparks = [(-62, 250, 34), (-38, 400, 26), (-22, 300, 30), (-6, 470, 22), (10, 340, 32),
              (26, 430, 24), (44, 290, 28), (64, 380, 20), (-80, 360, 18), (82, 240, 22)]
    for angle, distance, radius in sparks:
        a = math.radians(angle)
        x = core[0] + math.sin(a) * distance
        y = core[1] - math.cos(a) * distance
        heat = 1.0 - distance / 620.0
        glow(light, x, y, radius * 2.6, (1.0, 0.45 + 0.3 * heat, 0.10 + 0.3 * heat), 0.55)
        glow(light, x, y, radius, (1.0, 0.9, 0.7), 1.2)

    background = np.empty((SIZE, SIZE, 3), dtype=np.float32)
    background[...] = (0.055, 0.058, 0.072)
    rgb = np.clip(background + light, 0.0, 1.0)
    picture = Image.fromarray((rgb * 255).astype(np.uint8), "RGB").convert("RGBA")

    # macOS icons sit inside a rounded square with a margin around it.
    margin, corner = 100, 185
    mask = Image.new("L", (SIZE * 2, SIZE * 2), 0)
    ImageDraw.Draw(mask).rounded_rectangle(
        (margin * 2, margin * 2, (SIZE - margin) * 2, (SIZE - margin) * 2), corner * 2, fill=255)
    picture.putalpha(mask.resize((SIZE, SIZE), Image.LANCZOS))

    picture.save("VFXForge.png")
    picture.save("VFXForge.icns")
    picture.save("VFXForge.ico", sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64),
                                        (128, 128), (256, 256)])


if __name__ == "__main__":
    main()
