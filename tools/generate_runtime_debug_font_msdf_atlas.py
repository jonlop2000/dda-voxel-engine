#!/usr/bin/env python3
"""Generate the temporary runtime UI debug MSDF smoke atlas.

This is intentionally not the production font pipeline. It exports a small
MSDF-like atlas from the same bitmap glyph patterns used by the early runtime UI
smoke font so the renderer can exercise external atlas loading and MSDF shader
sampling before FreeType/msdf-atlas-gen integration lands.
"""

from __future__ import annotations

import math
import json
from pathlib import Path


FIRST_GLYPH = 32
LAST_GLYPH = 126
ATLAS_COLUMNS = 16
CELL_WIDTH = 12
CELL_HEIGHT = 16
BIT_SCALE = 2
GLYPH_OFFSET_X = 1
GLYPH_OFFSET_Y = 1
DISTANCE_RANGE = 6.0
OUTPUT = Path("assets/ui/fonts/runtime_debug_font_msdf.tga")
METADATA_OUTPUT = Path("assets/ui/fonts/runtime_debug_font.json")


PATTERNS = {
    "A": [".###.", "#...#", "#...#", "#####", "#...#", "#...#", "#...#"],
    "B": ["####.", "#...#", "#...#", "####.", "#...#", "#...#", "####."],
    "C": [".####", "#....", "#....", "#....", "#....", "#....", ".####"],
    "D": ["####.", "#...#", "#...#", "#...#", "#...#", "#...#", "####."],
    "E": ["#####", "#....", "#....", "####.", "#....", "#....", "#####"],
    "F": ["#####", "#....", "#....", "####.", "#....", "#....", "#...."],
    "G": [".####", "#....", "#....", "#.###", "#...#", "#...#", ".####"],
    "H": ["#...#", "#...#", "#...#", "#####", "#...#", "#...#", "#...#"],
    "I": ["#####", "..#..", "..#..", "..#..", "..#..", "..#..", "#####"],
    "J": ["..###", "...#.", "...#.", "...#.", "...#.", "#..#.", ".##.."],
    "K": ["#...#", "#..#.", "#.#..", "##...", "#.#..", "#..#.", "#...#"],
    "L": ["#....", "#....", "#....", "#....", "#....", "#....", "#####"],
    "M": ["#...#", "##.##", "#.#.#", "#.#.#", "#...#", "#...#", "#...#"],
    "N": ["#...#", "##..#", "#.#.#", "#..##", "#...#", "#...#", "#...#"],
    "O": [".###.", "#...#", "#...#", "#...#", "#...#", "#...#", ".###."],
    "P": ["####.", "#...#", "#...#", "####.", "#....", "#....", "#...."],
    "Q": [".###.", "#...#", "#...#", "#...#", "#.#.#", "#..#.", ".##.#"],
    "R": ["####.", "#...#", "#...#", "####.", "#.#..", "#..#.", "#...#"],
    "S": [".####", "#....", "#....", ".###.", "....#", "....#", "####."],
    "T": ["#####", "..#..", "..#..", "..#..", "..#..", "..#..", "..#.."],
    "U": ["#...#", "#...#", "#...#", "#...#", "#...#", "#...#", ".###."],
    "V": ["#...#", "#...#", "#...#", "#...#", "#...#", ".#.#.", "..#.."],
    "W": ["#...#", "#...#", "#...#", "#.#.#", "#.#.#", "##.##", "#...#"],
    "X": ["#...#", "#...#", ".#.#.", "..#..", ".#.#.", "#...#", "#...#"],
    "Y": ["#...#", "#...#", ".#.#.", "..#..", "..#..", "..#..", "..#.."],
    "Z": ["#####", "....#", "...#.", "..#..", ".#...", "#....", "#####"],
    "0": [".###.", "#...#", "#..##", "#.#.#", "##..#", "#...#", ".###."],
    "1": ["..#..", ".##..", "..#..", "..#..", "..#..", "..#..", ".###."],
    "2": [".###.", "#...#", "....#", "...#.", "..#..", ".#...", "#####"],
    "3": ["####.", "....#", "....#", ".###.", "....#", "....#", "####."],
    "4": ["#...#", "#...#", "#...#", "#####", "....#", "....#", "....#"],
    "5": ["#####", "#....", "#....", "####.", "....#", "....#", "####."],
    "6": [".###.", "#....", "#....", "####.", "#...#", "#...#", ".###."],
    "7": ["#####", "....#", "...#.", "..#..", ".#...", ".#...", ".#..."],
    "8": [".###.", "#...#", "#...#", ".###.", "#...#", "#...#", ".###."],
    "9": [".###.", "#...#", "#...#", ".####", "....#", "....#", ".###."],
    "-": [".....", ".....", ".....", ".###.", ".....", ".....", "....."],
    "!": ["..#..", "..#..", "..#..", "..#..", "..#..", ".....", "..#.."],
    "%": ["##..#", "##.#.", "...#.", "..#..", ".#...", ".#.##", "#..##"],
    ".": [".....", ".....", ".....", ".....", ".....", ".##..", ".##.."],
    ":": [".....", ".##..", ".##..", ".....", ".##..", ".##..", "....."],
    "/": ["....#", "...#.", "...#.", "..#..", ".#...", ".#...", "#...."],
    " ": [".....", ".....", ".....", ".....", ".....", ".....", "....."],
    "?": [".###.", "#...#", "...#.", "..#..", "..#..", ".....", "..#.."],
}


