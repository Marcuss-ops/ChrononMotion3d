# geo_camera_v1 — the map dive as a camera language

Status: **accepted**, certified by `chrononmotion_geospatial` (285 assertions) and the
Python pixel-level canary (`ChrononTemplate/tools/render_geo_camera_canary.py --self-test`).

## What this family is

The dynamic tile pyramid dive (`DynamicPyramidMap.hpp`) certified one primitive: a
top-down multi-LOD descent from orbit to a landmark. `GeoCameraRig.hpp` turns that
primitive into a family of ten shots, the way a lens family is one optical formula at
different focal lengths:

| preset | shape |
|---|---|
| `MapDiveToLandmark` | pure dive, ends framed on the landmark (nadir) |
| `MapDiveToCity` | dive landing at city scale (z≈14.5) |
| `MapDiveToCountry` | dive landing at country scale (z≈9.5) |
| `MapDiveThenTilt` | dive, then pitch 0°→35° with the anchor locked |
| `MapDiveThenOrbit` | dive → tilt → 15° yaw sweep (the cinematic shot) |
| `MapDivePinReveal` | dive, then a drop pin fires at the anchor |
| `MapDiveLabelReveal` | dive, then a label card fires at the anchor |
| `MapDiveMetricReveal` | dive, then a metric figure rises over the place |
| `MapDiveRouteContinue` | dive, then hand off to a route walking onward |
| `MapDiveThenCraneOut` | dive, then crane back up for a closing wide |

The pipeline is the one this family exists for:

```
geo target -> MapCameraRig -> arrive at target -> CameraShot -> Chronon camera language
```

`GeoCameraRig::toCameraShot` converts any rig pose into an ordinary
`motion::CameraShot`, so a dive can be concatenated with transitions, focus pulls,
foreground passes and rack focus without a seam. The mapping is exact: this engine's
pitch-0 nadir is `CameraShot`'s 90-degree elevation, orbit yaw is azimuth, slant
distance is `distance`.

## The motion law (and why it cannot snap)

- **Altitude is never authored by hand.** It follows from the continuous zoom through
  the pyramid's texel-to-screen law (`DynamicPyramidSpec::altitudeMetersAtZoom`), so
  camera and ground cannot disagree about scale.
- **The dive profile is smootherstep** (C²: zero velocity *and* zero acceleration at
  both ends). There is no frame where the descent "starts" as an event.
- **Pitch and yaw phases reuse the same curve**, each holding the previous phase's
  landing state exactly.
- **The camera sits on the sphere** `R = altitude / cos(pitch)` about the anchor, so
  the anchor stays exactly framed at (960, 540) through every pitch and yaw —
  measured at 0.000061 px worst case.

## Acceptance criteria (all certified)

| criterion | test | result |
|---|---|---|
| altitude monotonic during dive | `TestMapDiveAltitudeMonotonic` | max rise 0.000000 m |
| zoom velocity continuous, acceleration bounded | `TestMapZoomVelocityContinuity` | step ≤ 1.8751× mean, accel ≤ ceiling +0.5% discretisation |
| target locked at (960, 540) every frame, every preset | `TestMapDiveTargetLock` | 0.000061 px |
| LOD never skips a quadtree level | `TestMapLODNoMissingTiles` | max jump 1 |
| level handovers keep the shared level dominant | `TestMapLODNoVisibleSeams` | max incoming α 0.046 |
| cross-fade rides the camera exactly | (same) | α step ≤ 1.5 × zoom step |
| handover tile is the ground child of the previous tile | (same) | mismatch 0 |
| prefetch covers every sampled level | `TestMapLODCoverage` | 325 tiles (13 levels × radius-2) |
| camera samples, LOD choices, tile IDs bit-for-bit reproducible | `TestMapDiveDeterminism` | memcmp-identical |
| bridge preserves yaw/pitch/distance/target | `TestGeoCameraBridge` | ≤ 1e-5 |

