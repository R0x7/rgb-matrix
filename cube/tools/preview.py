#!/usr/bin/env python3
"""Turn `ledcube --dump x.ppm` output (three faces side by side) into an
isometric picture of the cube corner, for checking scenes without the panels.

usage: preview.py out.png a.ppm [b.ppm ...]
"""
import sys
from PIL import Image, ImageDraw

N, S = 64, 4
C, H = S * 0.866, S * 0.5  # screen step per pixel along the top-face axes


def cube(ppm):
    src = Image.open(ppm).convert("RGB").load()
    w, h = int(2 * N * C) + 8, int(2 * N * H + N * S) + 8
    img = Image.new("RGB", (w, h), (12, 12, 14))
    d = ImageDraw.Draw(img)
    ox, oy = w / 2, 4
    ey, ex, dn = (C, H), (-C, H), (0, S)

    def quad(px, py, a, b, col):
        d.polygon([(px, py), (px + a[0], py + a[1]),
                   (px + a[0] + b[0], py + a[1] + b[1]), (px + b[0], py + b[1])], fill=col)

    for j in range(N):
        for i in range(N):
            quad(ox + i * ey[0] + j * ex[0], oy + i * ey[1] + j * ex[1], ey, ex, src[i, j])
            quad(ox + N * ex[0] + i * ey[0], oy + N * ex[1] + i * ey[1] + j * S, ey, dn,
                 src[N + i, j])
            rx, ry = ox + N * ey[0] + N * ex[0], oy + N * ey[1] + N * ex[1]
            quad(rx - (i + 1) * ex[0], ry - (i + 1) * ex[1] + j * S, ex, dn, src[2 * N + i, j])
    return img


def main():
    out, files = sys.argv[1], sys.argv[2:]
    tiles = [cube(f) for f in files]
    cols = min(3, len(tiles))
    rows = (len(tiles) + cols - 1) // cols
    tw, th = tiles[0].size
    sheet = Image.new("RGB", (cols * tw, rows * th), (12, 12, 14))
    for k, t in enumerate(tiles):
        sheet.paste(t, ((k % cols) * tw, (k // cols) * th))
    sheet.save(out)


if __name__ == "__main__":
    main()
