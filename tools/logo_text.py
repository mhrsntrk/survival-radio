#!/usr/bin/env python3
"""Build the 128x40 boot logo: the radio tower icon of the old logo (columns 0-38)
and "SURVIVAL / RADIO" in a 5x7 block font drawn at 2x. Prints the C array and an
ASCII preview.

Usage: logo_text.py firmware/App/bitmaps.c
"""
import re
import sys

FONT = {  # 5x7, '#' = pixel
    "S": [".####", "#....", "#....", ".###.", "....#", "....#", "####."],
    "U": ["#...#", "#...#", "#...#", "#...#", "#...#", "#...#", ".###."],
    "R": ["####.", "#...#", "#...#", "####.", "#.#..", "#..#.", "#...#"],
    "V": ["#...#", "#...#", "#...#", "#...#", ".#.#.", ".#.#.", "..#.."],
    "I": ["#####", "..#..", "..#..", "..#..", "..#..", "..#..", "#####"],
    "A": [".###.", "#...#", "#...#", "#####", "#...#", "#...#", "#...#"],
    "L": ["#....", "#....", "#....", "#....", "#....", "#....", "#####"],
    "D": ["####.", "#...#", "#...#", "#...#", "#...#", "#...#", "####."],
    "O": [".###.", "#...#", "#...#", "#...#", "#...#", "#...#", ".###."],
}
W, H, ICON_W = 128, 40, 39


def main():
    src = open(sys.argv[1]).read()
    i = src.find("BITMAP_SurvivalLogo")
    old = [int(x, 16) for x in re.findall(r"0x([0-9A-Fa-f]{2})", src[i:src.find("};", i)])]
    px = [[0] * W for _ in range(H)]
    for y in range(H):
        for x in range(ICON_W):
            px[y][x] = (old[(y // 8) * W + x] >> (y % 8)) & 1

    def text(s, y0, scale=2, gap=1):
        width = len(s) * 5 * scale + (len(s) - 1) * gap
        x0 = ICON_W + 2 + (W - ICON_W - 2 - width) // 2
        for n, ch in enumerate(s):
            for r, row in enumerate(FONT[ch]):
                for c, v in enumerate(row):
                    if v == "#":
                        for dy in range(scale):
                            for dx in range(scale):
                                px[y0 + r * scale + dy][x0 + n * (5 * scale + gap) + c * scale + dx] = 1

    text("SURVIVAL", 3)
    text("RADIO", 22)

    for row in px:
        print("".join("#" if v else "." for v in row), file=sys.stderr)
    print("const uint8_t BITMAP_SurvivalLogo[5][128] = {")
    for p in range(H // 8):
        vals = [sum(px[p * 8 + b][x] << b for b in range(8)) for x in range(W)]
        print("    {" + ", ".join(f"0x{v:02X}" for v in vals) + "},")
    print("};")


if __name__ == "__main__":
    main()
