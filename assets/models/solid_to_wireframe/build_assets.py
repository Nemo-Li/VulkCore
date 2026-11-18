"""Build the original solid sedan and import the supplied line ribbons into standard glTF.

Usage: python3 build_assets.py /path/to/solid-to-wireframe/assets/models/sm_linecar_uncompressed.glb
"""
import json
import math
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parent


class Glb:
    def __init__(self):
        self.data = bytearray()
        self.doc = dict(asset=dict(version="2.0"), scenes=[dict(nodes=[])], scene=0,
                        nodes=[], meshes=[], accessors=[], bufferViews=[])

    def accessor(self, rows, kind, indices=False):
        binary = b"".join(struct.pack("<" + ("I" if indices else "f") * len(row), *row)
                          for row in rows)
        view = len(self.doc["bufferViews"])
        self.doc["bufferViews"].append(dict(buffer=0, byteOffset=len(self.data), byteLength=len(binary)))
        self.data.extend(binary)
        entry = dict(bufferView=view, componentType=5125 if indices else 5126, count=len(rows), type=kind)
        if kind == "VEC3":
            entry.update(min=list(map(min, zip(*rows))), max=list(map(max, zip(*rows))))
        self.doc["accessors"].append(entry)
        return len(self.doc["accessors"]) - 1

    def mesh(self, name, vertices, faces, color):
        attrs = dict(POSITION=self.accessor(vertices, "VEC3"),
                     COLOR_0=self.accessor([color] * len(vertices), "VEC4"))
        indices = self.accessor([(index,) for face in faces for index in face], "SCALAR", True)
        self.doc["meshes"].append(dict(name=name, primitives=[dict(attributes=attrs, indices=indices)]))
        self.doc["scenes"][0]["nodes"].append(len(self.doc["nodes"]))
        self.doc["nodes"].append(dict(name=name, mesh=len(self.doc["meshes"]) - 1))

    def save(self, name):
        self.doc["buffers"] = [dict(byteLength=len(self.data))]
        document = json.dumps(self.doc, separators=(",", ":"), ensure_ascii=False).encode()
        document += b" " * (-len(document) % 4)
        payload = struct.pack("<I4s", len(document), b"JSON") + document
        payload += struct.pack("<I4s", len(self.data), b"BIN\0") + self.data
        (ROOT / name).write_bytes(struct.pack("<4sII", b"glTF", 2, len(payload) + 12) + payload)


def copy_ribbons(path):
    data = path.read_bytes()
    length, = struct.unpack_from("<I", data, 12)
    src = json.loads(data[20:20 + length])
    start = 28 + length
    glb = Glb()
    types = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4}
    formats = {5120: ("b", 127), 5122: ("h", 32767), 5123: ("H", 65535), 5126: ("f", 1)}

    def convert(index, is_index=False):
        acc = src["accessors"][index]
        view = src["bufferViews"][acc["bufferView"]]
        fmt, divisor = formats[acc["componentType"]]
        row = struct.Struct("<" + fmt * types[acc["type"]])
        offset = start + view.get("byteOffset", 0) + acc.get("byteOffset", 0)
        stride = view.get("byteStride", row.size)
        values = [row.unpack_from(data, offset + i * stride) for i in range(acc["count"])]
        if acc.get("normalized"):
            values = [tuple(max(-1.0, x / divisor) for x in value) for value in values]
        return glb.accessor(values, acc["type"], is_index)

    for mesh in src["meshes"]:
        primitives = []
        for primitive in mesh["primitives"]:
            attrs = {key: convert(primitive["attributes"][key])
                     for key in ("POSITION", "NORMAL", "TEXCOORD_0", "COLOR_0")}
            primitives.append(dict(attributes=attrs, indices=convert(primitive["indices"], True)))
        glb.doc["meshes"].append(dict(name=mesh["name"], primitives=primitives))
    glb.doc["nodes"] = src["nodes"]
    glb.doc["scenes"] = src["scenes"]
    glb.save("line_ribbons.glb")


def loft(glb, name, rings, color):
    width = len(rings[0])
    faces = []
    vertices = [vertex for ring in rings for vertex in ring]
    for i in range(len(rings) - 1):
        for j in range(width):
            a = i * width + j
            b = i * width + (j + 1) % width
            faces.extend([(a, b, b + width), (a, b + width, a + width)])
    # Cap both ends with fans. The quad winding also keeps exterior normals consistent.
    for j in range(1, width - 1):
        base = (len(rings) - 1) * width
        faces.extend([(0, j + 1, j), (base, base + j, base + j + 1)])
    glb.mesh(name, vertices, faces, color)


def mix(a, b, t):
    return a * (1.0 - t) + b * t


