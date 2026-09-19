#!/usr/bin/env python3
"""Derive the dark-theme image assets from the light ones.

The bubble nine-slices and the composer's text-field background are flat fills: the
shape, the antialiasing and the nine-slice geometry all live in the alpha channel (or,
for the text field, in a one-pixel hairline), and the RGB is one colour repeated. So a
dark variant is the same file with its RGB replaced, which keeps the 22px border insets
and the tails byte-identical to the originals rather than hoping a second hand-drawn set
matches them.

Re-run after changing a light asset, or to try a different shade:

    python3 tools/make_inverted_assets.py

Only the colours below are a judgement call; everything else is mechanical. Note the
pressed states inverting their direction - on white, pressing darkens; on a dark bubble
there is nothing below to darken towards, so pressing lightens.
"""

from collections import Counter
from pathlib import Path

from PIL import Image

IMAGES = Path(__file__).resolve().parent.parent / "resources" / "images"

# Bubble fills. Incoming sits on the page and has to read as a surface above it; outgoing
# keeps the accent's hue and drops its luminance, because #0AA7CC against a near-black
# page is the glare the dark theme exists to remove.
BUBBLES = {
    "incoming-normal": "#2b2b2b",
    "incoming-pressed": "#3d3d3d",
    "outgoing-normal": "#0a6a82",
    "outgoing-pressed": "#0e8aa8",
}

# The composer's field: one flat fill and a one-pixel hairline along the top.
TEXTEDIT = "messaging-textedit-background"
TEXTEDIT_FILL = "#1e1e1e"
TEXTEDIT_LINE = "#3c3c3c"


def rgb(value):
    return tuple(int(value[i : i + 2], 16) for i in (1, 3, 5))


def recolour_keeping_alpha(name, colour):
    """Replace every pixel's RGB, keep its alpha. The shape is entirely in the alpha."""
    source = Image.open(IMAGES / f"{name}.png").convert("RGBA")
    alpha = source.split()[3]

    out = Image.new("RGBA", source.size, rgb(colour) + (255,))
    out.putalpha(alpha)

    target = IMAGES / f"{name}-inverted.png"
    out.save(target)
    return target


def recolour_two_tone(name, fill, line):
    """Map the dominant colour to `fill` and everything else to `line`."""
    source = Image.open(IMAGES / f"{name}.png").convert("RGB")
    pixels = list(source.getdata())

    dominant = Counter(pixels).most_common(1)[0][0]
    mapped = [rgb(fill) if p == dominant else rgb(line) for p in pixels]

    out = Image.new("RGB", source.size)
    out.putdata(mapped)

    target = IMAGES / f"{name}-inverted.png"
    out.save(target)
    return target


def main():
    for name, colour in BUBBLES.items():
        print("wrote", recolour_keeping_alpha(name, colour).name, colour)

    print("wrote", recolour_two_tone(TEXTEDIT, TEXTEDIT_FILL, TEXTEDIT_LINE).name, TEXTEDIT_FILL)


if __name__ == "__main__":
    main()
