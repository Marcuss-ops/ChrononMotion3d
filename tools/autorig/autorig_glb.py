#!/usr/bin/env python3
"""Minimal in-repo auto-rig / dressing / bake CLI for static GLB characters.

The engine renders static (unskinned) GLB with materials, and imports skinned
GLB only through its geometry-only motion reader. This tool bridges the gap
without any DCC:

  inspect   report what a GLB actually contains
  rig       build a humanoid skeleton + 4-influence skin weights + inverse
            bind matrices, optionally merging garment meshes onto the same rig
  validate  check a rigged GLB against the engine's skin contract
  bake      apply a procedural clip and write one static GLB per frame, so the
            existing textured static render path can play it

No third-party dependencies.
"""

import argparse
import json
import math
import os
import struct
import sys

# --------------------------------------------------------------------------
# glTF container and accessor I/O
# --------------------------------------------------------------------------

GLB_MAGIC = 0x46546C67
CHUNK_JSON = 0x4E4F534A
CHUNK_BIN = 0x004E4942

FMT = {
    5120: ("b", 1),   # BYTE
    5121: ("B", 1),   # UNSIGNED_BYTE
    5122: ("h", 2),   # SHORT
    5123: ("H", 2),   # UNSIGNED_SHORT
    5125: ("I", 4),   # UNSIGNED_INT
    5126: ("f", 4),   # FLOAT
}
NCOMP = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT4": 16}


def load_glb(path):
    with open(path, "rb") as handle:
        data = handle.read()
    if len(data) < 20:
        raise SystemExit("error: %s is not a GLB file" % path)
    magic, version, length = struct.unpack_from("<III", data, 0)
    if magic != GLB_MAGIC:
        raise SystemExit("error: %s is not a GLB file" % path)
    if version != 2:
        raise SystemExit("error: unsupported GLB version %d" % version)
    offset = 12
    doc = None
    binary = bytearray()
    while offset + 8 <= length:
        chunk_length, chunk_type = struct.unpack_from("<II", data, offset)
        offset += 8
        chunk = data[offset:offset + chunk_length]
        offset += chunk_length
        if chunk_type == CHUNK_JSON:
            doc = json.loads(chunk.decode("utf-8"))
        elif chunk_type == CHUNK_BIN:
            binary = bytearray(chunk)
    if doc is None:
        raise SystemExit("error: %s has no JSON chunk" % path)
    for buffer in doc.get("buffers", []):
        if "uri" in buffer:
            raise SystemExit("error: external buffer %r is not supported" % buffer["uri"])
    return doc, binary


def save_glb(path, doc, binary):
    buffers = doc.setdefault("buffers", [{}])
    if not buffers:
        buffers.append({})
    buffers[0].pop("uri", None)
    buffers[0]["byteLength"] = len(binary)
    js = json.dumps(doc, separators=(",", ":")).encode("utf-8")
    js += b" " * ((4 - len(js) % 4) % 4)
    blob = bytes(binary)
    blob += b"\x00" * ((4 - len(blob) % 4) % 4)
    total = 12 + 8 + len(js) + 8 + len(blob)
    with open(path, "wb") as handle:
        handle.write(struct.pack("<III", GLB_MAGIC, 2, total))
        handle.write(struct.pack("<II", len(js), CHUNK_JSON))
        handle.write(js)
        handle.write(struct.pack("<II", len(blob), CHUNK_BIN))
        handle.write(blob)


def read_accessor(doc, binary, index):
    accessor = doc["accessors"][index]
    count = accessor["count"]
    ncomp = NCOMP[accessor["type"]]
    ctype = accessor["componentType"]
    code, size = FMT[ctype]
    if "bufferView" not in accessor:
        return [tuple([0.0] * ncomp)] * count
    view = doc["bufferViews"][accessor["bufferView"]]
    base = view.get("byteOffset", 0) + accessor.get("byteOffset", 0)
    stride = view.get("byteStride") or (ncomp * size)
    pattern = "<" + code * ncomp
    values = []
    for i in range(count):
        values.append(struct.unpack_from(pattern, binary, base + i * stride))
    return values


def add_accessor(doc, binary, values, gtype, ctype, normalized=False, bounds=False):
    ncomp = NCOMP[gtype]
    code, _size = FMT[ctype]
    flat = []
    for value in values:
        if isinstance(value, (list, tuple)):
            flat.extend(value)
        else:
            flat.append(value)
    raw = struct.pack("<" + code * len(flat), *flat)
    while len(binary) % 4:
        binary.append(0)
    offset = len(binary)
    binary.extend(raw)
    doc.setdefault("bufferViews", []).append(
        {"buffer": 0, "byteOffset": offset, "byteLength": len(raw)}
    )
    accessor = {
        "bufferView": len(doc["bufferViews"]) - 1,
        "componentType": ctype,
        "count": len(values),
        "type": gtype,
    }
    if normalized:
        accessor["normalized"] = True
    if bounds and gtype == "VEC3" and values:
        accessor["min"] = [min(v[i] for v in values) for i in range(3)]
        accessor["max"] = [max(v[i] for v in values) for i in range(3)]
    doc.setdefault("accessors", []).append(accessor)
    return len(doc["accessors"]) - 1


# --------------------------------------------------------------------------
# Matrices (column-major, glTF convention: element (column, row) at c * 4 + r)
# --------------------------------------------------------------------------

def m_identity():
    return [1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0]


def m_mul(a, b):
    out = [0.0] * 16
    for col in range(4):
        for row in range(4):
            out[col * 4 + row] = sum(a[k * 4 + row] * b[col * 4 + k] for k in range(4))
    return out


