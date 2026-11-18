#!/usr/bin/env python3
"""Generate the layered, stylized tornado used by the tornado VFX shader.

The mesh deliberately contains several kinds of translucent surface in one
draw part. The integer UV bands identify those surfaces to the shader:

    0  translucent funnel core
    1  broad hero ribbons
    2  thin orbiting wisps and upward splashes
    3  ground-impact rings
    4  detached glowing shards

UV.x also carries a deterministic per-piece seed in its integer portion. The
fractional part remains a regular along-strip coordinate.
"""

from __future__ import annotations

import math
import random
from dataclasses import dataclass
from pathlib import Path


TAU = math.tau
HEIGHT = 6.0
CORE_KIND = 0
RIBBON_KIND = 1
WISP_KIND = 2
RING_KIND = 3
SHARD_KIND = 4


@dataclass
class MeshWriter:
    lines: list[str]
    vertex_count: int = 0
    uv_count: int = 0

    def vertex(self, position: tuple[float, float, float]) -> int:
        self.vertex_count += 1
        self.lines.append("v {:.6f} {:.6f} {:.6f}".format(*position))
        return self.vertex_count

    def uv(self, along: float, across: float, kind: int, seed: int = 0) -> int:
        self.uv_count += 1
        encoded_u = seed * 2.0 + max(0.0, min(0.999999, along))
        encoded_v = kind * 2.0 + max(0.0, min(1.0, across))
        self.lines.append(f"vt {encoded_u:.6f} {encoded_v:.6f}")
        return self.uv_count

    def quad(
        self,
        vertices: tuple[int, int, int, int],
        uvs: tuple[int, int, int, int],
    ) -> None:
        self.lines.append(
            "f " + " ".join(f"{vertex}/{uv}" for vertex, uv in zip(vertices, uvs))
        )


def add_core(writer: MeshWriter) -> None:
    """Add two nested, uneven funnel skins for translucent interior depth."""

    ring_count = 38
    segment_count = 48
    for shell in range(2):
        shell_vertices: list[list[int]] = []
        shell_uvs: list[list[int]] = []
        shell_scale = 1.0 if shell == 0 else 0.72
        phase = shell * 0.43
        for ring in range(ring_count):
            height_ratio = ring / (ring_count - 1)
            y = 0.34 + 5.25 * height_ratio
            flare = 0.085 + 1.82 * math.pow(height_ratio, 1.22)
            shoulder = 0.22 * math.exp(-((height_ratio - 0.68) / 0.20) ** 2)
            base_radius = (flare + shoulder) * shell_scale
            center_x = 0.10 * math.sin(height_ratio * TAU * 1.15 + phase)
            center_z = 0.07 * math.cos(height_ratio * TAU * 0.85 + phase)
            vertices: list[int] = []
            uvs: list[int] = []
            for segment in range(segment_count + 1):
                angle_ratio = segment / segment_count
                theta = angle_ratio * TAU + height_ratio * 1.55 + phase
                lobes = 1.0 + 0.075 * math.sin(theta * 3.0 - height_ratio * 8.0 + phase)
                radius = base_radius * lobes
                vertices.append(writer.vertex((
                    center_x + radius * math.cos(theta),
                    y,
                    center_z + radius * math.sin(theta),
                )))
                writer.uv_count += 1
                writer.lines.append(
                    f"vt {angle_ratio + shell * 2.0:.6f} "
                    f"{CORE_KIND * 2.0 + height_ratio:.6f}"
                )
                uvs.append(writer.uv_count)
            shell_vertices.append(vertices)
            shell_uvs.append(uvs)

        writer.lines.append(f"g FunnelCore_{shell}")
        writer.lines.append("s 1")
        for ring in range(ring_count - 1):
            for segment in range(segment_count):
                writer.quad(
                    (
                        shell_vertices[ring][segment],
                        shell_vertices[ring + 1][segment],
                        shell_vertices[ring + 1][segment + 1],
                        shell_vertices[ring][segment + 1],
                    ),
                    (
                        shell_uvs[ring][segment],
                        shell_uvs[ring + 1][segment],
                        shell_uvs[ring + 1][segment + 1],
                        shell_uvs[ring][segment + 1],
                    ),
                )


def tapered_width(along: float, width: float, start_taper: float, end_taper: float) -> float:
    lead = min(1.0, along / max(start_taper, 1e-4))
    tail = min(1.0, (1.0 - along) / max(end_taper, 1e-4))
    return width * math.pow(max(0.0, min(lead, tail)), 0.48)


