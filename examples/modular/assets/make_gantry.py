"""Regenerate the wholly authored gantry GLB and its package integrity entry."""

import hashlib
import json
from pathlib import Path
import struct


def cuboid(low, high):
    x0, y0, z0 = low
    x1, y1, z1 = high
    vertices = [
        (x0, y0, z0), (x1, y0, z0), (x1, y0, z1), (x0, y0, z1),
        (x0, y1, z0), (x1, y1, z0), (x1, y1, z1), (x0, y1, z1),
    ]
    triangles = [
        (0, 1, 2), (0, 2, 3), (4, 6, 5), (4, 7, 6),
        (0, 4, 5), (0, 5, 1), (3, 2, 6), (3, 6, 7),
        (1, 5, 6), (1, 6, 2), (0, 3, 7), (0, 7, 4),
    ]
    return vertices, triangles


def main():
    root = Path(__file__).resolve().parent
    binary = bytearray()
    views, accessors, meshes = [], [], []

    def accessor(values, component, shape, packing):
        while len(binary) % 4:
            binary.append(0)
        start = len(binary)
        for value in values:
            binary.extend(struct.pack(packing, *value))
        view = len(views)
        views.append({"buffer": 0, "byteOffset": start, "byteLength": len(binary) - start})
        index = len(accessors)
        accessors.append({"bufferView": view, "componentType": component,
                          "count": len(values), "type": shape})
        return index

    def mesh(boxes, material):
        vertices, indices = [], []
        for low, high in boxes:
            box_vertices, triangles = cuboid(low, high)
            offset = len(vertices)
            vertices.extend(box_vertices)
            indices.extend((index + offset,) for triangle in triangles for index in triangle)
        positions = accessor(vertices, 5126, "VEC3", "<3f")
        accessors[positions]["min"] = [min(v[i] for v in vertices) for i in range(3)]
        accessors[positions]["max"] = [max(v[i] for v in vertices) for i in range(3)]
        triangles = accessor(indices, 5123, "SCALAR", "<H")
        meshes.append({"primitives": [{"attributes": {"POSITION": positions},
                                        "indices": triangles, "material": material}]})
        return len(meshes) - 1

    frame = mesh([((-9, 0, -1.5), (-7, 12, 1.5)),
                  ((7, 0, -1.5), (9, 12, 1.5)),
                  ((-9, 11, -1.5), (9, 13, 1.5))], 0)
    control = mesh([((-2, 0, -2), (2, 2, 2))], 1)
    beacon = mesh([((-0.8, 0, -0.8), (0.8, 1.5, 0.8))], 2)
    while len(binary) % 4:
        binary.append(0)
    colors = [(0.45, 0.48, 0.52, 1), (0.95, 0.45, 0.1, 1), (0.1, 0.85, 1, 1)]
    document = {
        "asset": {"version": "2.0", "generator": "Core Critical authored gantry example"},
        "buffers": [{"byteLength": len(binary)}],
        "bufferViews": views, "accessors": accessors, "meshes": meshes,
        "materials": [{"pbrMetallicRoughness": {"baseColorFactor": color,
                                                  "metallicFactor": 0, "roughnessFactor": 1}}
                      for color in colors],
        "nodes": [
            {"name": "Gantry", "mesh": frame, "children": [1]},
            {"name": "Control", "translation": [0, 11, 0], "mesh": control, "children": [2]},
            {"name": "Beacon", "translation": [0, 2.5, 0], "mesh": beacon},
        ],
        "scenes": [{"nodes": [0]}], "scene": 0,
    }
    encoded = json.dumps(document, sort_keys=True, separators=(",", ":")).encode("utf-8")
    encoded += b" " * (-len(encoded) % 4)
    payload = (struct.pack("<III", 0x46546C67, 2, 28 + len(encoded) + len(binary))
               + struct.pack("<II", len(encoded), 0x4E4F534A) + encoded
               + struct.pack("<II", len(binary), 0x004E4942) + binary)
    (root / "gantry.glb").write_bytes(payload)
    path = root / "pack.json"
    manifest = json.loads(path.read_text(encoding="utf-8"))
    manifest["files"]["gantry.glb"] = {
        "bytes": len(payload), "sha256": hashlib.sha256(payload).hexdigest(),
    }
    path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