def body(glb):
    # X runs nose to tail, Y is up, and Z is the width of the car.
    stations = [
        (-2.64, .44, .74), (-2.59, .65, .88), (-2.49, .82, .97),
        (-2.28, .95, 1.05), (-1.90, 1.01, 1.08), (-1.42, 1.00, 1.10),
        (-.90, .99, 1.06), (-.20, .99, 1.02), (.58, 1.04, 1.005),
        (1.22, .97, 1.035), (1.62, .91, 1.055), (2.05, .77, 1.01),
        (2.34, .66, .94), (2.54, .55, .83), (2.60, .41, .67)
    ]
    rings = []
    for start, end in zip(stations, stations[1:]):
        for step in range(7):
            x, roof, wide = (mix(a, b, step / 7) for a, b in zip(start, end))
            rings.append((x, roof, wide))
    rings.append(stations[-1])
    section = [(0, 1), (.65, .995), (.89, .97), (.98, .90), (1, .74),
               (.99, .51), (.91, .21), (.82, .05), (0, 0),
               (-.82, .05), (-.91, .21), (-.99, .51), (-1, .74),
               (-.98, .90), (-.89, .97), (-.65, .995)]
    cross_sections = []
    for x, roof, wide in rings:
        # Flared rocker panels lift over each tyre and close underneath the cabin.
        radial = min(abs(x - 1.571), abs(x + 1.485))
        arch = .37 + math.sqrt(max(0.0, .395 ** 2 - radial ** 2)) if radial < .395 else .22
        bottom = min(max(.22, arch), roof - .035)
        cross_sections.append([(x, mix(bottom, roof, y), wide * z) for z, y in section])
    loft(glb, "Painted body", cross_sections, (.075, .36, .43, 1))

    cabin = [
        (-1.86, .998, .76, .70), (-1.53, 1.11, .81, .64),
        (-1.15, 1.36, .82, .59), (-.78, 1.49, .81, .57),
        (-.35, 1.56, .81, .57), (.19, 1.56, .81, .57),
        (.61, 1.46, .80, .59), (.94, 1.25, .76, .62),
        (1.28, 1.02, .73, .69)
    ]
    rings = []
    for x, top, width, crown in cabin:
        base = .95
        rings.append([(x, base, -width), (x, base, width), (x, mix(base, top, .90), crown),
                      (x, top, crown * .72), (x, top, -crown * .72),
                      (x, mix(base, top, .90), -crown)])
    loft(glb, "Panoramic glazing", rings, (.020, .044, .059, 1))

    # Flush brightwork and the B-pillars break up the continuous cabin glass.
    for side in (-1, 1):
        for x in (-.39, -.34):
            profile = [[(x + dx, y, side * z) for dx, y, z in
                        [(-.014, .98, .818), (.014, .98, .818),
                         (.014, 1.49, .612), (-.014, 1.49, .612)]]]
            glb.mesh("Window pillar", profile[0], [(0, 1, 2), (0, 2, 3)], (.029, .066, .078, 1))
        for end in (-1, 1):
            points = ([(2.51, .57, .48), (2.49, .63, .66), (2.36, .68, .87),
                       (2.21, .71, .94), (2.20, .67, .944), (2.50, .55, .49)] if end > 0
                      else [(-2.59, .57, .05), (-2.58, .59, .73), (-2.52, .61, .895),
                            (-2.50, .58, .90), (-2.57, .55, .73), (-2.60, .55, .05)])
            glb.mesh("Headlamp" if end > 0 else "Taillamp",
                     [(x, y, side * z) for x, y, z in points],
                     [(0, i, i + 1) for i in range(1, 5)],
                     (.52, .83, 1, 1) if end > 0 else (.72, .045, .052, 1))


def wheels(glb):
    def ring(x, side, offset, radius):
        return [(x + radius * math.cos(a), .371 + radius * math.sin(a), side * offset)
                for a in (2 * math.pi * j / 48 for j in range(48))]

    for x in (-1.485, 1.571):
        for side in (-1, 1):
            loft(glb, "Tyre", [ring(x, side, offset, radius) for offset, radius in
                              [(.698, .311), (.72, .351), (.75, .373),
                               (.871, .373), (.903, .34), (.920, .279)]],
                 (.021, .029, .035, 1))
            loft(glb, "Alloy wheel", [ring(x, side, z, r) for z, r in
                                      [(.919, .265), (.928, .257), (.933, .24)]],
                 (.084, .119, .14, 1))
            for spoke in range(10):
                a = spoke * math.tau / 10
                points = []
                for radius, angle in [(.075, a - .12), (.241, a + .14),
                                      (.240, a + .25), (.073, a + .15)]:
                    points.append((x + radius * math.cos(angle), .371 + radius * math.sin(angle), side * .936))
                glb.mesh("Wheel spoke", points, [(0, 1, 2), (0, 2, 3)], (.34, .44, .50, 1))
            loft(glb, "Wheel centre", [ring(x, side, .933, .082), ring(x, side, .942, .068)],
                 (.14, .19, .22, 1))


def stage():
    glb = Glb()
    glb.mesh("Floor", [(-35, -.03, -35), (-35, -.03, 35), (35, -.03, 35), (35, -.03, -35)],
             [(0, 1, 2), (0, 2, 3)], (.019608, .031373, .05098, 1))
    glb.mesh("Grid", [(-12, 0, -12), (-12, 0, 12), (12, 0, 12), (12, 0, -12)],
             [(0, 1, 2), (0, 2, 3)], (.109804, .192157, .25098, .1))
    glb.save("stage.glb")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    copy_ribbons(Path(sys.argv[1]))
    glb = Glb()
    body(glb)
    wheels(glb)
    glb.save("solid_sedan.glb")
    stage()
