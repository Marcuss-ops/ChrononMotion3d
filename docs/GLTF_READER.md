# glTF reader contracts

ChrononMotion exposes two deliberately separate import routes. `loadGltf`
imports static render-neutral scene geometry; `loadGltfMotion` imports a
narrow in-memory motion asset for `RigProgram` authoring and baking. Neither
route performs file or network I/O, and motion assets do not implicitly enter
the static `Scene` route.

## Static `loadGltf` route

`chrononmotion/assets/GltfReader.hpp` is an in-memory, dependency-free import boundary. It turns supported glTF primitive attributes into `BufferGeometry` and `Mesh` objects under a renderer-neutral `Scene` / `Group` graph. Static `materials` with metallic-roughness factors and a bounded base-color texture reference are imported as data-only `PbrMaterial` objects. The URI is retained as a mounted-root-relative asset identity; ChrononMotion does not read image bytes or access the filesystem/network.

## Accepted input

- glTF 2.0 JSON (`asset.version == "2.0"`) and GLB 2.0 only.
- Only the selected/default scene and its reachable nodes are instantiated. Node order, mesh primitive order, and output mesh order are stable.
- Static triangle lists (`mode == TRIANGLES`, including the default mode). Each primitive requires `POSITION` as a non-normalized `FLOAT VEC3`. Optional `NORMAL` (`FLOAT VEC3`) and `TEXCOORD_0` (`FLOAT VEC2`) must match the position count and are imported as float attributes.
- Indices may be non-normalized unsigned `SCALAR` `UNSIGNED_BYTE`, `UNSIGNED_SHORT`, or `UNSIGNED_INT`; their count must be divisible by three and each index must be in range. Non-indexed triangle lists are accepted when vertex count is divisible by three.
- Node `translation`, `rotation` (normalized quaternion), `scale`, or column-major `matrix` are carried on ordinary `Object3D` groups. A node may specify matrix or TRS, not both. Mesh primitives become child `Mesh` nodes.
- Optional `materials` support `pbrMetallicRoughness.baseColorFactor`, `metallicFactor`, `roughnessFactor`, `emissiveFactor`, `alphaMode`/`alphaCutoff`, and `doubleSided`; primitive material references are range-checked. Base-color and metallic-roughness textures may reference external `images[].uri` values through `textures[]`, with explicit CLAMP_TO_EDGE samplers and `TEXCOORD_0`. Their URIs pass through `meshAttachmentFromGltf` into the V3 mesh asset and inline texture slots; the asset owner mounts images under those paths. Embedded images, non-clamp wrapping, texture transforms, nonzero `texCoord`, and material extensions fail closed.
- Buffers can be base64 `data:application/octet-stream` or `data:application/gltf-buffer` URIs, GLB's first BIN chunk, or external URIs supplied by a caller resolver. The resolver returns bytes; ChrononMotion never accesses the filesystem or network itself.

## Limits and validation

- JSON input: 16 MiB; nesting: 128; buffers: 256 MiB each; GLB container: buffer + JSON limit plus chunk overhead.
- At most 100,000 nodes, meshes, or total primitives; 1,000,000 bufferViews/accessors; 4,000,000 values per accessor; at most 256 buffers.
- Aggregate declared and resolved buffer bytes are limited to 512 MiB. Per-buffer bytes are limited to 256 MiB. GLB JSON is padded with spaces and BIN padding is at most three bytes; non-GLB buffer payloads must exactly match `byteLength`.
- Accessor ranges, strides, alignment, component/type vocabulary, bufferView references, all referenced indices, finite float data, required POSITION min/max values (checked against decoded VEC3 positions), duplicate JSON keys, JSON/GLB lengths and chunk ordering are checked. Scene traversal and instantiated mesh counts are bounded. Unknown object members fail closed except explicitly metadata-only `extras`; invalid or unsupported input throws `std::invalid_argument` with a `glTF:` prefix.

## Static route exclusions

Textures, animations, skins, morph targets/weights, sparse accessors, required or declared extensions, compression, non-triangle modes, extra vertex semantics, missing/unsupported scenes, and unresolved external URIs are not interpreted. These inputs are rejected rather than silently approximated. The implementation currently requires a `scenes` entry, a selected scene with at least one mesh primitive, and all buffer/accessor declarations used by the input to satisfy its strict checks.

This route intentionally favors predictable geometry import over broad interchange compatibility. Animation, skin, and morph data remain rejected here.

## Motion `loadGltfMotion` route

`loadGltfMotion` accepts glTF 2.0 JSON with an explicit caller resolver for
external buffers. It returns a `GltfMotionAsset` containing a bounded rig
hierarchy, skin bindings, indexed triangle POSITION geometry, POSITION morph
deltas, optional base normals and NORMAL morph deltas, and clips.
JOINTS_0/WEIGHTS_0 and inverse-bind matrices are validated.
Animation channels support translation, rotation, scale, and morph weights
with `LINEAR`, `STEP`, or glTF `CUBICSPLINE` interpolation. CUBICSPLINE uses
per-second Hermite tangents (scaled by each key interval); quaternion channels
interpolate components and normalize the result. `GltfMotionClipLibrary` is
the named runtime authority: callers select a clip explicitly and sample its
rig pose/geometry by absolute frame and FPS. The clips can also be mapped into
`RigProgram` and deterministically baked; the Chronon3D adapter serializes
baked geometry as `inline_mesh.animation` V3 samples.

The motion importer shares JSON and buffer policy limits with the static
reader (16 MiB JSON, 256 MiB per buffer, 512 MiB aggregate). Its motion-specific
caps include 10,000 nodes, meshes, and primitives; 1,000,000 vertices; 256
skins; 10,000 joints per skin; 256 morph targets per primitive; 1,000,000
aggregate morph vertices; 256 clips; and 1,000,000 animation key values.
Invalid or unsupported motion input throws `std::invalid_argument` with a
`glTF:` prefix.

The motion route is intentionally V1: GLB containers, materials, sparse
accessors, extensions, compression, and
non-triangle primitives are not supported. These exclusions are independent
of the static route's own fail-closed contract.
