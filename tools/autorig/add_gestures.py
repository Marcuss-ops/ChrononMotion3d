#!/usr/bin/env python3
"""Author gesture clips onto any humanoid GLB skin.

The in-repo clip packs cover locomotion, combat and posture, but not the
gesture vocabulary a talking-head video needs (pointing, shrugging, facepalm,
listening, ...). This tool writes new glTF `animations` onto an existing rig,
so they play in the same renderer as any imported clip.

Poses are authored as world-space bone aims relative to the rest pose:

  q_local(t) = parent^-1 * delta(t) * parent * q_bind

so a bone points where the gesture says regardless of how the source rig
oriented its bones. Every clip is rest-anchored (starts and ends at rest) so it
can be concatenated with a short blend, and each one is checked numerically:
`--check` prints where the hands and head actually end up at peak.

Usage:
  python3 add_gestures.py rig.glb -o rig_gestures.glb
  python3 add_gestures.py rig.glb -o rig_gestures.glb --check
  python3 add_gestures.py rig.glb --list-roles
"""

import argparse
import json
import math
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import autorig_glb as A  # noqa: E402

# --------------------------------------------------------------------------
# Quaternions (glTF order x, y, z, w)
# --------------------------------------------------------------------------

IDENTITY = (0.0, 0.0, 0.0, 1.0)


def q_mul(a, b):
    ax, ay, az, aw = a
    bx, by, bz, bw = b
    return (
        aw * bx + ax * bw + ay * bz - az * by,
        aw * by - ax * bz + ay * bw + az * bx,
        aw * bz + ax * by - ay * bx + az * bw,
        aw * bw - ax * bx - ay * by - az * bz,
    )


def q_conj(q):
    return (-q[0], -q[1], -q[2], q[3])


def q_normalize(q):
    length = math.sqrt(sum(c * c for c in q)) or 1.0
    return tuple(c / length for c in q)


def q_from_axis_angle(axis, degrees):
    x, y, z = axis
    length = math.sqrt(x * x + y * y + z * z)
    if length < 1e-9 or abs(degrees) < 1e-9:
        return IDENTITY
    half = math.radians(degrees) * 0.5
    sin_half = math.sin(half)
    return (x / length * sin_half, y / length * sin_half, z / length * sin_half, math.cos(half))


def q_to_axis_angle(q):
    q = q_normalize(q)
    if q[3] < 0.0:
        q = (-q[0], -q[1], -q[2], -q[3])
    angle = 2.0 * math.acos(max(-1.0, min(1.0, q[3])))
    sin_half = math.sqrt(max(0.0, 1.0 - q[3] * q[3]))
    if sin_half < 1e-7:
        return (1.0, 0.0, 0.0), 0.0
    return (q[0] / sin_half, q[1] / sin_half, q[2] / sin_half), math.degrees(angle)


def q_between(source, target):
    """Shortest-arc rotation taking unit vector `source` onto `target`."""
    sx, sy, sz = source
    tx, ty, tz = target
    dot = max(-1.0, min(1.0, sx * tx + sy * ty + sz * tz))
    if dot > 0.999999:
        return IDENTITY
    if dot < -0.999999:
        axis = (0.0, 0.0, 1.0) if abs(sx) < 0.9 else (1.0, 0.0, 0.0)
        return q_from_axis_angle(axis, 180.0)
    cx = sy * tz - sz * ty
    cy = sz * tx - sx * tz
    cz = sx * ty - sy * tx
    scale = math.sqrt((1.0 + dot) * 2.0)
    return (cx / scale, cy / scale, cz / scale, scale * 0.5)


def q_rotate(q, v):
    x, y, z, w = q
    vx, vy, vz = v
    # v' = q * v * q^-1
    tx = 2.0 * (y * vz - z * vy)
    ty = 2.0 * (z * vx - x * vz)
    tz = 2.0 * (x * vy - y * vx)
    return (vx + w * tx + (y * tz - z * ty),
            vy + w * ty + (z * tx - x * tz),
            vz + w * tz + (x * ty - y * tx))


def vec_sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def vec_length(v):
    return math.sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2])


def vec_normalize(v):
    length = vec_length(v) or 1.0
    return (v[0] / length, v[1] / length, v[2] / length)


# --------------------------------------------------------------------------
# Rig introspection
# --------------------------------------------------------------------------

ROLE_TOKENS = (
    ("upperchest", "upperchest"),
    ("chest", "chest"),
    ("middlebody", "spine"),
    ("spine", "spine"),
    ("lowerbody", "hips"),
    ("pelvis", "hips"),
    ("hips", "hips"),
    ("neck", "neck"),
    ("head", "head"),
    ("shoulder", "shoulder"),
    ("clavicle", "shoulder"),
    ("upperarm", "upperarm"),
    ("lowerarm", "lowerarm"),
    ("forearm", "lowerarm"),
    ("hand", "hand"),
    ("wrist", "hand"),
    ("upperleg", "thigh"),
    ("thigh", "thigh"),
    ("lowerleg", "shin"),
    ("calf", "shin"),
    ("shin", "shin"),
    ("lowerfoot", "foot"),
    ("foot", "foot"),
    ("ankle", "foot"),
    ("finger", "finger"),
)


