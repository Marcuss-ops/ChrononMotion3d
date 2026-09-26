# ADR: MeshLayer, Object3D, and Layer authority

- **Status:** Current milestone decision: retain separate authorities; any future unification is explicitly deferred pending owner approval.
- **Scope:** ChrononMotion3D authoring graph, static glTF/GLB import, Chronon3d typed RenderPlan adapter.

## Context

ChrononMotion currently has two cooperating but non-identical contracts:

1. `Object3D` is the retained threepp scene-graph node used by math, geometry, cameras, and imported static glTF hierarchy. It owns parent/child topology and object TRS/matrices. `Mesh` owns a shared `BufferGeometry` handle and abstract material handles.
2. `motion::Layer` is the timeline node. It inherits `Object3D` for compatibility, but `MotionScene` authoring links (`parentId`) and its evaluated `Transform`/world state are the authority for frame sampling. `MeshAttachment` carries opaque asset identity plus optional geometry/material descriptors; `CompiledScene` emits descriptors at the authoring boundary.
3. The optional `RenderPlanLowering` adapter flattens evaluated world transforms into renderer-facing typed layer plans. Chronon3d renders the plan; it does not execute Motion solvers.

A glTF reader now builds a neutral `Scene -> Group -> Mesh` object graph from static geometry and node transforms. It neither produces `motion::Layer` instances nor imports material/animation semantics. GlTF objects can be traversed or explicitly referenced by an authoring system; implicit synchronization between the two authorities is not introduced.

## Decision

Keep `Object3D` the authority for retained threepp object graphs and `MotionScene`/`Layer` the authority for authored/evaluated motion until the owners approve a migration design. Do not add a second `MeshLayer` type that duplicates `MeshAttachment`, `Mesh`, content identity, transforms, or lifetime. Do not automatically convert imported `Object3D` trees into `Layer` trees, and do not make `Object3D` evaluation another animation authority.

For the current milestone, static glTF meshes remain `Mesh` nodes in a caller-owned `Scene`; any use in Motion must be an explicit authoring mapping. The RenderPlan adapter continues to require caller-supplied content/shape plans and an opaque mesh asset id rather than guessing asset identity or renderer content.

## Ownership, transforms, and lifetime

- Geometry arrays are owned by shared `BufferGeometry` instances; each primitive Mesh retains a shared geometry reference. Object3D parent links control node lifetime according to the existing owning `add()` contract.
- MotionScene owns stable Layer nodes and is the only authority that samples Motion tracks, constraints, drivers, hierarchy world transforms, and frame lifetime.
- Imported Object3D node transforms are local glTF transforms composed by Object3D. Motion Layer transforms use the Motion `Transform` contract, including anchor and frame lifetime. Conversion must preserve hierarchy and all representable semantics or fail explicitly; matrix/TRS conversion must reject unrepresentable shear/reflection/pivot behavior, not silently approximate.
- Chronon3d consumes only a compiled/render-plan projection. It must not retain Motion solvers or run parsing, baking, or audio analysis in its frame path.

## Alternatives

- **Make Layer the only graph node:** breaks retained threepp math/camera/geometry APIs and risks introducing Motion fields into generic object graphs.
- **Make Object3D the only authority:** requires defining track ownership, time/lifetime, constraints, opacity inheritance, anchor semantics, camera handling, and snapshot/restore on every object; this is not a mechanical rename.
- **Add a distinct MeshLayer:** creates parallel content/geometry/transform state while the existing Layer and MeshAttachment already cover those responsibilities; rejected absent a concrete API gap.
- **Explicit import-to-motion bridge:** viable future migration. It must define deterministic IDs, scene selection, ownership transfer/reference semantics, matrix representability, material/asset identity, and frame lifetime before implementation.

## Consequences and gates

- The glTF reader does not claim to create Motion layers. It returns `GltfAsset { Scene, meshes }` and leaves authoring conversion to the caller.
- No unification code is approved by this ADR. Owner approval is required before changing graph authority or public object lifetime semantics.
- Revisit only after: the glTF fixture contract is stable; the scoped pinned upstream retained-API assertion port is maintained; RenderPlan adapter interop tests validate camera and path operators; and owners choose the source of truth for parent, transform, lifetime, and ownership.
- If an explicit bridge is later approved, record its owner, reason, exit condition, equivalence gate, removal scope, and status as required by Chronon3d Demolition Debt policy.
