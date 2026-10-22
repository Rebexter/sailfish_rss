#!/usr/bin/env python3
"""Draws the SailRSS launcher icon and writes icons/<size>/harbour-sailrss.png.

The shape follows the Sailfish launcher style: a square with three strongly
rounded corners and a sharp top-right corner, filling the canvas. On it, a
white RSS symbol whose arcs double as a sail in the wind.

Usage: scripts/make-icons.py [--preview out.png]
Needs Pillow.
"""
import argparse
import math
import os

from PIL import Image, ImageChops, ImageDraw

SIZES = (86, 108, 128, 172)
S = 1024  # drawing size; downscaled for each icon size

TOP_COLOR = (46, 182, 228)      # light sea blue
BOTTOM_COLOR = (12, 84, 170)    # deep sea blue
SYMBOL = (255, 255, 255, 255)


def gradient(size, top, bottom):
    """Diagonal gradient from the top left to the bottom right."""
    image = Image.new("RGBA", (size, size))
    pixels = image.load()
    for y in range(size):
        for x in range(size):
            t = (x + y) / (2 * (size - 1))
            pixels[x, y] = tuple(round(a + (b - a) * t) for a, b in zip(top, bottom)) + (255,)
    return image


def shape_mask(size):
    """Rounded square, top-right corner sharp."""
    mask = Image.new("L", (size, size), 0)
    margin = round(size * 0.004)
    ImageDraw.Draw(mask).rounded_rectangle(
        (margin, margin, size - 1 - margin, size - 1 - margin),
        radius=round(size * 0.40),
        fill=255,
        corners=(True, False, True, True),  # top left, top right, bottom right, bottom left
    )
    return mask


def rss_symbol(size):
    """Dot and two quarter arcs with round ends, as an alpha mask."""
    mask = Image.new("L", (size, size), 0)
    draw = ImageDraw.Draw(mask)
    cx, cy = size * 0.30, size * 0.70  # centre of the dot and the arcs
    dot = size * 0.085
    draw.ellipse((cx - dot, cy - dot, cx + dot, cy + dot), fill=255)

    width = size * 0.095
    for radius in (size * 0.255, size * 0.43):
        box = (cx - radius, cy - radius, cx + radius, cy + radius)
        draw.arc(box, start=-90, end=0, fill=255, width=round(width))
        # Round ends: the arc is drawn inward from its radius
        mid = radius - width / 2
        for angle in (-90, 0):
            ex = cx + mid * math.cos(math.radians(angle))
            ey = cy + mid * math.sin(math.radians(angle))
            draw.ellipse((ex - width / 2, ey - width / 2, ex + width / 2, ey + width / 2), fill=255)
    return mask


def draw_icon():
    icon = gradient(S, TOP_COLOR, BOTTOM_COLOR)
    # A soft light band at the top for depth, like the system icons
    light = Image.new("L", (S, S), 0)
    ImageDraw.Draw(light).ellipse((-S * 0.4, -S * 1.05, S * 1.4, S * 0.45), fill=34)
    icon = Image.composite(Image.new("RGBA", (S, S), (255, 255, 255, 255)), icon, light)

    symbol = Image.new("RGBA", (S, S), SYMBOL)
    icon = Image.composite(symbol, icon, rss_symbol(S))
    icon.putalpha(ImageChops.multiply(icon.getchannel("A"), shape_mask(S)))
    return icon


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--preview", help="also write a preview on a dark background")
    args = parser.parse_args()

    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    icon = draw_icon()
    for size in SIZES:
        path = os.path.join(root, "icons", f"{size}x{size}", "harbour-sailrss.png")
        icon.resize((size, size), Image.LANCZOS).save(path, optimize=True)
        print("wrote", os.path.relpath(path, root))

    if args.preview:
        sizes = (172, 128, 86)
        preview = Image.new("RGBA", (sum(sizes) + 40 * (len(sizes) + 1), 172 + 80), (27, 42, 58, 255))
        x = 40
        for size in sizes:
            small = icon.resize((size, size), Image.LANCZOS)
            preview.alpha_composite(small, (x, 40 + (172 - size)))
            x += size + 40
        preview.save(args.preview)
        print("wrote", args.preview)


if __name__ == "__main__":
    main()