def classify(name):
    tokens = [token for token in re.split(r"[._\s]+", name.lower()) if token]
    side = ""
    if "l" in tokens or "left" in tokens:
        side = "L"
    elif "r" in tokens or "right" in tokens:
        side = "R"
    joined = "".join(tokens)
    for token, role in ROLE_TOKENS:
        if token in joined:
            return role, side
    return None, side


class Rig:
    def __init__(self, doc):
        self.doc = doc
        nodes = doc["nodes"]
        self.parents = A.parents_of(nodes)
        self.joints = list(doc["skins"][0]["joints"]) if doc.get("skins") else []
        self.roles = {}
        candidates = {}
        for joint in self.joints:
            role, side = classify(nodes[joint].get("name", ""))
            if role and role != "finger":
                key = "%s.%s" % (role, side) if side else role
                self.roles.setdefault(key, joint)
                candidates.setdefault(key, []).append(joint)
        # A rig with several spine bones (UE4 has spine_01..03) should rotate the
        # highest one, which is what reads as the chest.
        for key, index in candidates.items():
            if key.startswith("spine") and len(index) > 1:
                self.roles[key] = max(index, key=self._depth)
        # Rigs without an explicit chest/clavicle still need a torso bone.
        if "chest" not in self.roles:
            for fallback in ("upperchest", "spine"):
                if fallback in self.roles:
                    self.roles["chest"] = self.roles[fallback]
                    break
        if "shoulder" not in self.roles:
            for side in ("L", "R"):
                fallback = "upperarm.%s" % side
                if fallback in self.roles:
                    self.roles["shoulder.%s" % side] = self.roles[fallback]
        self.rest_world = A.world_matrices(doc)
        self.height = self._measure_height()
        self.world_q = {}
        self.local_q = {}
        self.local_t = {}
        for index, node in enumerate(nodes):
            translation, rotation, _scale = decomposition(node)
            self.local_t[index] = translation
            self.local_q[index] = rotation
        self._accumulate()

    def _depth(self, index):
        depth = 0
        while index in self.parents:
            index = self.parents[index]
            depth += 1
        return depth

    def _measure_height(self):
        heights = [A.m_point(matrix, (0.0, 0.0, 0.0))[1] for matrix in self.rest_world.values()]
        return max(max(heights) - min(heights), 1e-6) if heights else 1.0

    def _accumulate(self):
        def visit(index):
            if index in self.world_q:
                return self.world_q[index]
            parent = self.parents.get(index)
            parent_q = visit(parent) if parent is not None else IDENTITY
            value = q_normalize(q_mul(parent_q, self.local_q[index]))
            self.world_q[index] = value
            return value
        for index in self.rest_world:
            visit(index)

    def bone(self, role, side=""):
        key = "%s.%s" % (role, side) if side else role
        return self.roles.get(key)

    def children(self, index):
        return [child for child, parent in self.parents.items() if parent == index]

    def head(self, index):
        return A.m_point(self.rest_world[index], (0.0, 0.0, 0.0))

    def rest_direction(self, index):
        children = self.children(index)
        if children:
            tail = self.head(children[0])
        else:
            tail = self.head(index)
        direction = vec_sub(tail, self.head(index))
        if vec_length(direction) < 1e-6:
            return (0.0, 1.0, 0.0)
        return vec_normalize(direction)

    def side_sign(self, side):
        """World X sign of that side, measured from the rest pose."""
        hand = self.bone("hand", side)
        hips = self.bone("hips") or self.bone("spine")
        if hand is None or hips is None:
            return 1.0 if side != "R" else -1.0
        return 1.0 if self.head(hand)[0] >= self.head(hips)[0] else -1.0


def decomposition(node):
    if "matrix" in node:
        m = node["matrix"]
        translation = (m[12], m[13], m[14])
        # Rotation columns, normalised to strip scale.
        cx = vec_normalize((m[0], m[1], m[2]))
        cy = vec_normalize((m[4], m[5], m[6]))
        cz = vec_normalize((m[8], m[9], m[10]))
        return translation, matrix_to_quat(cx, cy, cz), 1.0
    return (tuple(node.get("translation", (0.0, 0.0, 0.0))),
            tuple(node.get("rotation", IDENTITY)),
            tuple(node.get("scale", (1.0, 1.0, 1.0))))