def add_helix_ribbon(
    writer: MeshWriter,
    *,
    seed: int,
    kind: int,
    y_start: float,
    y_end: float,
    radius_start: float,
    radius_end: float,
    phase: float,
    turns: float,
    width: float,
    radial_width: float = 0.22,
    segments: int = 56,
) -> None:
    """Add a broad, vertically biased strip following a broken helix arc."""

    left_vertices: list[int] = []
    right_vertices: list[int] = []
    left_uvs: list[int] = []
    right_uvs: list[int] = []
    for segment in range(segments + 1):
        along = segment / segments
        eased = along * along * (3.0 - 2.0 * along)
        y = y_start + (y_end - y_start) * along
        y += 0.10 * math.sin(along * TAU * 1.35 + seed * 0.7)
        radius = radius_start + (radius_end - radius_start) * eased
        radius *= 1.0 + 0.055 * math.sin(along * TAU * 2.0 + seed)
        theta = phase + turns * TAU * along
        radial = (math.cos(theta), 0.0, math.sin(theta))
        center = (radial[0] * radius, y, radial[2] * radius)
        half_width = 0.5 * tapered_width(along, width, 0.13, 0.18)

        roll = 0.52 + 0.28 * math.sin(along * TAU * 1.45 + seed * 1.9)
        side = (
            radial[0] * radial_width * math.cos(roll),
            math.sin(roll),
            radial[2] * radial_width * math.cos(roll),
        )
        side_length = math.sqrt(sum(component * component for component in side))
        side = tuple(component / side_length for component in side)
        left_vertices.append(writer.vertex(tuple(
            center[index] - side[index] * half_width for index in range(3)
        )))
        right_vertices.append(writer.vertex(tuple(
            center[index] + side[index] * half_width for index in range(3)
        )))
        left_uvs.append(writer.uv(along, 0.0, kind, seed))
        right_uvs.append(writer.uv(along, 1.0, kind, seed))

    writer.lines.append(f"g Ribbon_{kind}_{seed}")
    writer.lines.append("s 1")
    for segment in range(segments):
        writer.quad(
            (
                left_vertices[segment],
                right_vertices[segment],
                right_vertices[segment + 1],
                left_vertices[segment + 1],
            ),
            (
                left_uvs[segment],
                right_uvs[segment],
                right_uvs[segment + 1],
                left_uvs[segment + 1],
            ),
        )


def add_ground_arc(
    writer: MeshWriter,
    *,
    seed: int,
    radius: float,
    width: float,
    phase: float,
    arc: float,
    y: float,
    segments: int = 72,
) -> None:
    inner_vertices: list[int] = []
    outer_vertices: list[int] = []
    inner_uvs: list[int] = []
    outer_uvs: list[int] = []
    for segment in range(segments + 1):
        along = segment / segments
        theta = phase + arc * along
        taper = math.pow(max(0.0, math.sin(math.pi * along)), 0.24)
        half_width = width * taper * 0.5
        wave = 0.025 * math.sin(theta * 5.0 + seed)
        inner_vertices.append(writer.vertex((
            (radius - half_width) * math.cos(theta),
            y + wave,
            (radius - half_width) * math.sin(theta),
        )))
        outer_vertices.append(writer.vertex((
            (radius + half_width) * math.cos(theta),
            y + wave,
            (radius + half_width) * math.sin(theta),
        )))
        inner_uvs.append(writer.uv(along, 0.0, RING_KIND, seed))
        outer_uvs.append(writer.uv(along, 1.0, RING_KIND, seed))

    writer.lines.append(f"g ImpactRing_{seed}")
    writer.lines.append("s off")
    for segment in range(segments):
        writer.quad(
            (
                inner_vertices[segment],
                outer_vertices[segment],
                outer_vertices[segment + 1],
                inner_vertices[segment + 1],
            ),
            (
                inner_uvs[segment],
                outer_uvs[segment],
                outer_uvs[segment + 1],
                inner_uvs[segment + 1],
            ),
        )


def add_shard(
    writer: MeshWriter,
    *,
    seed: int,
    center: tuple[float, float, float],
    size: float,
    tilt: float,
) -> None:
    """Add two crossed tapered quads so detached sparks read from most views."""

    for cross in range(2):
        angle = tilt + cross * math.pi * 0.5
        horizontal = (math.cos(angle) * size, 0.0, math.sin(angle) * size)
        vertical = (0.0, size * 1.8, 0.0)
        points = (
            tuple(center[i] - horizontal[i] * 0.18 - vertical[i] for i in range(3)),
            tuple(center[i] + horizontal[i] - vertical[i] * 0.15 for i in range(3)),
            tuple(center[i] + horizontal[i] * 0.10 + vertical[i] for i in range(3)),
            tuple(center[i] - horizontal[i] - vertical[i] * 0.12 for i in range(3)),
        )
        vertices = tuple(writer.vertex(point) for point in points)
        uvs = (
            writer.uv(0.0, 0.0, SHARD_KIND, seed),
            writer.uv(0.999999, 0.0, SHARD_KIND, seed),
            writer.uv(0.999999, 1.0, SHARD_KIND, seed),
            writer.uv(0.0, 1.0, SHARD_KIND, seed),
        )
        writer.lines.append(f"g Shard_{seed}_{cross}")
        writer.lines.append("s off")
        writer.quad(vertices, uvs)