def m_from_trs(t, q, s):
    x, y, z, w = q
    m = [0.0] * 16
    m[0] = (1 - 2 * (y * y + z * z)) * s[0]
    m[1] = 2 * (x * y + w * z) * s[0]
    m[2] = 2 * (x * z - w * y) * s[0]
    m[4] = 2 * (x * y - w * z) * s[1]
    m[5] = (1 - 2 * (x * x + z * z)) * s[1]
    m[6] = 2 * (y * z + w * x) * s[1]
    m[8] = 2 * (x * z + w * y) * s[2]
    m[9] = 2 * (y * z - w * x) * s[2]
    m[10] = (1 - 2 * (x * x + y * y)) * s[2]
    m[12], m[13], m[14], m[15] = t[0], t[1], t[2], 1.0
    return m


def m_translation(t):
    m = m_identity()
    m[12], m[13], m[14] = t
    return m


def m_scale(factor):
    m = m_identity()
    m[0] = m[5] = m[10] = factor
    return m


def m_rotation(axis, angle):
    x, y, z = axis
    norm = math.sqrt(x * x + y * y + z * z)
    if norm == 0.0:
        return m_identity()
    x, y, z = x / norm, y / norm, z / norm
    c, s = math.cos(angle), math.sin(angle)
    t = 1.0 - c
    return [
        t * x * x + c, t * x * y + s * z, t * x * z - s * y, 0.0,
        t * x * y - s * z, t * y * y + c, t * y * z + s * x, 0.0,
        t * x * z + s * y, t * y * z - s * x, t * z * z + c, 0.0,
        0.0, 0.0, 0.0, 1.0,
    ]


def m_point(m, p):
    x, y, z = p
    return (
        m[0] * x + m[4] * y + m[8] * z + m[12],
        m[1] * x + m[5] * y + m[9] * z + m[13],
        m[2] * x + m[6] * y + m[10] * z + m[14],
    )


def m_direction(m, p):
    x, y, z = p
    out = (
        m[0] * x + m[4] * y + m[8] * z,
        m[1] * x + m[5] * y + m[9] * z,
        m[2] * x + m[6] * y + m[10] * z,
    )
    norm = math.sqrt(out[0] ** 2 + out[1] ** 2 + out[2] ** 2)
    if norm == 0.0:
        return out
    return tuple(c / norm for c in out)


def m_inverse_affine(m):
    a, b, c = m[0], m[4], m[8]
    d, e, f = m[1], m[5], m[9]
    g, h, i = m[2], m[6], m[10]
    det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g)
    if abs(det) < 1e-12:
        raise SystemExit("error: singular transform in skinned node")
    inv = [0.0] * 16
    inv[0] = (e * i - f * h) / det
    inv[4] = (c * h - b * i) / det
    inv[8] = (b * f - c * e) / det
    inv[1] = (f * g - d * i) / det
    inv[5] = (a * i - c * g) / det
    inv[9] = (c * d - a * f) / det
    inv[2] = (d * h - e * g) / det
    inv[6] = (b * g - a * h) / det
    inv[10] = (a * e - b * d) / det
    inv[15] = 1.0
    tx, ty, tz = m[12], m[13], m[14]
    inv[12] = -(inv[0] * tx + inv[4] * ty + inv[8] * tz)
    inv[13] = -(inv[1] * tx + inv[5] * ty + inv[9] * tz)
    inv[14] = -(inv[2] * tx + inv[6] * ty + inv[10] * tz)
    return inv


def node_local(node):
    if "matrix" in node:
        return list(node["matrix"])
    return m_from_trs(
        node.get("translation", [0.0, 0.0, 0.0]),
        node.get("rotation", [0.0, 0.0, 0.0, 1.0]),
        node.get("scale", [1.0, 1.0, 1.0]),
    )


def parents_of(nodes):
    parents = {}
    for index, node in enumerate(nodes):
        for child in node.get("children", []):
            parents[child] = index
    return parents


def world_matrices(doc):
    nodes = doc.get("nodes", [])
    scene_index = doc.get("scene", 0)
    scenes = doc.get("scenes", [])
    roots = scenes[scene_index].get("nodes", []) if scenes else []
    world = {}
    stack = [(root, m_identity()) for root in roots]
    while stack:
        index, parent_matrix = stack.pop()
        if index in world:
            continue
        matrix = m_mul(parent_matrix, node_local(nodes[index]))
        world[index] = matrix
        for child in nodes[index].get("children", []):
            stack.append((child, matrix))
    return world


# --------------------------------------------------------------------------
# Character extraction
# --------------------------------------------------------------------------

class Primitive:
    def __init__(self, positions, normals, indices):
        self.positions = positions
        self.normals = normals
        self.indices = indices


def mesh_nodes(doc):
    return [i for i, node in enumerate(doc.get("nodes", [])) if "mesh" in node]


def collect_primitives(doc, binary, world, mesh_node):
    node = doc["nodes"][mesh_node]
    matrix = world.get(mesh_node, m_identity())
    mesh = doc["meshes"][node["mesh"]]
    out = []
    for prim in mesh["primitives"]:
        attrs = prim["attributes"]
        if "POSITION" not in attrs:
            raise SystemExit("error: mesh %d has no POSITION" % node["mesh"])
        positions = [m_point(matrix, v) for v in read_accessor(doc, binary, attrs["POSITION"])]
        normals = None
        if "NORMAL" in attrs:
            normals = [m_direction(matrix, v)
                       for v in read_accessor(doc, binary, attrs["NORMAL"])]
        indices = None
        if "indices" in prim:
            indices = [v[0] for v in read_accessor(doc, binary, prim["indices"])]
        out.append(Primitive(positions, normals, indices))
    return out


def vertex_adjacency(prims):
    adjacency = {}
    for prim in prims:
        if not prim.indices:
            continue
        for i in range(0, len(prim.indices) - 2, 3):
            a, b, c = prim.indices[i], prim.indices[i + 1], prim.indices[i + 2]
            adjacency.setdefault(a, set()).update((b, c))
            adjacency.setdefault(b, set()).update((a, c))
            adjacency.setdefault(c, set()).update((a, b))
    return adjacency