def matrix_to_quat(c0, c1, c2):
    m00, m10, m20 = c0
    m01, m11, m21 = c1
    m02, m12, m22 = c2
    trace = m00 + m11 + m22
    if trace > 0.0:
        s = math.sqrt(trace + 1.0) * 2.0
        return ((m21 - m12) / s, (m02 - m20) / s, (m10 - m01) / s, 0.25 * s)
    if m00 > m11 and m00 > m22:
        s = math.sqrt(1.0 + m00 - m11 - m22) * 2.0
        return (0.25 * s, (m01 + m10) / s, (m02 + m20) / s, (m21 - m12) / s)
    if m11 > m22:
        s = math.sqrt(1.0 + m11 - m00 - m22) * 2.0
        return ((m01 + m10) / s, 0.25 * s, (m12 + m21) / s, (m02 - m20) / s)
    s = math.sqrt(1.0 + m22 - m00 - m11) * 2.0
    return ((m02 + m20) / s, (m12 + m21) / s, 0.25 * s, (m10 - m01) / s)


# --------------------------------------------------------------------------
# Gesture vocabulary
# --------------------------------------------------------------------------
# aim:   role.side -> world direction the bone should point at peak
# turn:  role.side -> (axis, degrees) world-space twist at peak
# x components are multiplied by the measured side sign

# Bones arrive after the ones that drive them: a shoulder moves first, the hand
# last, and the head lags the chest. These are the seconds of lag applied to the
# whole pose curve of that role, which is what turns a single held pose into
# overshoot plus follow-through.
DEFAULT_DELAY = {
    "hips": 0.00, "spine": 0.02, "chest": 0.03, "neck": 0.05, "head": 0.08,
    "shoulder": 0.02, "upperarm": 0.03, "lowerarm": 0.06, "hand": 0.09,
    "thigh": 0.00, "shin": 0.04, "foot": 0.08,
}

