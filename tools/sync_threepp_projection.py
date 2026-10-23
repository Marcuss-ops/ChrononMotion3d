#!/usr/bin/env python3
"""sync_threepp_projection.py — single-source projection for the threepp fork.

`src/chrononmotion/**` and `include/chrononmotion/**` are the CANONICAL sources.
The GL renderer tree (`render/src/threepp/**`, `render/include/threepp/**`)
historically carried hand-copied duplicates of the same modules under the
`threepp` namespace.  Governance: one fact, one canonical owner; projections
are allowed, second sources of truth are not.

For every file listed in PROJECTION (see below) the render-side copy is a pure
projection of the canonical file under the namespace rename

    chrononmotion -> threepp
    CHRONONMOTION -> THREEPP

This script rewrites the projections (default) or verifies them (--check),
exiting non-zero on drift — same contract as scripts/sync_motion_catalog.sh.

Files that are NOT projections are deliberately excluded from PROJECTION:
they are genuinely forked modules (math/Ray, math/Frustum, materials/Material,
objects/Mesh, scenes/Scene, ...) whose render-side implementations diverge on
purpose and must be merged by hand under build/test verification.
"""

import argparse
import os
import sys

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

PAIRS = [
    # (canonical root, projection root)
    ("src/chrononmotion", "render/src/threepp"),
    ("include/chrononmotion", "render/include/threepp"),
]

# Relative paths whose render-side copy is a projection of the canonical file.
PROJECTION = [
    "animation/AnimationUtils.hpp",
    "animation/KeyframeTrack.hpp",
    "animation/tracks/ColorKeyframeTrack.hpp",
    "animation/tracks/NumberKeyframeTrack.hpp",
    "animation/tracks/QuaternionKeyframeTrack.hpp",
    "animation/tracks/VectorKeyframeTrack.hpp",
    "cameras/Camera.cpp",
    "cameras/Camera.hpp",
    "cameras/OrthographicCamera.cpp",
    "cameras/OrthographicCamera.hpp",
    "cameras/PerspectiveCamera.cpp",
    "cameras/PerspectiveCamera.hpp",
    "core/Assert.hpp",
    "core/EventDispatcher.hpp",
    "extras/core/Interpolations.hpp",
    "extras/core/Path.hpp",
    "extras/curves/CatmullRomCurve3.hpp",
    "extras/curves/CubicBezierCurve.hpp",
    "extras/curves/CubicBezierCurve3.hpp",
    "extras/curves/EllipseCurve.hpp",
    "extras/curves/LineCurve.hpp",
    "extras/curves/QuadraticBezierCurve.hpp",
    "extras/curves/QuadraticBezierCurve3.hpp",
    "extras/curves/SplineCurve.hpp",
    "math/Box2.cpp",
    "math/Box2.hpp",
    "math/Box3.hpp",
    "math/Color.hpp",
    "math/Euler.cpp",
    "math/Euler.hpp",
    "math/Interpolant.cpp",
    "math/Line3.cpp",
    "math/Line3.hpp",
    "math/MathUtils.hpp",
    "math/Matrix3.cpp",
    "math/Matrix3.hpp",
    "math/Matrix4.hpp",
    "math/Plane.cpp",
    "math/Plane.hpp",
    "math/Quaternion.cpp",
    "math/Quaternion.hpp",
    "math/Sphere.cpp",
    "math/Sphere.hpp",
    "math/Spherical.cpp",
    "math/Spherical.hpp",
    "math/Triangle.cpp",
    "math/Triangle.hpp",
    "math/Vector2.cpp",
    "math/Vector2.hpp",
    "math/Vector3.cpp",
    "math/Vector3.hpp",
    "math/Vector4.cpp",
    "math/Vector4.hpp",
    "math/float_view.hpp",
    "math/infinity.hpp",
    "math/interpolants/CubicInterpolant.hpp",
    "math/interpolants/DiscreteInterpolant.hpp",
    "math/interpolants/LinearInterpolant.hpp",
    "math/interpolants/QuaternionLinearInterpolant.hpp",
    "objects/Group.cpp",
    "objects/Group.hpp",
]


def project(text: str) -> str:
    return text.replace("chrononmotion", "threepp").replace("CHRONONMOTION", "THREEPP")


def pair_paths(rel):
    out = []
    for canonical_root, projection_root in PAIRS:
        c = os.path.join(REPO_ROOT, canonical_root, rel)
        p = os.path.join(REPO_ROOT, projection_root, rel)
        if os.path.isfile(c) and os.path.isfile(p):
            out.append((c, p))
    return out


def discover():
    """Every relative path present as a (canonical, projection) file pair."""
    rels = set()
    for canonical_root, projection_root in PAIRS:
        base = os.path.join(REPO_ROOT, canonical_root)
        for root, _dirs, files in os.walk(base):
            for fn in files:
                c = os.path.join(root, fn)
                rel = os.path.relpath(c, base)
                if os.path.isfile(os.path.join(REPO_ROOT, projection_root, rel)):
                    rels.add(rel.replace(os.sep, "/"))
    return sorted(rels)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true",
                    help="verify projections match; non-zero exit on drift")
    ap.add_argument("--refresh-registry", action="store_true",
                    help="recompute PROJECTION from currently exact pairs")
    args = ap.parse_args()

    if args.refresh_registry:
        registry = []
        for rel in discover():
            if all(project(open(c, errors="ignore").read()) ==
                   open(p, errors="ignore").read()
                   for c, p in pair_paths(rel)):
                registry.append(rel)
        print(f"# {len(registry)} projection pairs")
        for rel in registry:
            print(f'    "{rel}",')
        return 0

    drift = []
    fixed = []
    missing = []
    for rel in PROJECTION:
        pairs = pair_paths(rel)
        if not pairs:
            missing.append(rel)
            continue
        for c, p in pairs:
            want = project(open(c, errors="ignore").read())
            have = open(p, errors="ignore").read()
            if want != have:
                if args.check:
                    drift.append(p)
                else:
                    with open(p, "w") as f:
                        f.write(want)
                    fixed.append(p)

    if missing:
        print("MISSING pair for:", *missing, sep="\n  ")
        return 2
    if args.check:
        if drift:
            print("PROJECTION DRIFT (run tools/sync_threepp_projection.py):",
                  *drift, sep="\n  ")
            return 1
        print(f"threepp projection OK: {len(PROJECTION)} pairs in sync")
    else:
        print(f"threepp projection written: {len(fixed)} file(s) updated")
    return 0


if __name__ == "__main__":
    sys.exit(main())