def bounds(prims):
    lo = [float("inf")] * 3
    hi = [float("-inf")] * 3
    for prim in prims:
        for p in prim.positions:
            for axis in range(3):
                lo[axis] = min(lo[axis], p[axis])
                hi[axis] = max(hi[axis], p[axis])
    return lo, hi


# --------------------------------------------------------------------------
# Skeleton
# --------------------------------------------------------------------------

def humanoid_skeleton(lo, hi, arms="down"):
    height = hi[1] - lo[1]
    width = hi[0] - lo[0]
    if height <= 0.0:
        raise SystemExit("error: mesh has zero height; cannot place a skeleton")
    cx = (lo[0] + hi[0]) * 0.5
    cz = (lo[2] + hi[2]) * 0.5
    y0 = lo[1]

    def y(f):
        return y0 + f * height

    def lateral(f):
        return min(f * height, 0.5 * width * 0.95)

    if arms == "t":
        shoulder, elbow, wrist, hand = (0.09, 0.82), (0.23, 0.82), (0.35, 0.82), (0.40, 0.82)
    else:
        shoulder, elbow, wrist, hand = (0.09, 0.80), (0.10, 0.66), (0.105, 0.53), (0.11, 0.47)

    bones = []

    def add(name, parent, head, tail):
        bones.append({"name": name, "parent": parent, "head": head, "tail": tail})

    add("root", None, (cx, y(0.50), cz), (cx, y(0.56), cz))
    add("spine", "root", (cx, y(0.56), cz), (cx, y(0.64), cz))
    add("chest", "spine", (cx, y(0.64), cz), (cx, y(0.73), cz))
    add("neck", "chest", (cx, y(0.73), cz), (cx, y(0.82), cz))
    add("head", "neck", (cx, y(0.82), cz), (cx, y(0.96), cz))
    add("head_top", "head", (cx, y(0.96), cz), (cx, y(1.0), cz))

    for side, sign in (("L", 1.0), ("R", -1.0)):
        def P(f, fy, z=0.0):
            return (cx + sign * lateral(f), y(fy), cz + z)
        add("shoulder.%s" % side, "chest", P(0.02, 0.78), P(*shoulder))
        add("upperarm.%s" % side, "shoulder.%s" % side, P(*shoulder), P(*elbow))
        add("lowerarm.%s" % side, "upperarm.%s" % side, P(*elbow), P(*wrist))
        add("hand.%s" % side, "lowerarm.%s" % side, P(*wrist), P(*hand))
        add("upperleg.%s" % side, "root", P(0.045, 0.50), P(0.05, 0.27))
        add("lowerleg.%s" % side, "upperleg.%s" % side, P(0.05, 0.27), P(0.05, 0.05))
        add("foot.%s" % side, "lowerleg.%s" % side, P(0.05, 0.05),
            P(0.05, 0.015, 0.05 * height))

    return bones


def distance_to_segment(p, head, tail):
    ax, ay, az = head
    bx, by, bz = tail
    px, py, pz = p
    dx, dy, dz = bx - ax, by - ay, bz - az
    length_sq = dx * dx + dy * dy + dz * dz
    if length_sq <= 1e-12:
        ox, oy, oz = px - ax, py - ay, pz - az
        return math.sqrt(ox * ox + oy * oy + oz * oz)
    t = ((px - ax) * dx + (py - ay) * dy + (pz - az) * dz) / length_sq
    t = max(0.0, min(1.0, t))
    ox, oy, oz = px - (ax + t * dx), py - (ay + t * dy), pz - (az + t * dz)
    return math.sqrt(ox * ox + oy * oy + oz * oz)


def compute_weights(prims, bones, height, side_floor, smooth, max_influences):
    adjacency = vertex_adjacency(prims)
    per_prim = []
    for prim in prims:
        weights = []
        for p in prim.positions:
            ranked = []
            for bone_index, bone in enumerate(bones):
                d = distance_to_segment(p, bone["head"], bone["tail"])
                if bone["name"].endswith(".L") and p[0] < -side_floor:
                    d *= 8.0
                elif bone["name"].endswith(".R") and p[0] > side_floor:
                    d *= 8.0
                ranked.append((d, bone_index))
            best = min(d for d, _ in ranked)
            cutoff = max(best * 2.5, 0.02 * height)
            scores = {}
            for d, bone_index in ranked:
                if d <= cutoff:
                    scores[bone_index] = 1.0 / (d ** 4 + 1e-9)
            if not scores:
                scores = {min(ranked)[1]: 1.0}
            weights.append(scores)
        per_prim.append(weights)

    # Laplacian smoothing over the mesh graph, then prune to the influence budget.
    for prim_index, prim in enumerate(prims):
        weights = per_prim[prim_index]
        for _ in range(smooth):
            averaged = []
            for index, current in enumerate(weights):
                neighbors = adjacency.get(index)
                if not neighbors:
                    averaged.append(dict(current))
                    continue
                acc = {key: value * 0.5 for key, value in current.items()}
                share = 0.5 / len(neighbors)
                for neighbor in neighbors:
                    if neighbor >= len(weights):
                        continue
                    for key, value in weights[neighbor].items():
                        acc[key] = acc.get(key, 0.0) + value * share
                averaged.append(acc)
            weights = averaged
        pruned = []
        for current in weights:
            top = sorted(current.items(), key=lambda item: (-item[1], item[0]))[:max_influences]
            total = sum(value for _key, value in top)
            if total <= 0.0:
                top, total = [(0, 1.0)], 1.0
            pruned.append([(key, value / total) for key, value in top])
        per_prim[prim_index] = pruned
    return per_prim


# --------------------------------------------------------------------------
# Rig writing
# --------------------------------------------------------------------------

def copy_visuals(source):
    out = {"asset": {"version": "2.0", "generator": "chrononmotion autorig_glb.py"}}
    for key in ("extensionsUsed", "extensionsRequired"):
        if key in source:
            out[key] = source[key]
    for key in ("bufferViews", "accessors", "materials", "textures", "images", "samplers"):
        out[key] = json.loads(json.dumps(source.get(key, [])))
    out["buffers"] = [{}]
    out["meshes"] = []
    return out


