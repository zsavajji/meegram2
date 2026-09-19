#!/usr/bin/env python3
"""Derive the dark-theme image assets from the light ones.

One asset, now that the message balloons are drawn shapes rather than images: the
composer's text-field background. It is a flat fill with a one-pixel hairline along the
top, so the dark variant is the same file with its two colours swapped - which keeps it
exactly the size and shape the light one is, rather than hoping a second hand-drawn file
matches.

Re-run after changing the light asset, or to try a different shade:

    python3 tools/make_inverted_assets.py

Only the two colours below are a judgement call; everything else is mechanical. They are
panelColor and separatorColor from resources/qml/main.qml, and should stay that way - the
field sits on a panel and its hairline is one of that panel's.
"""

from collections import Counter
from pathlib import Path

from PIL import Image

IMAGES = Path(__file__).resolve().parent.parent / "resources" / "images"

TEXTEDIT = "messaging-textedit-background"
TEXTEDIT_FILL = "#1e1e1e"
TEXTEDIT_LINE = "#3c3c3c"


def rgb(value):
    return tuple(int(value[i : i + 2], 16) for i in (1, 3, 5))


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


if __name__ == "__main__":
    print("wrote", recolour_two_tone(TEXTEDIT, TEXTEDIT_FILL, TEXTEDIT_LINE).name, TEXTEDIT_FILL)