The C++ tests live in `tests/motion_geospatial.cpp`
(`testGeoCameraDivePhysics`, `testGeoCameraTargetLock`, `testGeoCameraLodContinuity`,
`testGeoCameraDeterminismAndBridge`) and run under CTest as `chrononmotion_geospatial`.

## The runtime (geo_runtime.py)

The family is callable at runtime: text in, certified mp4 out.

```
text ("Lughetto, Campagna Lupia e infine Venezia")
  -> extract_places   heuristic parser (adjacency = one name, connectors =
                      new place; tilt/orbit/metric attach to the named place)
  -> geocode          Nominatim/OpenStreetMap (free, no key)
  -> gate             zoom TV per phase + junction continuity + determinism
  -> render           fast pool (union plates, fork COW), ~20 FPS at 1080p
  -> upload           optional Drive publish
```

- `GeoRuntime().render_tour(text)` returns the tour with its mp4 path and
  resolved stops; `render_place()` renders one resolved place; `--repl` is an
  interactive prompt.
- The **TourBuilder** is one continuous shot: dive to each stop, a two-half
  smootherstep travel leg between stops (out to the midpoint zoom, in to 1.2
  below the landing zoom, so the re-dive starts where the leg ends), glow
  route under the camera, spring pin on every arrival.
- The gate is **phase-aware**: the smootherstep slope bound (1.8751 x mean)
  applies to the total variation *within* each motion phase, and every phase
  junction is checked separately (both ends of the law have zero velocity, so
  a junction step must be a small fraction of the neighbouring phase's
  budget). Two bugs this gate caught during bring-up: a sine-bell leg (C1-
  broken at junctions) and an accidental smootherstep-o-smootherstep
  double-ease (3.5x slope).
- `geo_runtime.py --self-test` certifies the extraction table, the tour
  schedule coverage, and pose continuity offline (no network, no render).

## Why the pixels are rendered from Python (the C++ question)

The question: *if C++ has no equivalent projects, are we stuck paying Python's price
for maps?* The researched answer — no:

**C++ options that exist today:**

- **MapLibre Native** (BSD-2): a real C++ map renderer, GPU-accelerated, actively
  developed. But it renders **vector** tiles (MVT/MBTiles); our imagery pyramid is
  raster JPEG, and its Linux desktop path is documented as "partially supported".
  It would replace ~200 lines of our compositing with a large dependency whose
  style language we do not use.
- **Cesium Native** (Apache-2.0): C++ 3D Tiles streaming — the real foundation for
  **Geo 3D V2** (terrain, 3D buildings, occlusion, horizon). For V1's flat
  multi-LOD imagery dive it is far heavier than the problem.
- **GDAL / Mapnik**: tile *authoring/projection* power tools; neither renders our
  continuous-zoom cross-fade pipeline.

**The decision, recorded:** the engine's *geodesy, LOD law, camera language and every
acceptance test* already live in C++ (`DynamicPyramidMap.hpp`, `GeoCameraRig.hpp`,
`motion_geospatial.cpp`) and do not depend on Python at all. Python owns exactly one
layer: *fetching JPEGs and blitting them* (`dynamic_tile_pyramid.py`, ~200 lines of
numpy/cv2), because raster I/O is the one place the standard library + OpenCV beat
hand-rolled C++ on effort, and OpenCV's SIMD resize/addWeighted is already the
compiled speed we would have written in C++ anyway (38 ms/frame at 1080p ≈ 26 FPS).

So: we are not paying "Python's price" — we are paying one thin, replaceable I/O
shim's price, and the moment Geo 3D V2 needs terrain meshes and occlusion, the
answer changes to **cesium-native** for geometry, keeping the same camera language.

## What V1 does not certify (Geo 3D V2's scope)

terrain elevation, 3D buildings, true pitch at low altitude against real geometry,
occlusion by buildings, terrain-following camera, horizon against real sky.