def pack_influences(entries, max_influences):
    """entries: list of [(bone_index, weight), ...] -> (joint rows, weight rows)."""
    joints = []
    weights = []
    for entry in entries:
        quad = [(0, 0.0)] * 4
        for slot, (bone_index, value) in enumerate(entry[:max_influences]):
            quad[slot] = (bone_index, value)
        joints.append([item[0] for item in quad])
        weights.append([item[1] for item in quad])
    return joints, weights


def build_skeleton_nodes(out, bones):
    """Create parent-first joint nodes; return name -> node index and rest globals."""
    lookup = {bone["name"]: index for index, bone in enumerate(bones)}
    nodes = out["nodes"]
    indices = {}
    for bone in bones:
        parent = bone["parent"]
        head = bone["head"]
        translation = head if parent is None else tuple(
            head[i] - bones[lookup[parent]]["head"][i] for i in range(3))
        node = {"name": bone["name"], "translation": [float(c) for c in translation]}
        if parent is not None:
            nodes[indices[parent]].setdefault("children", []).append(len(nodes))
        indices[bone["name"]] = len(nodes)
        nodes.append(node)
        local = m_translation(translation)
        bone["rest_global"] = local if parent is None else m_mul(
            bones[lookup[parent]]["rest_global"], local)
    return indices


def cmd_inspect(args):
    doc, binary = load_glb(args.file)
    meshes = doc.get("meshes", [])
    print("file: %s" % args.file)
    print("nodes: %d  meshes: %d  skins: %d  animations: %d  materials: %d  images: %d" % (
        len(doc.get("nodes", [])), len(meshes), len(doc.get("skins", [])),
        len(doc.get("animations", [])), len(doc.get("materials", [])),
        len(doc.get("images", []))))
    vertices = 0
    triangles = 0
    for mesh in meshes:
        for prim in mesh.get("primitives", []):
            attrs = prim.get("attributes", {})
            count = doc["accessors"][attrs["POSITION"]]["count"] if "POSITION" in attrs else 0
            vertices += count
            if "indices" in prim:
                triangles += doc["accessors"][prim["indices"]]["count"] // 3
            else:
                triangles += count // 3
            print("  attributes: %s" % ", ".join(sorted(attrs)))
    print("vertices: %d  triangles: %d" % (vertices, triangles))
    for skin in doc.get("skins", []):
        print("  skin joints: %d  inverse binds: %s" % (
            len(skin.get("joints", [])),
            "yes" if "inverseBindMatrices" in skin else "no"))
    print("animation clips: %s" % (
        ", ".join(a.get("name", "?") for a in doc.get("animations", [])) or "none"))


def cmd_rig(args):
    doc, binary = load_glb(args.input)
    if doc.get("skins"):
        raise SystemExit("error: %s is already skinned; run 'validate' instead" % args.input)
    if doc.get("animations"):
        print("warning: dropping %d existing clip(s); use 'bake' for motion"
              % len(doc["animations"]))
        doc.pop("animations")
    sources = mesh_nodes(doc)
    if not sources:
        raise SystemExit("error: no mesh nodes in %s" % args.input)

    world = world_matrices(doc)
    groups = []
    for node_index in sources:
        groups.append((doc["nodes"][node_index]["mesh"],
                       collect_primitives(doc, binary, world, node_index)))
    all_prims = [prim for _mesh, prims in groups for prim in prims]
    lo, hi = bounds(all_prims)
    height = hi[1] - lo[1]
    print("bounds: %.3f x %.3f x %.3f (picked up %d primitive(s))"
          % (hi[0] - lo[0], height, hi[2] - lo[2], len(all_prims)))

    bones = humanoid_skeleton(lo, hi, args.arms)
    out = copy_visuals(doc)
    out_binary = bytearray(binary)
    out["nodes"] = []
    indices = build_skeleton_nodes(out, bones)

    inverse_binds = [m_inverse_affine(bone["rest_global"]) for bone in bones]
    ibm_accessor = add_accessor(out, out_binary, [list(m) for m in inverse_binds],
                                "MAT4", 5126)
    out["skins"] = [{
        "joints": [indices[bone["name"]] for bone in bones],
        "skeleton": indices["root"],
        "inverseBindMatrices": ibm_accessor,
    }]

    weights = compute_weights(all_prims, bones, height,
                              0.02 * max(hi[0] - lo[0], 1e-6),
                              args.smooth, args.max_influences)

    body_nodes = []
    cursor = 0
    for mesh_index, prims in groups:
        original = doc["meshes"][mesh_index]
        rebuilt = []
        for prim, original_prim in zip(prims, original["primitives"]):
            entry = weights[cursor]
            cursor += 1
            joint_rows, weight_rows = pack_influences(entry, args.max_influences)
            attrs = {
                "POSITION": add_accessor(out, out_binary, prim.positions, "VEC3", 5126,
                                         bounds=True),
                "JOINTS_0": add_accessor(out, out_binary, joint_rows, "VEC4", 5121),
                "WEIGHTS_0": add_accessor(out, out_binary, weight_rows, "VEC4", 5126),
            }
            if prim.normals:
                attrs["NORMAL"] = add_accessor(out, out_binary, prim.normals, "VEC3", 5126)
            if "TEXCOORD_0" in original_prim["attributes"]:
                attrs["TEXCOORD_0"] = original_prim["attributes"]["TEXCOORD_0"]
            built = {"attributes": attrs, "mode": 4}
            if "indices" in original_prim:
                built["indices"] = original_prim["indices"]
            if "material" in original_prim:
                built["material"] = original_prim["material"]
            rebuilt.append(built)
        out["meshes"].append({"name": original.get("name", "body"), "primitives": rebuilt})
        body_nodes.append(len(out["nodes"]))
        out["nodes"].append({"name": "body", "mesh": len(out["meshes"]) - 1, "skin": 0})

    out["scenes"] = [{"nodes": body_nodes[:]}]
    out["scene"] = 0
    for bone in bones:
        if bone["parent"] is None:
            out["scenes"][0]["nodes"].append(indices[bone["name"]])

    for path in args.clothes:
        merge_garment(out, out_binary, path, bones, indices, height, args)

    if args.motion_only:
        strip_visuals(out)

    save_glb(args.output, out, out_binary)
    print("wrote %s" % args.output)
    print("bones: %d  influences/vertex: up to %d" % (len(bones), args.max_influences))
    return 0


