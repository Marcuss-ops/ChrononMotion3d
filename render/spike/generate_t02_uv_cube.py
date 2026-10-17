#!/usr/bin/env python3
"""Generate a deterministic, self-contained numbered UV-atlas GLB for T02."""

from __future__ import annotations

import json
import struct
import zlib
from pathlib import Path


def png_chunk(kind: bytes, data: bytes) -> bytes:
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)


def make_png() -> bytes:
    cell_w = cell_h = 64
    width, height = 3 * cell_w, 2 * cell_h
    colors = [
        ((227, 78, 65), (102, 29, 30)),
        ((61, 183, 205), (21, 75, 98)),
        ((91, 195, 111), (33, 91, 50)),
        ((219, 174, 66), (104, 64, 22)),
        ((148, 106, 215), (63, 39, 106)),
        ((226, 116, 178), (113, 43, 79)),
    ]
    glyphs = {
        "0": ("111", "101", "101", "101", "111"),
        "1": ("010", "110", "010", "010", "111"),
        "2": ("111", "001", "111", "100", "111"),
        "3": ("111", "001", "111", "001", "111"),
        "4": ("101", "101", "111", "001", "001"),
        "5": ("111", "100", "111", "001", "111"),
    }
    rows = []
    for y in range(height):
        row = bytearray([0])
        for x in range(width):
            col, local_x = divmod(x, cell_w)
            row_id, local_y = divmod(y, cell_h)
            face = row_id * 3 + col
            light, dark = colors[face]
            checker = ((local_x // 8) + (local_y // 8)) & 1
            rgb = dark if checker else light
            # A high-contrast, centered face index provides an orientation and
            # atlas-region landmark without adding external font dependencies.
            gx, gy = local_x - 22, local_y - 17
            if 0 <= gx < 20 and 0 <= gy < 30:
                cx, cy = gx // 6, gy // 6
                if cx < 3 and cy < 5 and glyphs[str(face)][cy][cx] == "1":
                    rgb = (250, 244, 220)
            row.extend((*rgb, 255))
        rows.append(bytes(row))
    raw = b"".join(rows)
    return (
        b"\x89PNG\r\n\x1a\n"
        + png_chunk(b"IHDR", struct.pack(">2I5B", width, height, 8, 6, 0, 0, 0))
        + png_chunk(b"IDAT", zlib.compress(raw, 9))
        + png_chunk(b"IEND", b"")
    )


def generate(path: Path) -> None:
    h = 0.8
    faces = [
        ((0, 0, 1), ((-h, -h, h), (h, -h, h), (h, h, h), (-h, h, h))),
        ((0, 0, -1), ((h, -h, -h), (-h, -h, -h), (-h, h, -h), (h, h, -h))),
        ((0, 1, 0), ((-h, h, h), (h, h, h), (h, h, -h), (-h, h, -h))),
        ((0, -1, 0), ((-h, -h, -h), (h, -h, -h), (h, -h, h), (-h, -h, h))),
        ((1, 0, 0), ((h, -h, h), (h, -h, -h), (h, h, -h), (h, h, h))),
        ((-1, 0, 0), ((-h, -h, -h), (-h, -h, h), (-h, h, h), (-h, h, -h))),
    ]
    blob = bytearray()
    views, accessors, primitives = [], [], []

    def append_data(data: bytes, target: int, accessor: dict) -> int:
        while len(blob) % 4:
            blob.append(0)
        offset = len(blob)
        blob.extend(data)
        views.append({"buffer": 0, "byteOffset": offset, "byteLength": len(data), "target": target})
        accessor["bufferView"] = len(views) - 1
        accessors.append(accessor)
        return len(accessors) - 1

    uv_corners = ((0.08, 0.08), (0.92, 0.08), (0.92, 0.92), (0.08, 0.92))
    for face_id, (normal, verts) in enumerate(faces):
        col, row_id = face_id % 3, face_id // 3
        positions = b"".join(struct.pack("<3f", *v) for v in verts)
        normals = b"".join(struct.pack("<3f", *normal) for _ in range(4))
        uv = b"".join(
            struct.pack("<2f", (col + u) / 3.0, (row_id + v) / 2.0)
            for u, v in uv_corners
        )
        indices = struct.pack("<6H", 0, 1, 2, 0, 2, 3)
        pos_accessor = append_data(positions, 34962, {
            "componentType": 5126, "count": 4, "type": "VEC3",
            "min": [min(v[j] for v in verts) for j in range(3)],
            "max": [max(v[j] for v in verts) for j in range(3)],
        })
        normal_accessor = append_data(normals, 34962, {"componentType": 5126, "count": 4, "type": "VEC3"})
        uv_accessor = append_data(uv, 34962, {"componentType": 5126, "count": 4, "type": "VEC2"})
        index_accessor = append_data(indices, 34963, {"componentType": 5123, "count": 6, "type": "SCALAR"})
        primitives.append({
            "attributes": {"POSITION": pos_accessor, "NORMAL": normal_accessor, "TEXCOORD_0": uv_accessor},
            "indices": index_accessor,
            "material": 0,
        })

    image = make_png()
    while len(blob) % 4:
        blob.append(0)
    image_offset = len(blob)
    blob.extend(image)
    views.append({"buffer": 0, "byteOffset": image_offset, "byteLength": len(image)})
    doc = {
        "asset": {"version": "2.0", "generator": "ChrononMotion3D T02 deterministic fixture"},
        "scene": 0, "scenes": [{"nodes": [0]}],
        "nodes": [{"name": "numbered_uv_cube", "mesh": 0}],
        "meshes": [{"name": "six_numbered_faces", "primitives": primitives}],
        "buffers": [{"byteLength": len(blob)}], "bufferViews": views, "accessors": accessors,
        "images": [{"bufferView": len(views) - 1, "mimeType": "image/png"}],
        "samplers": [{"magFilter": 9729, "minFilter": 9987, "wrapS": 10497, "wrapT": 10497}],
        "textures": [{"sampler": 0, "source": 0}],
        "materials": [{"name": "numbered_uv_checker", "pbrMetallicRoughness": {
            "baseColorFactor": [1, 1, 1, 1], "baseColorTexture": {"index": 0},
            "metallicFactor": 0, "roughnessFactor": 0.82,
        }}],
    }
    json_bytes = json.dumps(doc, separators=(",", ":")).encode()
    json_bytes += b" " * ((-len(json_bytes)) % 4)
    blob += b"\0" * ((-len(blob)) % 4)
    total = 12 + 8 + len(json_bytes) + 8 + len(blob)
    glb = struct.pack("<III", 0x46546C67, 2, total)
    glb += struct.pack("<II", len(json_bytes), 0x4E4F534A) + json_bytes
    glb += struct.pack("<II", len(blob), 0x004E4942) + blob
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(glb)


if __name__ == "__main__":
    generate(Path(__file__).with_name("assets") / "threepp-certification" / "NumberedUVCube.glb")
