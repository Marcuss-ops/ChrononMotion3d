# MATERIAL_PBR_V1

## Scope

This contract carries renderer-independent metallic/roughness material values
from ChrononMotion authoring to the Chronon3D RenderPlan V3 inline mesh.
ChrononMotion static `loadGltf` also imports the supported glTF scalar factors
into `PbrMaterial`. A bounded external base-color image identity travels as the
inline mesh layer's ordinary mounted asset reference; image bytes stay owned by
the caller and are not embedded in RenderPlan.

## Values and bounds

- `PbrMaterial.baseColor`: glTF linear RGBA factors in `[0,1]`; Motion lowers
  the RGB channels into the RenderPlan sRGB color convention and preserves
  opacity in alpha.
- `metallic`: finite scalar in `[0,1]`.
- `roughness`: finite scalar in `[0,1]`; `0` is mirror-like and `1` is fully
  rough. A UI smoothness value maps as `smoothness = 1 - roughness`.
- `emissive`: three finite, non-negative linear RGB values in `[0,10000]`.
  ChrononMotion combines `emissiveColor * emissiveStrength` before emission.
- `double_sided`: boolean. It controls software mesh normal orientation on
  back-facing fragments.
- `alpha_cutoff`: optional finite scalar in `[0,1]`; it is emitted for glTF
  `alphaMode: MASK` and discards fragments below the cutoff. `alphaMode: BLEND`
  continues to use `base_color` alpha for compositing.
- Existing `diffuse`, `specular`, `shininess`, and `rim_intensity` remain for
  the Phong-compatible renderer path. When the exact PBR scalars are present,
  software shading derives its roughness/specular approximation from them.

All fields are optional in RenderPlan for compatibility. Missing exact PBR
fields retain the legacy material behavior. Unknown material fields fail
closed. The decoder, render budget validator, fingerprint, scene materializer,
Motion emitter, and JSON schema use the same limits and field names.

## Texture scope

The static glTF reader accepts `baseColorTexture` and
`metallicRoughnessTexture` only when each resolves through
`textures[]` to an external root-relative `images[].uri`, uses an explicit
CLAMP_TO_EDGE sampler on both axes with nearest/linear non-mip filtering, and
the primitive has `TEXCOORD_0`. Unsafe paths, embedded or buffer-view images,
nonzero texture coordinates, unsupported sampler modes and extensions fail
closed. The image is not read by Motion; the caller mounts it under the emitted
URI. `meshAttachmentFromGltf` transfers that identity, and lowering places it
in the RenderPlan V3 mesh `asset` field with sRGB input. Chronon3D's software
mesh renderer samples base color and alpha and applies MASK cutoff. The
metallic-roughness image identity is retained on `MeshAttachment` and emitted
as `inline_mesh.metallic_roughness_texture`; Chronon3D samples its G/B channels
for prepared meshes. Software sampling of that V3 inline asset remains open.

Normal and occlusion textures remain out of scope.

## Determinism

Material values are serialized as JSON numbers in a fixed field order by the
Motion writer. The V3 content fingerprint includes presence and exact values of
each PBR field, so roughness, metalness, emissive, and side changes invalidate
cached content identity.