# Every curve is authored as keys (seconds) + factors, where 1.0 is the peak
# pose. A factor above 1.0 overshoots on the way in, one below 1.0 settles back,
# and a negative factor is a recoil past rest.
GESTURES = {
    "G_Wave": {
        "keys": (0.0, 0.22, 0.32, 0.58, 0.80, 1.15),
        "factors": (0.0, 0.90, 1.10, 0.96, 1.04, 0.0),
        "aim": {"upperarm.R": (0.85, 0.55, 0.12), "lowerarm.R": (0.25, 1.0, 0.25)},
        "turn": {"head": ((0.0, 1.0, 0.0), 6.0), "chest": ((0.0, 1.0, 0.0), 3.0)},
        "check": ("hand_above_head", "R"),
    },
    "G_Victory": {
        "keys": (0.0, 0.24, 0.34, 0.62, 0.90, 1.15),
        "factors": (0.0, 0.95, 1.12, 0.94, 0.99, 0.0),
        "aim": {"upperarm.L": (0.45, 1.0, 0.10), "lowerarm.L": (0.10, 1.0, 0.05),
                "upperarm.R": (0.45, 1.0, 0.10), "lowerarm.R": (0.10, 1.0, 0.05)},
        "turn": {"head": ((1.0, 0.0, 0.0), -6.0), "chest": ((1.0, 0.0, 0.0), -4.0)},
        "check": ("both_hands_above_head", ""),
    },
    "G_Shrug": {
        "keys": (0.0, 0.20, 0.30, 0.60, 0.85, 1.05),
        "factors": (0.0, 0.88, 1.08, 0.96, 1.02, 0.0),
        "aim": {"upperarm.L": (0.95, -0.35, 0.30), "lowerarm.L": (0.98, 0.25, 0.30),
                "upperarm.R": (0.95, -0.35, 0.30), "lowerarm.R": (0.98, 0.25, 0.30)},
        "turn": {"head": ((0.0, 0.0, 1.0), 7.0), "chest": ((1.0, 0.0, 0.0), -3.0)},
        "check": ("hands_out", ""),
    },
    "G_Facepalm": {
        "keys": (0.0, 0.26, 0.36, 0.70, 1.00, 1.20),
        "factors": (0.0, 1.02, 1.10, 0.95, 1.01, 0.0),
        "aim": {"upperarm.R": (0.20, 0.45, 0.75), "lowerarm.R": (-0.45, 0.85, 0.25)},
        "turn": {"head": ((1.0, 0.0, 0.0), 16.0), "chest": ((1.0, 0.0, 0.0), 5.0)},
        "check": ("hand_near_head", "R"),
    },
    "G_Think": {
        "keys": (0.0, 0.28, 0.40, 0.78, 1.08, 1.30),
        "factors": (0.0, 0.98, 1.06, 0.96, 1.00, 0.0),
        "aim": {"upperarm.R": (0.15, 0.10, 0.90), "lowerarm.R": (-0.55, 0.75, 0.35)},
        "turn": {"head": ((0.0, 1.0, 0.0), -8.0), "neck": ((0.0, 1.0, 0.0), -4.0)},
        "check": ("hand_near_head", "R"),
    },
    "G_Point": {
        "keys": (0.0, 0.18, 0.28, 0.60, 0.90, 1.15),
        "factors": (0.0, 0.96, 1.12, 0.97, 1.00, 0.0),
        "aim": {"upperarm.R": (0.95, 0.10, 0.35), "lowerarm.R": (0.98, 0.02, 0.30),
                "hand.R": (0.98, 0.0, 0.30)},
        "turn": {"head": ((0.0, 1.0, 0.0), -10.0), "chest": ((0.0, 1.0, 0.0), -5.0)},
        "check": ("hand_far_from_hips", "R"),
    },
    "G_Explain": {
        "keys": (0.0, 0.22, 0.32, 0.62, 0.90, 1.10),
        "factors": (0.0, 0.94, 1.08, 0.94, 1.00, 0.0),
        "aim": {"upperarm.L": (0.40, -0.10, 0.90), "lowerarm.L": (0.55, 0.35, 0.75),
                "upperarm.R": (0.40, -0.10, 0.90), "lowerarm.R": (0.55, 0.35, 0.75)},
        "turn": {"chest": ((1.0, 0.0, 0.0), -4.0)},
        "check": ("hands_forward", ""),
    },
    "G_CrossArms": {
        "keys": (0.0, 0.24, 0.36, 0.70, 1.00, 1.20),
        "factors": (0.0, 0.92, 1.07, 0.97, 1.00, 0.0),
        "aim": {"upperarm.L": (0.25, -0.75, 0.55), "lowerarm.L": (-0.85, 0.10, 0.45),
                "upperarm.R": (0.25, -0.75, 0.55), "lowerarm.R": (-0.85, 0.10, 0.45)},
        "turn": {"chest": ((0.0, 1.0, 0.0), 6.0)},
        "check": ("hands_forward", ""),
    },
    "G_Listen": {
        "keys": (0.0, 0.28, 0.40, 0.80, 1.05, 1.30),
        "factors": (0.0, 0.96, 1.07, 0.96, 1.00, 0.0),
        "aim": {"upperarm.R": (0.55, 0.05, 0.35), "lowerarm.R": (-0.50, 0.80, 0.30)},
        "turn": {"head": ((0.0, 1.0, 0.0), -14.0), "chest": ((1.0, 0.0, 0.0), -6.0)},
        "check": ("hand_near_head", "R"),
    },
    "G_Shh": {
        "keys": (0.0, 0.26, 0.38, 0.75, 1.05, 1.25),
        "factors": (0.0, 0.98, 1.08, 0.96, 1.00, 0.0),
        "aim": {"upperarm.R": (0.18, 0.12, 0.85), "lowerarm.R": (-0.30, 0.85, 0.40)},
        "turn": {"head": ((1.0, 0.0, 0.0), 8.0), "chest": ((1.0, 0.0, 0.0), 4.0)},
        "check": ("hand_near_head", "R"),
    },
    "G_Nod": {
        "keys": (0.0, 0.20, 0.34, 0.52, 0.72, 0.95),
        "factors": (0.0, 1.0, -0.28, 0.92, -0.12, 0.0),
        "aim": {},
        "turn": {"head": ((1.0, 0.0, 0.0), 16.0), "neck": ((1.0, 0.0, 0.0), 8.0)},
        "check": ("head_rotates", ""),
    },
    "G_ShakeHead": {
        "keys": (0.0, 0.18, 0.32, 0.50, 0.70, 0.95),
        "factors": (0.0, 1.0, -0.82, 0.90, -0.30, 0.0),
        "aim": {},
        "turn": {"head": ((0.0, 1.0, 0.0), 20.0), "neck": ((0.0, 1.0, 0.0), 8.0)},
        "check": ("head_rotates", ""),
    },
    "G_Typing": {
        "keys": (0.0, 0.18, 0.30, 0.42, 0.54, 0.66, 0.78, 0.90, 1.02, 1.30),
        "factors": (0.0, 0.96, 1.06, 0.95, 1.05, 0.96, 1.05, 0.95, 1.0, 0.0),
        "aim": {"upperarm.L": (0.30, -0.30, 0.90), "lowerarm.L": (0.20, 0.22, 0.95),
                "upperarm.R": (0.30, -0.30, 0.90), "lowerarm.R": (0.20, 0.22, 0.95),
                "hand.L": (0.10, -0.10, 0.99), "hand.R": (0.10, -0.10, 0.99)},
        "turn": {"head": ((1.0, 0.0, 0.0), 10.0), "chest": ((1.0, 0.0, 0.0), -6.0)},
        "check": ("hands_forward", ""),
    },
    "G_Scroll": {
        "keys": (0.0, 0.18, 0.30, 0.44, 0.58, 0.72, 0.86, 1.15),
        "factors": (0.0, 1.06, 0.94, 1.04, 0.92, 1.02, 0.96, 0.0),
        "aim": {"upperarm.R": (0.42, -0.15, 0.90), "lowerarm.R": (0.28, 0.30, 0.92),
                "hand.R": (0.15, 0.30, 0.94)},
        "turn": {"head": ((1.0, 0.0, 0.0), 12.0)},
        "check": ("hand_forward", "R"),
    },
    "G_Paper": {
        "keys": (0.0, 0.26, 0.38, 0.85, 1.25, 1.60),
        "factors": (0.0, 0.94, 1.06, 0.98, 1.00, 0.0),
        "aim": {"upperarm.L": (0.28, -0.25, 0.92), "lowerarm.L": (0.22, 0.12, 0.97),
                "upperarm.R": (0.28, -0.25, 0.92), "lowerarm.R": (0.22, 0.12, 0.97)},
        "turn": {"head": ((1.0, 0.0, 0.0), 20.0), "neck": ((1.0, 0.0, 0.0), 8.0)},
        "check": ("hands_forward", ""),
    },
    "G_Count": {
        "keys": (0.0, 0.22, 0.34, 0.52, 0.70, 0.88, 1.05, 1.55),
        "factors": (0.0, 0.95, 1.08, 0.98, 1.04, 0.97, 1.02, 0.0),
        "aim": {"upperarm.R": (0.26, 0.18, 0.88), "lowerarm.R": (-0.42, 0.86, 0.30),
                "hand.R": (-0.45, 0.82, 0.35)},
        "turn": {"head": ((1.0, 0.0, 0.0), 10.0)},
        "check": ("hand_near_head", "R"),
    },
    "G_Watch": {
        "keys": (0.0, 0.24, 0.36, 0.70, 0.95, 1.20),
        "factors": (0.0, 0.95, 1.06, 0.98, 1.02, 0.0),
        "aim": {"upperarm.R": (0.40, -0.30, 0.80), "lowerarm.R": (-0.60, 0.25, 0.70),
                "hand.R": (-0.70, 0.20, 0.65)},
        "turn": {"head": ((1.0, 0.0, 0.0), 18.0), "neck": ((1.0, 0.0, 0.0), 6.0)},
        "check": ("hands_forward_or_across", "R"),
    },
    "G_Knock": {
        "keys": (0.0, 0.16, 0.26, 0.36, 0.46, 0.56, 0.66, 0.95),
        "factors": (0.0, 0.9, 1.15, 0.85, 1.12, 0.88, 1.0, 0.0),
        "aim": {"upperarm.R": (0.22, 0.02, 0.97), "lowerarm.R": (0.12, 0.05, 1.0),
                "hand.R": (0.05, 0.0, 1.0)},
        "turn": {"chest": ((1.0, 0.0, 0.0), -5.0)},
        "check": ("hand_forward", "R"),
    },
    "G_ThumbsUp": {
        "keys": (0.0, 0.22, 0.34, 0.65, 0.95, 1.20),
        "factors": (0.0, 0.92, 1.10, 0.98, 1.02, 0.0),
        "aim": {"upperarm.R": (0.25, 0.10, 0.85), "lowerarm.R": (-0.10, 0.95, 0.30),
                "hand.R": (-0.05, 1.0, 0.15)},
        "turn": {"head": ((1.0, 0.0, 0.0), -4.0)},
        "check": ("hand_high_forward", "R"),
    },
    "G_Offer": {
        "keys": (0.0, 0.26, 0.40, 0.80, 1.10, 1.35),
        "factors": (0.0, 0.90, 1.06, 0.97, 1.00, 0.0),
        "aim": {"upperarm.L": (0.35, -0.40, 0.85), "lowerarm.L": (0.25, 0.10, 0.95),
                "upperarm.R": (0.35, -0.40, 0.85), "lowerarm.R": (0.25, 0.10, 0.95)},
        "turn": {"head": ((1.0, 0.0, 0.0), 8.0), "chest": ((1.0, 0.0, 0.0), -6.0)},
        "check": ("hands_forward", ""),
    },
    "G_Refuse": {
        "keys": (0.0, 0.18, 0.28, 0.50, 0.70, 0.95),
        "factors": (0.0, 0.92, 1.12, 0.94, 1.0, 0.0),
        "aim": {"upperarm.L": (0.45, -0.20, 0.85), "lowerarm.L": (0.35, 0.35, 0.85),
                "upperarm.R": (0.45, -0.20, 0.85), "lowerarm.R": (0.35, 0.35, 0.85),
                "hand.L": (0.20, 0.45, 0.85), "hand.R": (0.20, 0.45, 0.85)},
        "turn": {"head": ((0.0, 1.0, 0.0), 10.0), "chest": ((1.0, 0.0, 0.0), 4.0)},
        "check": ("hands_forward", ""),
    },
    "G_Sneak": {
        "keys": (0.0, 0.30, 0.45, 0.80, 1.10, 1.40),
        "factors": (0.0, 0.94, 1.06, 0.98, 1.0, 0.0),
        "aim": {"thigh.L": (0.10, -0.55, 0.83), "shin.L": (-0.10, -0.95, -0.30),
                "thigh.R": (-0.10, -0.55, 0.83), "shin.R": (0.10, -0.95, -0.30),
                "upperarm.L": (0.20, -0.85, 0.45), "lowerarm.L": (-0.30, -0.90, 0.30),
                "upperarm.R": (0.20, -0.85, 0.45), "lowerarm.R": (-0.30, -0.90, 0.30)},
        "shift": {"hips": (0.0, -0.18, 0.02)},
        "turn": {"chest": ((1.0, 0.0, 0.0), -14.0), "head": ((1.0, 0.0, 0.0), 10.0)},
        "check": ("crouches", ""),
    },
}


