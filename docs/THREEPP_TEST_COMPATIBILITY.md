# Upstream threepp test compatibility inventory

- **Repository:** `https://github.com/markaren/threepp`
- **Pinned audit revision:** `e2b165ece3904012d97a08029e0bd6cd5252892a` (the local `upstream/master` ref when this inventory was created).
- **Upstream test framework:** Catch2 v3, linked by upstream's `tests/CMakeLists.txt` as `Catch2::Catch2WithMain`; each suite is a separate executable.
- **Scope requested:** `tests/math`, `tests/core`, and `tests/animation` at the pinned revision. Their upstream file inventories are discoverable through the GitHub tree API and the revision is recorded above.

## Compatibility classification

| Suite | Retained/applicable | Excluded or needs semantic adaptation |
|---|---|---|
| `math/` | Retained math types are generally applicable, but tests that cover removed classes (`Cylindrical`, `Rng`) are excluded because those APIs were intentionally deleted. `Box2`, `Box3`, `Color`, `Euler`, `Frustum`, `MathUtils`, `Matrix4`, `Quaternion`, `Sphere`, `Spherical`, `Triangle`, `Vector2/3/4` can be ported against public ChrononMotion headers. | Any assumptions tied to APIs removed in the fork are not regression requirements. Catch2 macros/helpers need translation to the repository's standalone check harness or a project-approved test dependency. |
| `core/` | `BufferAttribute`, `EventDispatcher`, `Object3D`, and matrix-world equivalence behavior are retained and relevant. | `Layers` is removed (renderer vocabulary); `Raycaster` is deleted; `Uniform` has no current consumer. Those tests must not resurrect removed APIs. Tests of renderer callbacks, userData, shadow/culling flags, instancing, morph target attributes, GPU upload usage, draw ranges, and material raycasts do not apply. |
| `animation/` | `Interpolants_test.cpp` and `KeyframeTrack_test.cpp` are the directly retained, portable suites. | `AnimationMixer_test.cpp` is intentionally excluded: AnimationMixer/Action/PropertyBinding/PropertyMixer are removed so MotionScene remains the sole runtime sampling authority. The clip/track data tests remain applicable where their feature API survived. |

## Framework and provenance gate

ChrononMotion has no third-party dependencies and its boundary test rejects configure-time dependency acquisition. Do not add Catch2 or fetch upstream tests during configure. Re-import applicable assertions as local tests using the existing no-dependency test conventions; preserve the upstream file path/revision and identify every intentional adaptation in the test source or an adjacent manifest. Tests that require removed APIs should be listed as excluded, not stubbed back into the product.

## Ported assertion coverage

`tests/motion_upstream_regressions.cpp` adapts retained assertions from these pinned upstream files into ChrononMotion's no-dependency executable harness:

| Upstream source at pinned SHA | Adapted assertion areas |
|---|---|
| `tests/math/Vector2_test.cpp` | length, normalization, angle units, centered rotation |
| `tests/math/Vector3_test.cpp` | add/subtract, angle units, scalar clamp, cross product |
| `tests/math/Vector4_test.cpp` | homogeneous Matrix4 transform |
| `tests/math/Quaternion_test.cpp` | spherical interpolation normalization and angular midpoint |
| `tests/math/Box2_test.cpp` and `tests/math/Box3_test.cpp` | point bounds, containment/intersection, transformed AABB |
| `tests/math/Matrix4_test.cpp` | determinant fixtures, inverse/product identity, aliased multiply/premultiply, singular inversion |
| `tests/core/Object3D_test.cpp` and `tests/core/MatrixWorldForceEquivalence_test.cpp` | parent world composition, local/world round-trip, externally driven matrix propagation |
| `tests/core/EventDispatcher_test.cpp` | subscription dispatch/unsubscribe and mutation during dispatch; ChrononMotion deliberately skips a listener removed before its turn, unlike threepp's copied-list dispatch behavior |
| `tests/animation/Interpolants_test.cpp` | linear clamp/vector interpolation, discrete holds, cubic key hits, normalized quaternion slerp |
| `tests/animation/KeyframeTrack_test.cpp` | malformed input rejection, sample stride, retained interpolation selection |
| `tests/core/BufferAttribute_test.cpp` | strided vector access and independent clone storage |

Catch2's assertion vocabulary is translated to `motion_check.hpp`; `threepp` names map to the corresponding `chrononmotion` API. Runtime-specific assertions and APIs removed from this fork are excluded rather than stubbed. The `Object3D` assertions are restricted to retained graph semantics; they do not reintroduce renderer helper, culling, or Layer behavior. EventDispatcher mutation behavior is intentionally adapted: the fork snapshots subscription identities but checks liveness before each callback, so listeners removed earlier in the same dispatch are skipped; the upstream copied-list implementation invokes the already-copied listener. This regression pins the fork contract rather than asserting parity for that behavior.

This port covers representative applicable regression assertions and does not claim a mechanical line-for-line import of every upstream suite. Extend the table when more retained assertions are adapted, preserving the same pinned source provenance and explicit exclusions.
