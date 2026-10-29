# LIGHT_RIG_V1

`motion::LightRig` is an in-memory authoring asset for a bounded group of
lights controlled by one parent transform. It owns no renderer or shadow
resources. Stable source order is authoring order; source IDs are unique,
nonzero `uint32` values. A rig contains 1–16 sources. Ambient sources are
excluded because the current RenderPlan light-layer contract has no ambient
layer kind.

Each source contains a stable ID/name, Point/Spot/Directional kind, local
transform and direction, RGB color, nonnegative intensity/range/decay,
Spot half-angle `(0, pi]`, penumbra `[0,1]`, and a shadow flag. Invalid or
non-finite values fail with `std::invalid_argument` prefixed `LightRig:`.
Rig position and Euler rotation tracks are sampled in seconds and applied to
each source in stored order. `serialize()` emits canonical
`LIGHT_RIG_V1` JSON with 9 significant decimal digits, source order preserved,
and all transform/key/easing values included.

The optional Chronon3D adapter appends one Light layer per source and bakes
integer-frame position, intensity, color, and direction samples. The bake
requires finite positive fps and 1–4096 frames. Coordinates follow the
existing Motion-to-RenderPlan light mapping (`z` is negated). It does not
render lights; shading and shadow support remain Chronon3D responsibilities.

Stable editorial preset IDs are `editorial_key_rim_fill`, `four_spot_edge`,
and `soft_top`. `lightRigPresets::animateRotation` keys a shared Euler rotation
track with per-segment easing.
