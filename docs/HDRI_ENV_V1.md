# HDRI_ENV_V1

`decodeHdriRadiance` accepts an in-memory Radiance RGBE file and returns
linear-light cubemap levels plus a Lambert-convolved irradiance probe. The
reader performs no filesystem or network I/O. `loadHdri(uri, resolver)` calls
only the explicit caller resolver and decodes the returned bytes.

## Input contract and limits

- Signature `#?RADIANCE` or `#?RGBE` and the exact
  `FORMAT=32-bit_rle_rgbe` declaration are required.
- Resolution is limited to canonical `-Y height +X width`; other axis
  orientations fail closed.
- Both raw RGBE scanlines and Radiance per-channel scanline RLE are accepted.
  Old run-length encoding, XYZE, color correction/exposure/header metadata,
  alternate orientation, and trailing bytes are rejected or not interpreted.
- Hard caps: 64 MiB encoded input, 8192×4096 dimensions, 16,777,216 source
  pixels, 1024 cubemap face size, and 33,554,432 output float samples
  including all mip levels. `HdriReadPolicy` can lower each cap but cannot
  raise the hard ceiling. The header is limited to 64 KiB.
- Output face size is the largest power of two not exceeding half the source
  height and the configured face cap. Six faces are ordered +X, -X, +Y, -Y,
  +Z, -Z. RGBE values decode to linear float radiance. Mips use a deterministic
  2×2 box filter per face and continue to 1×1.
- The nine real SH coefficients use a fixed Y-up basis, are integrated over
  equirectangular solid angle, and convolved for Lambert irradiance (band
  factors π, 2π/3, π/4). Coefficients are RGB interleaved.
- Invalid input, policy violations, and resolver errors are reported as
  `std::invalid_argument` with the `hdri:` prefix. An absent resolver is an
  error; URIs are opaque caller-owned names.

## Binary payload

`HdriEnvironment::serialize()` emits deterministic little-endian bytes:

1. Eight ASCII bytes `HDRIENV1`.
2. Four u32 values: source width, source height, base face size, mip count.
3. Two f32 values: rotation radians and intensity (defaults 0 and 1).
4. 27 f32 irradiance SH values (nine RGB coefficients).
5. For each mip: u32 face size, then six faces in the order above, each as
   row-major interleaved RGB f32 samples.

This is the producer-side asset contract for Chronon3D's environment-light
ticket. V3 RenderPlan can reference the asset with `environment_light`, carry
its 27 SH values, and optionally animate `rotation_y`. The typed writer rejects
unsafe paths and out-of-range values. Chronon3D now has a bounded binary
decoder for `HDRIENV1`, and its RenderPlan decoder accepts the schema. The
render compiler resolves and validates the mounted payload, then feeds the SH
probe to the software diffuse lighting path. Visible CLI verification and
specular cubemap sampling remain outstanding.