def glyph_pattern(ch: str) -> list[str]:
    return PATTERNS.get(ch.upper(), PATTERNS["?"])


# glyphs outside the contiguous ascii grid (32..126). they are packed into atlas
# cells right after the grid and emitted with explicit atlasBounds, so the loader
# resolves them by codepoint. drawn in the same all-caps bitmap style as the rest
# of the debug font.
EXTRA_GLYPHS: list[tuple[int, list[str]]] = [
    # U+00A1 inverted exclamation mark
    (0x00A1, ["..#..", ".....", "..#..", "..#..", "..#..", "..#..", "..#.."]),
    # U+00E9 e-acute (rendered as accented E to match the uppercase debug font)
    (0x00E9, ["...#.", "#####", "#....", "####.", "#....", "#....", "#####"]),
]

GRID_GLYPH_COUNT = LAST_GLYPH - FIRST_GLYPH + 1
ATLAS_ROWS = (GRID_GLYPH_COUNT + len(EXTRA_GLYPHS) + ATLAS_COLUMNS - 1) // ATLAS_COLUMNS


def stamp_glyph(mask: list[list[bool]], index: int, pattern: list[str], width: int,
                height: int) -> None:
    col = index % ATLAS_COLUMNS
    row = index // ATLAS_COLUMNS
    base_x = col * CELL_WIDTH
    base_y = row * CELL_HEIGHT
    for gy, line in enumerate(pattern):
        for gx, value in enumerate(line):
            if value != "#":
                continue
            for sy in range(BIT_SCALE):
                for sx in range(BIT_SCALE):
                    x = base_x + GLYPH_OFFSET_X + gx * BIT_SCALE + sx
                    y = base_y + GLYPH_OFFSET_Y + gy * BIT_SCALE + sy
                    if 0 <= x < width and 0 <= y < height:
                        mask[y][x] = True


def build_mask(width: int, height: int) -> list[list[bool]]:
    mask = [[False for _ in range(width)] for _ in range(height)]
    for code in range(FIRST_GLYPH, LAST_GLYPH + 1):
        stamp_glyph(mask, code - FIRST_GLYPH, glyph_pattern(chr(code)), width, height)
    for offset, (_codepoint, pattern) in enumerate(EXTRA_GLYPHS):
        stamp_glyph(mask, GRID_GLYPH_COUNT + offset, pattern, width, height)
    return mask


def nearest_opposite_distance(mask: list[list[bool]], x: int, y: int, search_radius: int) -> float:
    height = len(mask)
    width = len(mask[0])
    inside = mask[y][x]
    best_sq = float(search_radius * search_radius)
    y0 = max(0, y - search_radius)
    y1 = min(height - 1, y + search_radius)
    x0 = max(0, x - search_radius)
    x1 = min(width - 1, x + search_radius)
    for yy in range(y0, y1 + 1):
        dy = yy - y
        for xx in range(x0, x1 + 1):
            if mask[yy][xx] == inside:
                continue
            dx = xx - x
            dist_sq = float(dx * dx + dy * dy)
            if dist_sq < best_sq:
                best_sq = dist_sq
    return max(0.0, math.sqrt(best_sq) - 0.5)