class RigPose:
    """Evaluates a gesture's peak pose and sampled tracks on a Rig."""

    def __init__(self, rig, spec):
        self.rig = rig
        self.spec = spec
        self.deltas = {}
        self.roles = {}
        self.shifts = {}
        for key, offset in spec.get("shift", {}).items():
            role, _, side = key.partition(".")
            index = rig.bone(role, side)
            if index is None:
                raise KeyError("rig has no bone for %s" % key)
            # Offsets are authored as fractions of the rig height so a pose
            # means the same thing on differently sized characters.
            self.shifts[index] = (offset[0] * rig.height, offset[1] * rig.height,
                                  offset[2] * rig.height)
        for key, direction in spec.get("aim", {}).items():
            role, _, side = key.partition(".")
            index = rig.bone(role, side)
            if index is None:
                raise KeyError("rig has no bone for %s" % key)
            sign = rig.side_sign(side) if side else 1.0
            target = vec_normalize((direction[0] * sign, direction[1], direction[2]))
            rest = rig.rest_direction(index)
            delta = q_between(rest, target)
            self.deltas[index] = q_to_axis_angle(delta)
            self.roles[index] = role
        for key, (axis, degrees) in spec.get("turn", {}).items():
            role, _, side = key.partition(".")
            index = rig.bone(role, side)
            if index is None:
                raise KeyError("rig has no bone for %s" % key)
            self.deltas[index] = (vec_normalize(axis), degrees)
            self.roles[index] = role

    def delay_for(self, index):
        role = self.roles.get(index, "")
        overrides = self.spec.get("delay", {})
        for key in (role, "%s.%s" % (role, ""), role):
            if key in overrides:
                return float(overrides[key])
        return float(DEFAULT_DELAY.get(role, 0.0))

    def _evaluate(self, factor):
        """Posed world matrices and local quats at `factor`.

        A posed bone's world orientation is exactly `delta * rest`, so an aim
        points where the gesture says instead of stacking on its parent's
        rotation. Unposed bones keep following their parent.
        """
        cache = getattr(self, "_cache", {}).setdefault(factor, {})
        if cache:
            return cache
        nodes = self.rig.doc["nodes"]

        def visit(index):
            if index in cache:
                return cache[index]
            parent = self.rig.parents.get(index)
            parent_entry = visit(parent) if parent is not None else None
            parent_world = parent_entry[0] if parent_entry else A.m_identity()
            parent_q = parent_entry[1] if parent_entry else IDENTITY
            if index in self.deltas or index in self.shifts:
                axis, degrees = self.deltas.get(index, ((1.0, 0.0, 0.0), 0.0))
                delta = q_from_axis_angle(axis, degrees * factor)
                world_q = q_normalize(q_mul(delta, self.rig.world_q[index]))
                local_q = q_normalize(q_mul(q_conj(parent_q), world_q))
                local_t = self.rig.local_t[index]
                if index in self.shifts:
                    offset = q_rotate(q_conj(parent_q),
                                      tuple(c * factor for c in self.shifts[index]))
                    local_t = tuple(local_t[i] + offset[i] for i in range(3))
                local = A.m_from_trs(local_t, local_q,
                                     nodes[index].get("scale", (1.0, 1.0, 1.0)))
            else:
                local_q = self.rig.local_q[index]
                world_q = q_normalize(q_mul(parent_q, local_q))
                local = A.node_local(nodes[index])
                local_t = self.rig.local_t[index]
            cache[index] = (A.m_mul(parent_world, local), world_q, local_q, local_t)
            return cache[index]

        for index in self.rig.rest_world:
            visit(index)
        return cache

    def local_quat(self, index, factor):
        return self._evaluate(factor)[index][2]

    def local_translation(self, index, factor):
        return self._evaluate(factor)[index][3]

    def world_positions(self, factor):
        return {index: A.m_point(entry[0], (0.0, 0.0, 0.0))
                for index, entry in self._evaluate(factor).items()}

    def world_quat(self, index, factor):
        return self._evaluate(factor)[index][1]

    def factor_at(self, time):
        keys = self.spec["keys"]
        factors = self.spec["factors"]
        if time <= keys[0]:
            return factors[0]
        for i in range(1, len(keys)):
            if time <= keys[i]:
                span = keys[i] - keys[i - 1]
                mix = 0.0 if span <= 0.0 else (time - keys[i - 1]) / span
                return factors[i - 1] + (factors[i] - factors[i - 1]) * mix
        return factors[-1]

    def tracks(self):
        """Dense samples per channel, each bone delayed by its own lag.

        Sampling only the authored keys would bake every bone to the same
        timing; evaluating a translated copy of the factor curve per bone is
        what produces overshoot on the arm and follow-through on the hand.
        """
        duration = float(self.spec["keys"][-1])
        samples = max(12, int(round(duration * 24.0)) + 1)
        times = [duration * i / float(samples - 1) for i in range(samples)]
        channels = []
        for index in sorted(self.deltas):
            delay = min(max(self.delay_for(index), 0.0), duration * 0.35)
            quats = []
            for time in times:
                if delay > 1e-6:
                    # Compress the shifted curve so the bone is still at rest
                    # on the last frame of the clip.
                    shifted = max(0.0, (time - delay) * duration / (duration - delay))
                else:
                    shifted = time
                factor = round(self.factor_at(shifted), 3)
                quats.append(self.local_quat(index, factor))
            channels.append((index, "rotation", list(times), [list(q) for q in quats]))
        for index in sorted(self.shifts):
            if index in self.deltas:
                continue
            offsets = [list(self.local_translation(index, round(self.factor_at(time), 3)))
                       for time in times]
            channels.append((index, "translation", list(times), offsets))
        return channels


