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

    def rock_faces(self, cells, warp):
        """Measure distance to irregular, wrapping stone face boundaries."""
        x, y = (self.x + warp[0]) * cells, (self.y + warp[1]) * cells
        i, j = np.floor(x).astype(np.int32), np.floor(y).astype(np.int32)
        seeds = self.rng.random((3, cells, cells), dtype=np.float32)
        nearest = np.full_like(x, np.inf)
        second = nearest.copy()
        face_tone = np.zeros_like(x)
        for near_y in (-1, 0, 1):
            for near_x in (-1, 0, 1):
                k, l = i + near_x, j + near_y
                sx, sy, tone = seeds[:, l % cells, k % cells]
                dx, dy = x - k - (0.25 + 0.50 * sx), y - l - (0.25 + 0.50 * sy)
                distance = dx * dx + dy * dy
                closer = distance < nearest
                second = np.where(closer, nearest, np.minimum(second, distance))
                nearest = np.minimum(nearest, distance)
                face_tone = np.where(closer, tone, face_tone)
        return np.sqrt(second) - np.sqrt(nearest), face_tone


def blend(base, overlay, weight):
    return base * (1 - weight[..., None]) + overlay * weight[..., None]


def render_landform(name, tile):
    warp = (0.21 * (tile.noise(4, 5) - 0.5), 0.21 * (tile.noise(5, 4) - 0.5))
    field = tile.noise(6, 7, warp)
    brush = tile.noise(21, 25, warp)
    grain = tile.noise(68, 77, warp)
    flecks = tile.noise(131, 127)
    pigment = 0.53 * field + 0.30 * brush + 0.12 * grain + 0.05 * flecks
    micro_relief = 0.11 * field + 0.13 * brush + 0.039 * grain + 0.014 * flecks
    if name == "grass":
        wisps = tile.noise(38, 62, warp)
        leaves, leaf_height, leaf_tone = tile.stones(48, 59, 0.64, 1.46)
        color = palette(pigment, "5d763e", "779751", "92aa60", "b0bb78")
        color = blend(color, palette(leaf_tone, "697f43", "9fb565", "bac17a"), leaves * 0.21)
        color += ((wisps - 0.5) * 8 + (grain - 0.5) * 6)[..., None] * [0.85, 1, 0.5]
        return color, micro_relief * 0.58 + leaf_height * 0.024 + wisps * 0.014, 4.4, 0.20
    if name == "dirt":
        chips, relief, tone = tile.stones(29, 24, 0.75, 1.28)
        streaks = tile.noise(17, 35, warp)
        color = palette(pigment * 0.82 + streaks * 0.18, "7a5b3e", "a18055", "ba9b65", "d5bc84")
        color = blend(color, palette(tone, "867455", "ab986e", "d2bb8b"), chips * 0.48)
        return color, micro_relief * 0.71 + relief * 0.106 + streaks * 0.024, 5.0, 0.32
    if name == "rock":
        fracture_warp = (
            0.075 * (tile.noise(8, 9, warp) - 0.5),
            0.075 * (tile.noise(9, 8, warp) - 0.5),
        )
        edge, face_tone = tile.rock_faces(8, fracture_warp)
        mottling = tile.noise(12, 12, warp)
        # Fade across the boundary so only the broken joints have hard edges.
        face = (face_tone - 0.5) * smooth(0, 0.21, edge)
        exposed = smooth(0.29, 0.68, 0.55 * brush + 0.45 * grain)
        joints = (1 - smooth(0.013, 0.060, edge)) * exposed
        stone = 0.65 * pigment + 0.20 * mottling + 0.15 * grain + 0.14 * face
        color = palette(stone, "535c5b", "747c77", "9b9c91", "b2ab96")
        minerals = smooth(0.58, 0.81, 0.60 * mottling + 0.30 * brush + 0.10 * grain)
        color = blend(color, palette(grain, "938772", "a79b80", "b6aa8a"), minerals * 0.40)
        color -= (joints * 24)[..., None] * [0.94, 1, 1.07]
        height = micro_relief * 0.80 + face * 0.035 + mottling * 0.055 - joints * 0.058
        return color, height, 7.2, 0.43
    if name == "sand":
        phase = tile.y * 18 + tile.x * 2 + (field - 0.5) * 2.3 + (brush - 0.5) * 0.46
        ripple = smooth(-0.72, 0.86, np.sin(phase * (2 * np.pi)))
        color = palette(pigment * 0.78 + ripple * 0.15 + grain * 0.07, "af8f58", "c4a56d", "dbba7c", "edcf97")
        return color, micro_relief * 0.39 + ripple * 0.035, 3.5, 0.16
    if name == "snow":
        drift = tile.noise(10, 26, warp)
        ice = tile.noise(27, 39, warp)
        color = palette(pigment * 0.57 + drift * 0.33 + ice * 0.10, "a5bbc6", "c3d5dc", "dde8ea", "f3f2e7")
        return color, micro_relief * 0.42 + drift * 0.11, 2.9, 0.15
    if name == "mud":
        pools = 1 - smooth(0.27, 0.56, 0.72 * field + 0.28 * brush)
        color = palette(pigment, "4c4836", "65573d", "877251", "ad935f")
        color = blend(color, palette(field, "4c533c", "72764f"), pools * 0.63)
        return color, micro_relief * (1 - pools * 0.52) - pools * 0.079, 4.3, 0.31
    if name == "gravel":
        chips, relief, tone = tile.stones(21, 25, 0.76, 1.26)
        small, small_relief, small_tone = tile.stones(40, 43, 0.72, 1.13)
        color = palette(pigment, "6d6b57", "898370", "a3987a", "cabc96")
        color = blend(color, palette(small_tone, "767566", "a5a18b", "bbb297"), small * 0.45)
        color = blend(color, palette(tone, "73766d", "a5a698", "c3b392"), chips * 0.68)
        return color, micro_relief * 0.48 + np.maximum(relief * 0.128, small_relief * 0.067), 5.7, 0.38
    cinders, relief, tone = tile.stones(28, 33, 0.68, 1.30)
    seams = tile.noise(16, 31, warp)
    color = palette(pigment * 0.74 + seams * 0.26, "434545", "605753", "866e60", "a78a75")
    color = blend(color, palette(tone, "535452", "816e62", "9f8474"), cinders * 0.55)
    return color, micro_relief * 0.65 + relief * 0.10 + seams * 0.041, 5.0, 0.37


def world_controls(size):
    """Bake terrain-wide moisture, iron, sediment and light variation into linear RGBA."""
    field = Tile(size, 42109)
    warp = (0.13 * (field.noise(7, 9) - 0.5), 0.13 * (field.noise(9, 7) - 0.5))
    moisture = 0.66 * field.noise(9, 12, warp) + 0.34 * field.noise(30, 36, warp)
    iron = 0.77 * field.noise(7, 8, warp) + 0.23 * field.noise(22, 26, warp)
    sediment = 0.60 * field.noise(18, 24, warp) + 0.40 * field.noise(63, 55, warp)
    light = 0.67 * field.noise(14, 18, warp) + 0.33 * field.noise(42, 48, warp)
    return image_from(np.dstack([moisture, iron, sediment, light]) * 255.0)


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
    world_controls(1024).save(args.output / "terrain_macro.png", optimize=True)


if __name__ == "__main__":
    main()