def strip_visuals(doc):
    for mesh in doc.get("meshes", []):
        for prim in mesh.get("primitives", []):
            prim.pop("material", None)
    for key in ("materials", "textures", "images", "samplers"):
        doc.pop(key, None)


def merge_garment(out, out_binary, path, bones, indices, height, args):
    src, src_binary = load_glb(path)
    src_world = world_matrices(src)
    candidates = mesh_nodes(src)
    if not candidates:
        raise SystemExit("error: %s has no mesh nodes" % path)

    while len(out_binary) % 4:
        out_binary.append(0)
    base = len(out_binary)
    out_binary.extend(src_binary)

    view_offset = len(out["bufferViews"])
    for view in src.get("bufferViews", []):
        copy = dict(view)
        copy["buffer"] = 0
        copy["byteOffset"] = view.get("byteOffset", 0) + base
        out["bufferViews"].append(copy)

    accessor_offset = len(out["accessors"])
    for accessor in src.get("accessors", []):
        copy = dict(accessor)
        if "bufferView" in copy:
            copy["bufferView"] += view_offset
        out["accessors"].append(copy)

    image_offset = len(out.get("images", []))
    for image in src.get("images", []):
        copy = dict(image)
        if "bufferView" in copy:
            copy["bufferView"] += view_offset
        out.setdefault("images", []).append(copy)
    sampler_offset = len(out.get("samplers", []))
    for sampler in src.get("samplers", []):
        out.setdefault("samplers", []).append(dict(sampler))
    texture_offset = len(out.get("textures", []))
    for texture in src.get("textures", []):
        copy = dict(texture)
        if "source" in copy:
            copy["source"] += image_offset
        if "sampler" in copy:
            copy["sampler"] += sampler_offset
        out.setdefault("textures", []).append(copy)
    material_offset = len(out.get("materials", []))

    def remap_textures(node):
        if isinstance(node, dict):
            for key, value in node.items():
                if isinstance(value, dict) and "index" in value and key.endswith("Texture"):
                    value["index"] += texture_offset
                else:
                    remap_textures(value)
        elif isinstance(node, list):
            for value in node:
                remap_textures(value)

    for material in src.get("materials", []):
        copy = json.loads(json.dumps(material))
        remap_textures(copy)
        out.setdefault("materials", []).append(copy)

    transform = m_translation(args.clothes_offset)
    if args.clothes_scale != 1.0:
        garment_lo, garment_hi = bounds_of(src, src_binary)
        center = tuple((garment_lo[i] + garment_hi[i]) * 0.5 for i in range(3))
        transform = m_mul(m_mul(m_mul(m_translation(center), m_scale(args.clothes_scale)),
                                m_translation(tuple(-c for c in center))),
                          transform)

    rigid_index = None
    if args.clothes_bone:
        rigid_index = next((i for i, bone in enumerate(bones)
                            if bone["name"] == args.clothes_bone), None)
        if rigid_index is None:
            raise SystemExit("error: unknown --clothes-bone %r" % args.clothes_bone)

    for node_index in candidates:
        mesh_index = src["nodes"][node_index]["mesh"]
        placed = m_mul(transform, src_world.get(node_index, m_identity()))
        prims = collect_primitives(src, src_binary, {node_index: placed}, node_index)
        if rigid_index is not None:
            per_prim = [[[(rigid_index, 1.0)] for _ in prim.positions] for prim in prims]
        else:
            per_prim = compute_weights(prims, bones, height, 0.02 * height,
                                       args.smooth, args.max_influences)

        built = []
        original = src["meshes"][mesh_index]
        for prim, entry, original_prim in zip(prims, per_prim, original["primitives"]):
            joint_rows, weight_rows = pack_influences(entry, args.max_influences)
            attrs = {
                "POSITION": add_accessor(out, out_binary, prim.positions, "VEC3", 5126,
                                         bounds=True),
                "JOINTS_0": add_accessor(out, out_binary, joint_rows, "VEC4", 5121),
                "WEIGHTS_0": add_accessor(out, out_binary, weight_rows, "VEC4", 5126),
            }
            if prim.normals:
                attrs["NORMAL"] = add_accessor(out, out_binary, prim.normals, "VEC3", 5126)
            if "TEXCOORD_0" in original_prim["attributes"]:
                attrs["TEXCOORD_0"] = original_prim["attributes"]["TEXCOORD_0"] + accessor_offset
            piece = {"attributes": attrs, "mode": 4}
            if "indices" in original_prim:
                piece["indices"] = original_prim["indices"] + accessor_offset
            if "material" in original_prim:
                piece["material"] = original_prim["material"] + material_offset
            built.append(piece)
        out["meshes"].append({"name": os.path.basename(path), "primitives": built})
        out["nodes"].append({"name": "clothes:%s" % os.path.basename(path),
                             "mesh": len(out["meshes"]) - 1, "skin": 0})
        out["scenes"][0]["nodes"].append(len(out["nodes"]) - 1)
        print("dressed: %s (%d primitive(s))" % (path, len(built)))


def bounds_of(doc, binary):
    world = world_matrices(doc)
    prims = []
    for node_index in mesh_nodes(doc):
        prims.extend(collect_primitives(doc, binary, world, node_index))
    return bounds(prims)