def matrix_from_quat(q):
    x, y, z, w = q_normalize(q)
    return [
        1 - 2 * (y * y + z * z), 2 * (x * y + w * z), 2 * (x * z - w * y), 0.0,
        2 * (x * y - w * z), 1 - 2 * (x * x + z * z), 2 * (y * z + w * x), 0.0,
        2 * (x * z + w * y), 2 * (y * z - w * x), 1 - 2 * (x * x + y * y), 0.0,
        0.0, 0.0, 0.0, 1.0,
    ]


# --------------------------------------------------------------------------
# Checks: verify the peak pose does what the clip name claims
# --------------------------------------------------------------------------

def run_check(name, rig, pose, height):
    kind, side = GESTURES[name]["check"]
    peak = pose.world_positions(1.0)
    rest = pose.world_positions(0.0)
    head = rig.bone("head")
    hips = rig.bone("hips") or rig.bone("spine")
    hand = rig.bone("hand", side) if side else None
    if kind == "hand_near_head":
        # Compare against the middle of the skull, not the head joint: on some
        # rigs the head bone sits at the neck and the joint origin is a whole
        # skull away from where a hand would rest.
        skull = vec_length(vec_sub(peak[head], rest[head]))
        head_mid = peak[head]
        children = rig.children(head)
        if children:
            tail = peak[children[0]]
            head_mid = tuple((peak[head][i] + tail[i]) * 0.5 for i in range(3))
        distance = vec_length(vec_sub(peak[hand], head_mid))
        rest_mid = rest[head]
        if children:
            rest_mid = tuple((rest[head][i] + rest[children[0]][i]) * 0.5 for i in range(3))
        rest_distance = vec_length(vec_sub(rest[hand], rest_mid))
        # Relative to the rig's own reach: long-armed rigs sit further from the
        # skull in absolute units even when the pose is identical.
        limit = 0.55 * rest_distance
        cap = 0.25 * height
        ok = distance < limit and distance < cap
        return ok, "hand-skull %.2f (%.0f%% of rest %.2f, cap %.2f)" % (
            distance, 100.0 * distance / max(rest_distance, 1e-6), rest_distance, cap)
    if kind == "hand_above_head":
        shoulder = rig.bone("shoulder", side) or rig.bone("upperarm", side)
        floor = max(peak[shoulder][1] if shoulder is not None else -1e9,
                    peak[head][1] - 0.12 * height)
        ok = peak[hand][1] > floor
        return ok, "hand y %.2f vs floor %.2f" % (peak[hand][1], floor)
    if kind == "both_hands_above_head":
        heights = [peak[rig.bone("hand", s)][1] for s in ("L", "R")]
        ok = min(heights) > peak[head][1] - 0.02 * height
        return ok, "hand y %.2f/%.2f vs head y %.2f" % (
            heights[0], heights[1], peak[head][1])
    if kind == "hand_far_from_hips":
        lateral = abs(peak[hand][0] - peak[hips][0])
        ok = lateral > 0.30 * height and peak[hand][1] > peak[hips][1] + 0.30 * height
        return ok, "lateral %.2f (min %.2f), hand y %.2f" % (
            lateral, 0.30 * height, peak[hand][1])
    if kind == "hands_out":
        # A shrug opens the palms: both hands end up above their elbows.
        raised = 0
        for side_letter in ("L", "R"):
            hand_index = rig.bone("hand", side_letter)
            elbow_index = rig.bone("lowerarm", side_letter)
            if hand_index is None or elbow_index is None:
                continue
            lift = peak[hand_index][1] - peak[elbow_index][1]
            if lift > 0.03 * height:
                raised += 1
        return raised == 2, "%d hand(s) above the elbow" % raised
    if kind == "hands_forward":
        forward = sum(1 for s in ("L", "R")
                      if peak[rig.bone("hand", s)][2] - rest[rig.bone("hand", s)][2] > 0.05 * height)
        return forward == 2, "%d hand(s) moved forward" % forward
    if kind == "head_rotates":
        turned = q_mul(q_conj(pose.world_quat(head, 0.0)), pose.world_quat(head, 1.0))
        _axis, degrees = q_to_axis_angle(turned)
        return degrees > 8.0, "head turned %.1f degrees" % degrees
    if kind == "hand_forward":
        reach = peak[hand][2] - rest[hand][2]
        return reach > 0.12 * height, "hand forward %.2f (min %.2f)" % (
            reach, 0.12 * height)
    if kind == "hand_high_forward":
        reach = peak[hand][2] - rest[hand][2]
        lift = peak[hand][1] - rest[hand][1]
        ok = reach > 0.10 * height and lift > 0.15 * height
        return ok, "hand forward %.2f lift %.2f" % (reach, lift)
    if kind == "hands_forward_or_across":
        reach = peak[hand][2] - rest[hand][2]
        crossed = abs(peak[hand][0] - peak[head][0]) < 0.45 * height
        ok = reach > 0.05 * height and crossed
        return ok, "hand forward %.2f, head-hand lateral %.2f" % (
            reach, abs(peak[hand][0] - peak[head][0]))
    if kind == "crouches":
        drop = rest[head][1] - peak[head][1]
        return drop > 0.04 * height, "head dropped %.2f (min %.2f)" % (
            drop, 0.04 * height)
    return True, "no check"