def build_msdf_like_bgra(mask: list[list[bool]]) -> bytes:
    height = len(mask)
    width = len(mask[0])
    search_radius = int(DISTANCE_RANGE * 2.0)
    pixels = bytearray()
    for y in range(height):
        for x in range(width):
            distance = nearest_opposite_distance(mask, x, y, search_radius)
            signed = distance if mask[y][x] else -distance
            value = max(0.0, min(1.0, 0.5 + signed / (2.0 * DISTANCE_RANGE)))
            byte = int(value * 255.0 + 0.5)
            pixels.extend((byte, byte, byte, 255))
    return bytes(pixels)


def write_tga(path: Path, width: int, height: int, bgra: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    header = bytearray(18)
    header[2] = 2  # uncompressed true-color
    header[12] = width & 0xFF
    header[13] = (width >> 8) & 0xFF
    header[14] = height & 0xFF
    header[15] = (height >> 8) & 0xFF
    header[16] = 32
    header[17] = 0x28  # 8 alpha bits, top-left origin
    path.write_bytes(bytes(header) + bgra)


def build_metadata(width: int, height: int) -> dict:
    glyphs = []
    for code in range(FIRST_GLYPH, LAST_GLYPH + 1):
        index = code - FIRST_GLYPH
        col = index % ATLAS_COLUMNS
        row = index // ATLAS_COLUMNS
        x0 = col * CELL_WIDTH
        y0 = row * CELL_HEIGHT
        glyph = {
            "unicode": code,
            "advance": 7 if code == 32 else 11,
        }
        if code != 32:
            glyph["planeBounds"] = {
                "left": 0,
                "bottom": 0,
                "right": CELL_WIDTH,
                "top": CELL_HEIGHT,
            }
            glyph["atlasBounds"] = {
                "left": x0,
                "bottom": y0,
                "right": x0 + CELL_WIDTH,
                "top": y0 + CELL_HEIGHT,
            }
        glyphs.append(glyph)

    for offset, (codepoint, _pattern) in enumerate(EXTRA_GLYPHS):
        index = GRID_GLYPH_COUNT + offset
        col = index % ATLAS_COLUMNS
        row = index // ATLAS_COLUMNS
        x0 = col * CELL_WIDTH
        y0 = row * CELL_HEIGHT
        glyphs.append(
            {
                "unicode": codepoint,
                "advance": 11,
                "planeBounds": {
                    "left": 0,
                    "bottom": 0,
                    "right": CELL_WIDTH,
                    "top": CELL_HEIGHT,
                },
                "atlasBounds": {
                    "left": x0,
                    "bottom": y0,
                    "right": x0 + CELL_WIDTH,
                    "top": y0 + CELL_HEIGHT,
                },
            }
        )

    return {
        "schema": "voxel.runtime_ui.font.v1",
        "id": "runtime_debug_font",
        "render_mode": "msdf",
        "atlas": {
            "source": "debug_bitmap_sdf_grid",
            "image": "ui/fonts/runtime_debug_font_msdf.tga",
            "width": width,
            "height": height,
            "columns": ATLAS_COLUMNS,
            "rows": ATLAS_ROWS,
            "cell_width": CELL_WIDTH,
            "cell_height": CELL_HEIGHT,
            "first_codepoint": FIRST_GLYPH,
            "last_codepoint": LAST_GLYPH,
            "y_origin": "top",
        },
        "metrics": {
            "line_height": CELL_HEIGHT,
            "ascender": CELL_HEIGHT,
            "descender": 0,
            "space_advance": 7,
            "default_advance": 11,
            "distance_range": DISTANCE_RANGE,
            "fallback_codepoint": 63,
        },
        "glyphs": glyphs,
        "kerning": [
            {"unicode1": 84, "unicode2": 73, "advance": -1.0},
        ],
        "notes": (
            "Temporary runtime UI debug MSDF smoke font generated from bitmap glyph "
            "patterns. Replace with msdf-atlas-gen output after the offline font "
            "pipeline lands."
        ),
    }


def main() -> None:
    width = ATLAS_COLUMNS * CELL_WIDTH
    height = ATLAS_ROWS * CELL_HEIGHT
    mask = build_mask(width, height)
    write_tga(OUTPUT, width, height, build_msdf_like_bgra(mask))
    METADATA_OUTPUT.write_text(json.dumps(build_metadata(width, height), indent=2) + "\n")
    print(f"Wrote {OUTPUT} ({width}x{height})")
    print(f"Wrote {METADATA_OUTPUT}")


if __name__ == "__main__":
    main()