# --------------------------------------------------------------------------
# Validation
# --------------------------------------------------------------------------

def approx_identity(matrix, tolerance=1e-4):
    identity = m_identity()
    return all(abs(matrix[i] - identity[i]) <= tolerance for i in range(16))


def determinant3(m):
    return (m[0] * (m[5] * m[10] - m[6] * m[9])
            - m[4] * (m[1] * m[10] - m[2] * m[9])
            + m[8] * (m[1] * m[6] - m[2] * m[5]))


def cmd_validate(args):
    doc, binary = load_glb(args.file)
    problems = []
    notes = []

    skins = doc.get("skins", [])
    nodes = doc.get("nodes", [])
    if not skins:
        problems.append("no skin: the engine needs joints and inverse bind matrices")

    world = world_matrices(doc)
    for skin_index, skin in enumerate(skins):
        joints = skin.get("joints", [])
        if not joints:
            problems.append("skin %d has no joints" % skin_index)
            continue
        if len(set(joints)) != len(joints):
            problems.append("skin %d repeats a joint" % skin_index)
        for joint in joints:
            if not (0 <= joint < len(nodes)):
                problems.append("skin %d joint %d out of node range" % (skin_index, joint))
        if "inverseBindMatrices" not in skin:
            problems.append("skin %d has no inverseBindMatrices" % skin_index)
            continue
        inverse = [[float(c) for c in row]
                   for row in read_accessor(doc, binary, skin["inverseBindMatrices"])]
        if len(inverse) != len(joints):
            problems.append("skin %d: %d inverse binds for %d joints"
                            % (skin_index, len(inverse), len(joints)))
        for joint, ibm in zip(joints, inverse[:len(joints)]):
            if joint >= len(nodes):
                continue
            if abs(determinant3(ibm)) < 1e-12:
                problems.append("joint %d inverse bind matrix is singular" % joint)
                continue
            if not approx_identity(m_mul(world.get(joint, m_identity()), ibm), 1e-3):
                problems.append("joint %d: rest pose does not match inverse bind matrix"
                                % joint)

    for node_index, node in enumerate(nodes):
        if "skin" not in node:
            continue
        if node["skin"] >= len(skins):
            problems.append("node %d references missing skin %d" % (node_index, node["skin"]))
            continue
        joint_set = set(skins[node["skin"]].get("joints", []))
        stack = [node_index]
        seen = set()
        descended = False
        while stack:
            current = stack.pop()
            if current in joint_set:
                descended = True
                break
            if current in seen:
                continue
            seen.add(current)
            stack.extend(nodes[current].get("children", []))
        if descended:
            problems.append("mesh node %d is a descendant of a joint" % node_index)

    vertex_total = 0
    influence_total = 0
    unweighted = 0
    joint_count = len(skins[0].get("joints", [])) if skins else 0
    for mesh_index, mesh in enumerate(doc.get("meshes", [])):
        for prim_index, prim in enumerate(mesh.get("primitives", [])):
            attrs = prim.get("attributes", {})
            if "POSITION" not in attrs:
                problems.append("mesh %d prim %d has no POSITION" % (mesh_index, prim_index))
                continue
            count = doc["accessors"][attrs["POSITION"]]["count"]
            if "JOINTS_0" not in attrs or "WEIGHTS_0" not in attrs:
                problems.append("mesh %d prim %d has no JOINTS_0/WEIGHTS_0"
                                % (mesh_index, prim_index))
                continue
            if doc["accessors"][attrs["JOINTS_0"]]["count"] != count:
                problems.append("mesh %d prim %d joint count mismatch" % (mesh_index, prim_index))
                continue
            if doc["accessors"][attrs["WEIGHTS_0"]]["count"] != count:
                problems.append("mesh %d prim %d weight count mismatch" % (mesh_index, prim_index))
                continue
            check_geometry(doc, binary, mesh_index, prim_index, problems)
            joints = read_accessor(doc, binary, attrs["JOINTS_0"])
            weight_accessor = doc["accessors"][attrs["WEIGHTS_0"]]
            weights = read_accessor(doc, binary, attrs["WEIGHTS_0"])
            divisor = 255.0 if weight_accessor.get("normalized") and \
                weight_accessor["componentType"] == 5121 else 1.0
            for vertex, (jrow, wrow) in enumerate(zip(joints, weights)):
                if any(joint < 0 or joint >= joint_count for joint in jrow):
                    problems.append("mesh %d vertex %d uses a joint out of %d"
                                    % (mesh_index, vertex, joint_count))
                    break
                total = sum(c / divisor for c in wrow)
                if abs(total - 1.0) > 1e-3:
                    problems.append("mesh %d vertex %d weights sum to %.4f"
                                    % (mesh_index, vertex, total))
                    break
                used = sum(1 for c in wrow if c > 0)
                if used > 4:
                    problems.append("mesh %d vertex %d has %d influences"
                                    % (mesh_index, vertex, used))
                    break
                if used == 0:
                    unweighted += 1
                influence_total += used
            vertex_total += count

    if vertex_total:
        notes.append("average influences/vertex: %.2f" % (influence_total / vertex_total))
    if unweighted:
        problems.append("%d vertices have no bone influence" % unweighted)
    if doc.get("materials") and args.motion_capable:
        notes.append("materials present: the geometry-only motion reader rejects them "
                     "(rig with --motion-only for engine motion import)")
    if doc.get("animations"):
        notes.append("%d animation clip(s) present" % len(doc["animations"]))

    print("validated: %s" % args.file)
    print("skins: %d  joints: %d  vertices: %d" % (
        len(skins), joint_count, vertex_total))
    for note in notes:
        print("  note: %s" % note)
    for problem in problems:
        print("  FAIL: %s" % problem)
    if problems:
        print("%d problem(s)" % len(problems))
        return 1
    print("OK")
    return 0