# --------------------------------------------------------------------------
# glTF writing
# --------------------------------------------------------------------------

def add_clip(doc, binary, name, tracks):
    samplers = []
    channels = []
    for index, path, times, values in tracks:
        times_accessor = A.add_accessor(doc, binary, times, "SCALAR", 5126)
        doc["accessors"][times_accessor]["min"] = [min(times)]
        doc["accessors"][times_accessor]["max"] = [max(times)]
        kind = "VEC4" if path == "rotation" else "VEC3"
        values_accessor = A.add_accessor(doc, binary, values, kind, 5126)
        samplers.append({"input": times_accessor, "output": values_accessor,
                         "interpolation": "LINEAR"})
        channels.append({"sampler": len(samplers) - 1,
                         "target": {"node": index, "path": path}})
    doc.setdefault("animations", []).append(
        {"name": name, "channels": channels, "samplers": samplers})


def main(argv=None):
    parser = argparse.ArgumentParser(description="Author gesture clips onto a rigged GLB.")
    parser.add_argument("glb")
    parser.add_argument("-o", "--output", default="")
    parser.add_argument("--clips", default="", help="comma separated gesture names (default: all)")
    parser.add_argument("--check", action="store_true", help="print peak-pose measurements")
    parser.add_argument("--list-roles", action="store_true", help="print the bone mapping")
    args = parser.parse_args(argv)

    doc, binary = A.load_glb(args.glb)
    if not doc.get("skins"):
        raise SystemExit("error: %s is not skinned" % args.glb)
    rig = Rig(doc)
    if args.list_roles:
        for key in sorted(rig.roles):
            print("%-12s node %3d  %s" % (key, rig.roles[key],
                                          doc["nodes"][rig.roles[key]].get("name")))
        return 0

    # Bone space and mesh space can disagree (Sketchfab exports carry a root
    # scale that the skinned bounds ignore); all checks live in bone space.
    rest = RigPose(rig, {"keys": (0.0,), "factors": (0.0,)}).world_positions(0.0)
    top = max(rest[index][1] for index in rest)
    bottom = min(rest[index][1] for index in rest)
    height = max(top - bottom, 1e-6)
    print("rig: %d joints, bone height %.3f, roles %s" % (
        len(rig.joints), height, ", ".join(sorted(rig.roles))))

    wanted = [name.strip() for name in args.clips.split(",") if name.strip()]
    names = wanted or list(GESTURES)
    failures = 0
    for name in names:
        if name not in GESTURES:
            raise SystemExit("error: unknown gesture %r" % name)
        try:
            pose = RigPose(rig, GESTURES[name])
        except KeyError as error:
            print("SKIP %-14s %s" % (name, error))
            failures += 1
            continue
        ok, message = run_check(name, rig, pose, height)
        if args.check:
            print("%s %-14s %s" % ("ok  " if ok else "FAIL", name, message))
        if not ok:
            failures += 1
            continue
        add_clip(doc, binary, name, pose.tracks())
        print("added %-14s %.2fs  %d channel(s)" % (
            name, GESTURES[name]["keys"][-1], len(pose.deltas)))

    if args.output:
        A.save_glb(args.output, doc, binary)
        print("wrote %s (%d clip(s) total)" % (args.output, len(doc.get("animations", []))))
    print("%d gesture(s), %d rejected" % (len(names) - failures, failures))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
