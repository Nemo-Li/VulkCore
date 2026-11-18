#!/usr/bin/env python3
"""Generate the original seamless terrain materials (requires NumPy and Pillow)."""

from pathlib import Path
import argparse

import numpy as np
from PIL import Image


LAYER_NAMES = ("grass", "dirt", "rock", "sand", "snow", "mud", "gravel", "ash")


def smooth(low, high, value):
    t = np.clip((value - low) / (high - low), 0, 1)
    return t * t * (3 - 2 * t)


def palette(value, *hex_colors):
    colors = np.array([
        [int(color[i:i + 2], 16) for i in (0, 2, 4)] for color in hex_colors
    ], dtype=np.float32)
    stops = np.linspace(0, 1, len(colors))
    return np.stack([np.interp(value, stops, channel) for channel in colors.T], axis=-1)


class Tile:
    def __init__(self, size, seed):
        coordinates = (np.arange(size, dtype=np.float32) + 0.5) / size
        self.x, self.y = np.meshgrid(coordinates, coordinates)
        self.size = size
        self.rng = np.random.default_rng(seed)

    def noise(self, columns, rows=None, warp=None):
        rows = rows or columns
        x, y = self.x, self.y
        if warp is not None:
            x, y = x + warp[0], y + warp[1]
        x, y = x * columns, y * rows
        i, j = np.floor(x).astype(np.int32), np.floor(y).astype(np.int32)
        u, v = smooth(0, 1, x - i), smooth(0, 1, y - j)
        cells = self.rng.random((rows, columns), dtype=np.float32)
        left, right = i % columns, (i + 1) % columns
        top, bottom = j % rows, (j + 1) % rows
        a = cells[top, left] * (1 - u) + cells[top, right] * u
        b = cells[bottom, left] * (1 - u) + cells[bottom, right] * u
        return a * (1 - v) + b * v

    def stones(self, columns, rows, density, elongation=1.0):
        """Rasterize scattered beveled chips with periodic cell lookup."""
        x, y = self.x * columns, self.y * rows
        i, j = np.floor(x).astype(np.int32), np.floor(y).astype(np.int32)
        seeds = self.rng.random((7, rows, columns), dtype=np.float32)
        elevation = np.zeros_like(x)
        mask = np.zeros_like(x)
        face_tone = np.zeros_like(x)
        for near_y in (-1, 0, 1):
            for near_x in (-1, 0, 1):
                k, l = i + near_x, j + near_y
                sx, sy, shape, twist, tone, visible, size = seeds[:, l % rows, k % columns]
                dx, dy = x - k - (0.11 + 0.78 * sx), y - l - (0.11 + 0.78 * sy)
                angle = twist * (2 * np.pi)
                c, s = np.cos(angle), np.sin(angle)
                u = (dx * c - dy * s) / elongation
                v = (dx * s + dy * c) * (1.0 + 0.55 * shape)
                boundary = np.maximum.reduce([
                    abs(u), abs(v), abs(0.53 * u + 0.75 * v) * 1.18,
                    abs(0.67 * u - 0.56 * v) * 1.23,
                ]) / (0.17 + 0.22 * size)
                shape_mask = (1 - smooth(0.86, 1, boundary)) * (visible < density)
                bevel = (1 - smooth(0.46, 1, boundary)) * shape_mask
                face = tone * 0.70 + smooth(-0.12, 0.21, v) * 0.30
                face_tone = np.where(shape_mask > mask, face, face_tone)
                elevation = np.maximum(elevation, bevel * (0.65 + 0.35 * tone))
                mask = np.maximum(mask, shape_mask)
        return mask, elevation, face_tone


def blend(base, overlay, weight):
    return base * (1 - weight[..., None]) + overlay * weight[..., None]