def check_geometry(doc, binary, mesh_index, prim_index, problems):
    """Reject geometry a renderer would crash on: non-finite positions, bad indices."""
    prim = doc["meshes"][mesh_index]["primitives"][prim_index]
    attrs = prim.get("attributes", {})
    if "POSITION" not in attrs:
        return
    count = doc["accessors"][attrs["POSITION"]]["count"]
    positions = read_accessor(doc, binary, attrs["POSITION"])
    for vertex, p in enumerate(positions):
        if any(c != c or c in (float("inf"), float("-inf")) for c in p):
            problems.append("mesh %d prim %d vertex %d has a non-finite position"
                            % (mesh_index, prim_index, vertex))
            break
    if "indices" not in prim:
        if count % 3:
            problems.append("mesh %d prim %d: %d vertices are not whole triangles"
                            % (mesh_index, prim_index, count))
        return
    indices = [v[0] for v in read_accessor(doc, binary, prim["indices"])]
    if len(indices) % 3:
        problems.append("mesh %d prim %d: index count %d is not a whole number of triangles"
                        % (mesh_index, prim_index, len(indices)))
    if indices and (max(indices) >= count or min(indices) < 0):
        problems.append("mesh %d prim %d: index out of vertex range (max %d of %d)"
                        % (mesh_index, prim_index, max(indices), count))


def cmd_validate_static(args):
    """Check the Chronon3d static mesh loader contract used for baked frames."""
    doc, binary = load_glb(args.file)
    problems = []
    notes = []
    if doc.get("skins"):
        problems.append("skins present: the static mesh loader rejects skinned GLB")
    for node_index, node in enumerate(doc.get("nodes", [])):
        if "skin" in node:
            problems.append("node %d has a skin reference "
                            "(static loader rejects it)" % node_index)
        if "weights" in node:
            problems.append("node %d has morph weights" % node_index)
    if doc.get("animations"):
        notes.append("%d animation clip(s) ignored by the static loader"
                     % len(doc["animations"]))
    vertices = 0
    for mesh_index, mesh in enumerate(doc.get("meshes", [])):
        for prim_index, prim in enumerate(mesh.get("primitives", [])):
            attrs = prim.get("attributes", {})
            if prim.get("mode", 4) != 4:
                problems.append("mesh %d prim %d is not a triangle primitive"
                                % (mesh_index, prim_index))
            if "POSITION" not in attrs:
                problems.append("mesh %d prim %d has no POSITION" % (mesh_index, prim_index))
                continue
            accessor = doc["accessors"][attrs["POSITION"]]
            if accessor.get("min") is None or accessor.get("max") is None:
                problems.append("mesh %d prim %d POSITION lacks min/max"
                                % (mesh_index, prim_index))
            if "JOINTS_0" in attrs or "WEIGHTS_0" in attrs:
                problems.append("mesh %d prim %d keeps skin attributes"
                                % (mesh_index, prim_index))
            check_geometry(doc, binary, mesh_index, prim_index, problems)
            vertices += accessor["count"]
    print("validated (static): %s" % args.file)
    print("vertices: %d  materials: %d" % (vertices, len(doc.get("materials", []))))
    for note in notes:
        print("  note: %s" % note)
    for problem in problems:
        print("  FAIL: %s" % problem)
    if problems:
        print("%d problem(s)" % len(problems))
        return 1
    print("OK")
    return 0


# --------------------------------------------------------------------------
# Bake: procedural clip -> static GLB per frame
# --------------------------------------------------------------------------

AXES = {"x": (1.0, 0.0, 0.0), "y": (0.0, 1.0, 0.0), "z": (0.0, 0.0, 1.0)}
TAU = 2.0 * math.pi

CLIPS = {
    "idle": lambda t: {
        "chest": ("x", math.radians(1.5) * math.sin(TAU * t)),
        "neck": ("x", math.radians(-1.0) * math.sin(TAU * t)),
        "head": ("y", math.radians(4.0) * math.sin(TAU * t)),
        "upperarm.L": ("z", math.radians(2.0) * math.sin(TAU * t)),
        "upperarm.R": ("z", math.radians(-2.0) * math.sin(TAU * t)),
    },
    "walk": lambda t: {
        "upperleg.L": ("x", math.radians(22.0) * math.sin(TAU * t)),
        "upperleg.R": ("x", -math.radians(22.0) * math.sin(TAU * t)),
        "lowerleg.L": ("x", math.radians(18.0) * max(0.0, math.sin(TAU * t))),
        "lowerleg.R": ("x", math.radians(18.0) * max(0.0, -math.sin(TAU * t))),
        "upperarm.L": ("x", -math.radians(16.0) * math.sin(TAU * t)),
        "upperarm.R": ("x", math.radians(16.0) * math.sin(TAU * t)),
        "chest": ("y", math.radians(3.0) * math.sin(TAU * t)),
    },
    "wave": lambda t: {
        "upperarm.R": ("z", -math.radians(105.0)),
        "lowerarm.R": ("z", -math.radians(20.0) + math.radians(25.0) * math.sin(2 * TAU * t)),
        "head": ("y", math.radians(5.0) * math.sin(TAU * t)),
    },
}