def generate_obj() -> str:
    writer = MeshWriter([
        "# Layered stylized tornado mesh",
        "# Generated by generate_tornado.py",
        "mtllib tornado.mtl",
        "o TornadoVFX",
        "usemtl TornadoMaterial",
    ])
    add_core(writer)

    hero_ribbons = (
        (1, 0.42, 3.70, 0.10, 1.46, 0.10, 1.20, 0.72, 0.42),
        (2, 1.05, 5.38, 0.42, 2.08, 2.25, 1.12, 0.92, 0.48),
        (3, 2.25, 5.84, 0.78, 2.48, 4.15, 0.78, 0.68, 0.54),
        (4, 2.78, 5.12, 1.12, 2.14, 0.85, 0.72, 0.78, 0.50),
        (5, 1.72, 4.38, 0.55, 1.78, 3.10, 0.92, 0.61, 0.46),
        (6, 3.44, 5.94, 1.35, 2.62, 5.20, 0.56, 0.47, 0.58),
        (7, 0.62, 2.42, 0.16, 1.18, 4.72, 0.86, 0.42, 0.38),
        (8, 2.72, 5.88, 0.82, 2.74, 2.08, 0.58, 0.84, 0.56),
        (9, 3.38, 5.98, 1.28, 2.92, 4.46, -0.46, 0.62, 0.62),
        (10, 1.86, 4.92, 0.48, 2.12, 5.64, 0.72, 0.70, 0.48),
    )
    for seed, y0, y1, r0, r1, phase, turns, width, radial_width in hero_ribbons:
        add_helix_ribbon(
            writer,
            seed=seed,
            kind=RIBBON_KIND,
            y_start=y0,
            y_end=y1,
            radius_start=r0,
            radius_end=r1,
            phase=phase,
            turns=turns,
            width=width,
            radial_width=radial_width,
        )

    thin_wisps = (
        (11, 0.26, 2.15, 0.40, 1.74, 0.40, 0.68, 0.15),
        (12, 1.20, 4.82, 1.20, 2.72, 3.55, -0.68, 0.12),
        (13, 2.64, 6.04, 1.78, 3.00, 5.35, 0.52, 0.10),
        (14, 3.80, 5.70, 2.15, 3.18, 1.40, -0.42, 0.09),
        (15, 0.18, 1.18, 0.18, 1.58, 2.72, 0.28, 0.13),
        (16, 0.12, 0.92, 0.26, 1.92, 5.00, -0.24, 0.10),
        (17, 4.55, 6.18, 2.00, 3.24, 4.48, 0.34, 0.08),
    )
    for seed, y0, y1, r0, r1, phase, turns, width in thin_wisps:
        add_helix_ribbon(
            writer,
            seed=seed,
            kind=WISP_KIND,
            y_start=y0,
            y_end=y1,
            radius_start=r0,
            radius_end=r1,
            phase=phase,
            turns=turns,
            width=width,
            radial_width=0.30,
            segments=48,
        )

    add_ground_arc(writer, seed=21, radius=1.52, width=0.20, phase=0.0, arc=TAU, y=0.10)
    add_ground_arc(writer, seed=22, radius=2.34, width=0.13, phase=0.45, arc=TAU * 0.86, y=0.07)
    add_ground_arc(writer, seed=23, radius=3.05, width=0.085, phase=2.80, arc=TAU * 0.62, y=0.05)
    add_ground_arc(writer, seed=24, radius=0.84, width=0.27, phase=1.30, arc=TAU * 0.78, y=0.13)

    for index in range(8):
        angle = index * TAU / 8.0 + (0.18 if index % 2 else 0.0)
        add_helix_ribbon(
            writer,
            seed=31 + index,
            kind=WISP_KIND,
            y_start=0.08,
            y_end=0.52 + 0.24 * (index % 3),
            radius_start=0.32,
            radius_end=1.45 + 0.20 * (index % 2),
            phase=angle,
            turns=0.05 + 0.03 * (index % 3),
            width=0.18 + 0.04 * (index % 2),
            radial_width=0.55,
            segments=22,
        )

    random_generator = random.Random(0xC1C10E)
    for index in range(34):
        angle = random_generator.uniform(0.0, TAU)
        height = random_generator.uniform(0.18, 5.92)
        height_ratio = height / HEIGHT
        radius = random_generator.uniform(1.15, 2.75) * (0.45 + height_ratio * 0.75)
        center = (
            math.cos(angle) * radius,
            height,
            math.sin(angle) * radius,
        )
        add_shard(
            writer,
            seed=51 + index,
            center=center,
            size=random_generator.uniform(0.035, 0.105),
            tilt=angle + random_generator.uniform(-0.7, 0.7),
        )

    return "\n".join(writer.lines) + "\n"


if __name__ == "__main__":
    output_path = Path(__file__).with_name("tornado.obj")
    output_path.write_text(generate_obj(), encoding="utf-8")
