# Static glTF/GLB reader contract

`chrononmotion/assets/GltfReader.hpp` is an in-memory, dependency-free import boundary. It turns supported glTF primitive attributes into `BufferGeometry` and `Mesh` objects under a renderer-neutral `Scene` / `Group` graph. It does not create materials, textures, animation runtime state, renderer handles, or asset-side file I/O.

## Accepted input

- glTF 2.0 JSON (`asset.version == "2.0"`) and GLB 2.0 only.
- Only the selected/default scene and its reachable nodes are instantiated. Node order, mesh primitive order, and output mesh order are stable.
- Static triangle lists (`mode == TRIANGLES`, including the default mode). Each primitive requires `POSITION` as a non-normalized `FLOAT VEC3`. Optional `NORMAL` (`FLOAT VEC3`) and `TEXCOORD_0` (`FLOAT VEC2`) must match the position count and are imported as float attributes.
- Indices may be non-normalized unsigned `SCALAR` `UNSIGNED_BYTE`, `UNSIGNED_SHORT`, or `UNSIGNED_INT`; their count must be divisible by three and each index must be in range. Non-indexed triangle lists are accepted when vertex count is divisible by three.
- Node `translation`, `rotation` (normalized quaternion), `scale`, or column-major `matrix` are carried on ordinary `Object3D` groups. A node may specify matrix or TRS, not both. Mesh primitives become child `Mesh` nodes.
- Buffers can be base64 `data:application/octet-stream` or `data:application/gltf-buffer` URIs, GLB's first BIN chunk, or external URIs supplied by a caller resolver. The resolver returns bytes; ChrononMotion never accesses the filesystem or network itself.

## Limits and validation

- JSON input: 16 MiB; nesting: 128; buffers: 256 MiB each; GLB container: buffer + JSON limit plus chunk overhead.
- At most 100,000 nodes, meshes, or total primitives; 1,000,000 bufferViews/accessors; 4,000,000 values per accessor; at most 256 buffers.
- Aggregate declared and resolved buffer bytes are limited to 512 MiB. Per-buffer bytes are limited to 256 MiB. GLB JSON is padded with spaces and BIN padding is at most three bytes; non-GLB buffer payloads must exactly match `byteLength`.
- Accessor ranges, strides, alignment, component/type vocabulary, bufferView references, all referenced indices, finite float data, required POSITION min/max values (checked against decoded VEC3 positions), duplicate JSON keys, JSON/GLB lengths and chunk ordering are checked. Scene traversal and instantiated mesh counts are bounded. Unknown object members fail closed except explicitly metadata-only `extras`; invalid or unsupported input throws `std::invalid_argument` with a `glTF:` prefix.

## Fail-closed exclusions

Materials and their indices are intentionally ignored only by absence: any render intent remains caller-owned. Animations, skins, morph targets/weights, sparse accessors, required or declared extensions, compression, non-triangle modes, extra vertex semantics, missing/unsupported scenes, and unresolved external URIs are not interpreted. These inputs are rejected rather than silently approximated. The implementation currently requires a `scenes` entry, a selected scene with at least one mesh primitive, and all buffer/accessor declarations used by the input to satisfy its strict checks.

This contract intentionally favors predictable geometry import over broad interchange compatibility. Expanding it requires additional limits and regression fixtures for the new feature family; do not route it through Chronon3D's render-frame path.
