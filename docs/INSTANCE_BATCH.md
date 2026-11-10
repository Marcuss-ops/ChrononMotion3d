# INSTANCE_BATCH

## Scope

`InstanceBatch` is the single instancing contract in ChrononMotion. It exists so
a layer can draw many placements of one mesh without authoring one layer per
copy and without adding a second animation authority.

- The **layer** keeps the animated transform. Motion samples it per frame as it
  does for every other mesh layer.
- The **batch** keeps instance-local matrices only. It is not animated, has no
  tracks, no lifetime and no evaluation state.

For frame `N` instance `i` the renderer composes:

```text
world(N, i) = layer.world(N) * batch.localTransforms[i]
```

Because the batch never consults time, random access is exact and independent of
frame order.

## Values and bounds

`MeshAttachment::instances` is optional; absent means one instance and preserves
the previous scene document byte-for-byte.

When present:

- `localTransforms` must carry at least one and at most
  `InstanceBatch::kMaxInstances` (65536) matrices.
- Every element must be finite.
- Every matrix must be non-singular (`|determinant| >= 1e-8`). A negative
  determinant is legal and represents a mirrored copy.

`InstanceBatch::validate()` enforces all three rules and throws
`std::invalid_argument`. `MotionScene::compile()` re-validates the batch into
`MeshDescriptor::instances`, so a renderer never receives an unchecked batch.

## Scene document

A batch is exported only when present, under the mesh object:

```json
"instances": {
  "localTransforms": [[1,0,0,0, 0,1,0,0, 0,0,1,0, 2,0,0,1]]
}
```

The importer requires exactly 16 row-major numbers per transform, rejects
unknown fields, and validates the reconstructed batch. A scene with no batch
exports no `instances` key.

## Renderer adapter

`threepp::motion::MotionSceneBridge` creates a `threepp::InstancedMesh` for a
mesh descriptor that carries a batch and explicit geometry, uploads each
placement with `setMatrixAt`, and leaves the sampled layer transform on the
wrapper. A batch bound through `bindLayerObject` is applied when the bound
subtree contains an `InstancedMesh`.

Supported in V1:

- explicit geometry on the layer;
- a caller-supplied bound `InstancedMesh`.

Not supported in V1 (fails closed instead of drawing one copy):

- asset-tree instancing (`assetId` plus `instances` with no bound instance
  object);
- RenderPlan V3 emission (`lowerToRenderPlan` refuses a batch because the plan
  has no instance record);
- per-instance animation or per-instance material overrides.

## Out of scope, deliberately

- LOD selection: threepp already ships `LOD` and
  `threepp::geometrylod::generateChain` over the vendored meshoptimizer. This
  contract does not duplicate either; a batch is orthogonal to the LOD level a
  shot chooses.
- A instancing-specific culling or GPU-buffer API: the renderer owns those.