def cmd_bake(args):
    doc, binary = load_glb(args.input)
    skins = doc.get("skins", [])
    if not skins:
        raise SystemExit("error: %s is not skinned; run 'rig' first" % args.input)
    skin = skins[0]
    joints = skin["joints"]
    inverse = read_accessor(doc, binary, skin["inverseBindMatrices"])
    nodes = doc["nodes"]
    names = {joint: nodes[joint].get("name", "joint%d" % joint) for joint in joints}
    parents = parents_of(nodes)
    rest_world = world_matrices(doc)

    body_nodes = [i for i, node in enumerate(nodes) if "mesh" in node and "skin" in node]
    if not body_nodes:
        raise SystemExit("error: no skinned mesh node found")

    clip = CLIPS[args.clip]
    os.makedirs(args.output, exist_ok=True)
    frames = max(1, args.frames)

    for frame in range(frames):
        t = frame / float(frames)
        pose = clip(t)
        posed = {}
        for joint in joints:
            parent = parents.get(joint)
            base = rest_world.get(joint, m_identity())
            if parent is not None and parent in posed:
                local_rest = m_mul(m_inverse_affine(rest_world[parent]),
                                   rest_world.get(joint, m_identity()))
                base = m_mul(posed[parent], local_rest)
            rotation = pose.get(names[joint])
            if rotation is not None:
                base = m_mul(base, m_rotation(AXES[rotation[0]], rotation[1]))
            posed[joint] = base
        skin_matrices = {}
        for index, joint in enumerate(joints):
            skin_matrices[joint] = m_mul(posed[joint], [float(c) for c in inverse[index]])

        out = json.loads(json.dumps(doc))
        out.pop("skins", None)
        out.pop("animations", None)
        out_binary = bytearray(binary)
        for node in out["nodes"]:
            node.pop("skin", None)
            node.pop("weights", None)
        for mesh in out.get("meshes", []):
            for prim in mesh.get("primitives", []):
                prim["attributes"].pop("JOINTS_0", None)
                prim["attributes"].pop("WEIGHTS_0", None)

        for node_index in body_nodes:
            source_mesh = doc["meshes"][nodes[node_index]["mesh"]]
            target_mesh = out["meshes"][nodes[node_index]["mesh"]]
            for prim_index, prim in enumerate(source_mesh["primitives"]):
                attrs = prim["attributes"]
                positions = read_accessor(doc, binary, attrs["POSITION"])
                joint_rows = read_accessor(doc, binary, attrs["JOINTS_0"])
                weight_rows = read_accessor(doc, binary, attrs["WEIGHTS_0"])
                new_positions = []
                for p, jrow, wrow in zip(positions, joint_rows, weight_rows):
                    total = sum(wrow)
                    if total <= 0.0:
                        new_positions.append(tuple(float(c) for c in p))
                        continue
                    x = y = z = 0.0
                    for influence, joint in enumerate(jrow):
                        weight = wrow[influence]
                        if weight <= 0.0:
                            continue
                        px, py, pz = m_point(skin_matrices[joints[joint]], p)
                        x += px * weight
                        y += py * weight
                        z += pz * weight
                    new_positions.append((x / total, y / total, z / total))
                new_normals = None
                if "NORMAL" in attrs:
                    new_normals = []
                    for n, jrow, wrow in zip(read_accessor(doc, binary, attrs["NORMAL"]),
                                             joint_rows, weight_rows):
                        nx = ny = nz = 0.0
                        for influence, joint in enumerate(jrow):
                            weight = wrow[influence]
                            if weight <= 0.0:
                                continue
                            rotated = m_direction(skin_matrices[joints[joint]], n)
                            nx += rotated[0] * weight
                            ny += rotated[1] * weight
                            nz += rotated[2] * weight
                        length = math.sqrt(nx * nx + ny * ny + nz * nz) or 1.0
                        new_normals.append((nx / length, ny / length, nz / length))
                target = target_mesh["primitives"][prim_index]
                target["attributes"]["POSITION"] = add_accessor(
                    out, out_binary, new_positions, "VEC3", 5126, bounds=True)
                if new_normals is not None:
                    target["attributes"]["NORMAL"] = add_accessor(
                        out, out_binary, new_normals, "VEC3", 5126)
        save_glb(os.path.join(args.output, "frame_%06d.glb" % frame), out, out_binary)

    print("baked %d frame(s) with clip %r into %s" % (frames, args.clip, args.output))
    return 0


# --------------------------------------------------------------------------
# CLI
# --------------------------------------------------------------------------

def main(argv=None):
    parser = argparse.ArgumentParser(
        prog="autorig_glb.py",
        description="Auto-rig, dress and bake static GLB characters without a DCC.")
    subs = parser.add_subparsers(dest="command", required=True)

    inspect = subs.add_parser("inspect", help="report GLB contents")
    inspect.add_argument("file")
    inspect.set_defaults(func=cmd_inspect)

    rig = subs.add_parser("rig", help="build skeleton, weights and inverse binds")
    rig.add_argument("input")
    rig.add_argument("-o", "--output", required=True)
    rig.add_argument("--arms", choices=("down", "t"), default="down",
                     help="source pose: arms hanging (default) or T-pose")
    rig.add_argument("--smooth", type=int, default=4, help="weight smoothing passes")
    rig.add_argument("--max-influences", type=int, default=4, choices=range(1, 5))
    rig.add_argument("--clothes", action="append", default=[],
                     help="garment GLB, repeatable")
    rig.add_argument("--clothes-bone", default=None,
                     help="bind garments rigidly to this bone instead of auto weights")
    rig.add_argument("--clothes-scale", type=float, default=1.0)
    rig.add_argument("--clothes-offset", type=float, nargs=3, default=(0.0, 0.0, 0.0))
    rig.add_argument("--motion-only", action="store_true",
                     help="strip materials/textures for the geometry-only motion reader")
    rig.set_defaults(func=cmd_rig)

    validate = subs.add_parser("validate", help="check the skin contract")
    validate.add_argument("file")
    validate.add_argument("--motion-capable", action="store_true",
                          help="also report when materials block the motion reader")
    validate.add_argument("--expect-static", action="store_true",
                          help="check the static mesh loader contract instead "
                               "(baked frames)")
    validate.set_defaults(func=cmd_validate)

    bake = subs.add_parser("bake", help="write one static GLB per frame")
    bake.add_argument("input")
    bake.add_argument("-o", "--output", required=True)
    bake.add_argument("--clip", choices=sorted(CLIPS), default="idle")
    bake.add_argument("--frames", type=int, default=48)
    bake.set_defaults(func=cmd_bake)

    args = parser.parse_args(argv)
    if args.command == "validate" and args.expect_static:
        return cmd_validate_static(args)
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