def render_landform(name, tile):
    warp = (0.13 * (tile.noise(4) - 0.5), 0.13 * (tile.noise(4) - 0.5))
    field = tile.noise(5, 6, warp)
    brush = tile.noise(18, 24, warp)
    flecks = tile.noise(53, 47)
    pigment = np.clip(0.61 * field + 0.30 * brush + 0.09 * flecks, 0, 1)
    micro_relief = 0.13 * field + 0.15 * brush + 0.022 * flecks
    if name == "grass":
        grass_stroke = tile.noise(43, 21, warp)
        leaves, leaf_height, leaf_tone = tile.stones(33, 52, 0.70, 1.55)
        color = palette(pigment, "506228", "688136", "859b49", "a5b868")
        color += (smooth(0.56, 0.79, grass_stroke) - 0.34)[:, :, None] * [7, 8, 2]
        color = blend(color, palette(leaf_tone, "5a7332", "8eaa50", "b6bf66"), leaves * 0.27)
        return color, micro_relief + leaf_height * 0.034 + grass_stroke * 0.016, 4.4, 0.22
    if name == "dirt":
        chips, relief, tone = tile.stones(14, 16, 0.53, 1.12)
        color = palette(pigment, "644e39", "856644", "9a7c53", "b89b71")
        color = blend(color, palette(tone, "6e6250", "a79877", "c0b091"), chips * 0.55)
        return color, micro_relief * 0.72 + relief * 0.15, 5.0, 0.37
    if name == "rock":
        plates, relief, tone = tile.stones(5, 9, 0.94, 1.56)
        folds = tile.noise(7, 34, warp)
        ledges = smooth(0.38, 0.46, folds)
        stone = pigment * 0.73 + ledges * 0.26
        color = palette(stone, "565d57", "737971", "95958a", "bcb5a0")
        color = blend(color, palette(tone, "616761", "9b9b8c", "bbb4a0"), plates * 0.34)
        moss = smooth(0.65, 0.90, field) * smooth(0.33, 0.7, brush) * 0.74
        color = blend(color, palette(field, "535d42", "7e8560"), moss)
        return color, micro_relief * 0.84 + relief * 0.19 + ledges * 0.063, 7.3, 0.44
    if name == "sand":
        phase = tile.y * 12 + tile.x * 2 + (field - 0.5) * 1.85
        ripple = (np.sin(phase * (2 * np.pi)) * 0.5 + 0.5) ** 2
        color = palette(pigment * 0.84 + ripple * 0.13, "ac8a55", "c1a16a", "d7ba82", "e6cf9a")
        return color, micro_relief * 0.54 + ripple * 0.052, 3.6, 0.18
    if name == "snow":
        drift = tile.noise(9, 16, warp)
        color = palette(pigment * 0.81 + drift * 0.17, "9caec0", "bbcbd4", "dbe5e6", "f0f1e6")
        return color, micro_relief * 0.52 + drift * 0.16, 3.2, 0.18
    if name == "mud":
        pools = 1 - smooth(0.28, 0.51, pigment)
        color = palette(pigment, "484838", "5f5540", "78674d", "a08a66")
        color = blend(color, palette(field, "46483c", "62614e"), pools * 0.58)
        return color, micro_relief * (1 - pools * 0.50) - pools * 0.10, 4.1, 0.34
    if name == "gravel":
        chips, relief, tone = tile.stones(13, 15, 0.96, 1.25)
        small, small_relief, small_tone = tile.stones(23, 29, 0.78, 1.14)
        color = palette(pigment, "615c4d", "7d7360", "968a72")
        color = blend(color, palette(small_tone, "676656", "918977", "a69f8e"), small * 0.67)
        color = blend(color, palette(tone, "6c6d61", "9d9783", "b0a793"), chips * 0.91)
        return color, micro_relief * 0.55 + np.maximum(relief * 0.18, small_relief * 0.08), 5.9, 0.42
    cinders, relief, tone = tile.stones(15, 19, 0.72, 1.27)
    seams = tile.noise(9, 22, warp)
    color = palette(pigment * 0.8 + seams * 0.2, "393638", "514945", "6b5a54", "92776a")
    color = blend(color, palette(tone, "4a4544", "746159", "917970"), cinders * 0.65)
    return color, micro_relief + relief * 0.14 + seams * 0.058, 5.1, 0.41


def surface_map(height, normal_strength, ao_strength):
    # Central differences wrap at the seam. Green follows increasing texture V.
    du = (np.roll(height, -1, axis=1) - np.roll(height, 1, axis=1)) * 0.5
    dv = (np.roll(height, -1, axis=0) - np.roll(height, 1, axis=0)) * 0.5
    normal = np.stack([-du * normal_strength, -dv * normal_strength, np.ones_like(height)], axis=-1)
    normal /= np.linalg.norm(normal, axis=-1, keepdims=True)

    # Estimate visibility from the relief in eight directions at three radii.
    occlusion = np.zeros_like(height)
    for dx, dy in ((1, 0), (1, 1), (0, 1), (-1, 1), (-1, 0), (-1, -1), (0, -1), (1, -1)):
        horizon = np.zeros_like(height)
        for radius in (3, 9, 21):
            sample = np.roll(height, (-radius * dy, -radius * dx), (0, 1))
            horizon = np.maximum(horizon, (sample - height) / (radius * np.hypot(dx, dy)))
        occlusion += np.arctan(44 * horizon) / (np.pi / 2)
    ao = 1 - ao_strength * occlusion / 8
    return np.dstack([normal * 127.5 + 127.5, ao * 255])


def image_from(array):
    return Image.fromarray(np.clip(np.rint(array), 0, 255).astype(np.uint8))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=Path(__file__).resolve().parents[1] /
                        "assets/terrain/textures")
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    size = 512
    atlas = Image.new("RGB", (4 * size, 2 * size))
    for index, name in enumerate(LAYER_NAMES):
        color, height, normal_strength, ao_strength = render_landform(name, Tile(size, 8041 + index))
        albedo = image_from(color)
        albedo.save(args.output / f"terrain_{name}.png", optimize=True)
        image_from(surface_map(height, normal_strength, ao_strength)).save(
            args.output / f"terrain_{name}_surface.png", optimize=True
        )
        atlas.paste(albedo, ((index % 4) * size, (index // 4) * size))
    atlas.save(args.output / "terrain_atlas.png", optimize=True)


if __name__ == "__main__":
    main()
