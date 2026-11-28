# SPDX-License-Identifier: AGPL-3.0-only
"""Render the ArSonMix splash background from original vector geometry."""

from pathlib import Path
from random import Random

from PIL import Image, ImageDraw, ImageFilter


def render():
    width, height = 768, 432
    image = Image.new("RGB", (width, height), (10, 19, 27))
    draw = ImageDraw.Draw(image)
    random = Random(718)

    def point(x, depth):
        spread = 0.48 + 0.67 * depth
        return (350 + (x - 6) * 90 * spread - 14 * depth,
                28 + 391 * depth + (x - 6) * (13 + 3 * depth))

    def panel(left, top, right, bottom, color):
        draw.polygon([point(left, top), point(right, top),
                      point(right, bottom), point(left, bottom)], fill=color)

    panel(-0.48, 0, 12.48, 1.15, (24, 34, 41))
    for channel in range(12):
        x = channel + 0.5
        level = random.randrange(27, 42)
        panel(x - 0.47, 0.04, x + 0.47, 1.11, (level, level + 11, level + 18))
        panel(x - 0.46, 0.08, x - 0.40, 1.08, (65, 83, 91))
        for depth in (0.17, 0.28, 0.40):
            panel(x - 0.28, depth, x + 0.28, depth + 0.055, (12, 20, 25))
            panel(x - 0.17, depth - 0.016, x + 0.18, depth + 0.035, (65, 83, 91))
            panel(x - 0.14, depth - 0.013, x + 0.15, depth + 0.005, (112, 132, 140))
        panel(x - 0.14, 0.52, x + 0.14, 0.57, (18, 131, 140))
        panel(x - 0.32, 0.60, x + 0.32, 1.02, (36, 49, 58))
        panel(x - 0.045, 0.62, x + 0.045, 0.99, (9, 16, 22))
        for step in range(7):
            depth = 0.65 + step * 0.049
            panel(x + 0.12, depth, x + 0.23, depth + 0.006, (93, 111, 120))
        fader = 0.68 + random.random() * 0.24
        panel(x - 0.28, fader, x + 0.28, fader + 0.059, (17, 26, 34))
        panel(x - 0.25, fader - 0.023, x + 0.25, fader + 0.025, (121, 153, 166))
        panel(x - 0.25, fader - 0.018, x + 0.25, fader - 0.005, (165, 194, 202))

    image = image.filter(ImageFilter.GaussianBlur(8.4))
    pixels = image.load()
    for y in range(height):
        for x in range(width):
            nx = x / width - 0.5
            ny = y / height - 0.52
            light = max(0.0, 1 - 2.6 * ((nx + 0.18) ** 2 + (ny - 0.22) ** 2))
            shade = max(0.39, 0.91 - 1.08 * (nx * nx + ny * ny))
            red, green, blue = pixels[x, y]
            pixels[x, y] = (round(red * shade),
                            round(green * shade + 11 * light),
                            round(blue * shade + 16 * light))
    return image.resize((384, 216), Image.Resampling.LANCZOS)


if __name__ == "__main__":
    render().save(Path(__file__).with_name("arsonmix-splash-background.jpg"),
                  quality=60, optimize=True)
