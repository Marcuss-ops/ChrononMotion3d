# FACE_RIG_VISEME_V1 — morph-only face performance

## Scope

This is the engine-side face contract. It is deliberately **morph-only**: the
body remains the existing 20-bone/`RigProgram` authority, and the face is a set
of existing morph targets. There is no second facial bone hierarchy and no
runtime morph evaluator in Chronon3D.

`FaceRigMap` names morph indices inside one `RigGeometryBinding`; it does not use
glTF target names because the narrow motion reader does not carry them.

```json
{
  "geometry_id": 1,
  "visemes": {
    "AA": [{"morph": 0, "weight": 1.0}],
    "M":  [{"morph": 1, "weight": 1.0}]
  },
  "blink_morph": 2,
  "gaze_left_morph": 3,
  "gaze_right_morph": 4
}
```

The cue file is engine-side timing, not speech recognition:

```json
{
  "first_frame": 0,
  "last_frame": 149,
  "visemes": [
    {"frame": 20, "viseme": "AA", "weight": 1.0},
    {"frame": 34, "viseme": "M", "weight": 1.0}
  ],
  "blinks": [{"frame": 60, "duration_frames": 4}],
  "gaze": [{"frame": 90, "duration_frames": 12, "horizontal": 0.7}]
}
```

## Semantics

- The closed vocabulary is `Neutral, AA, E, I, O, U, M, F, L, WQ, TH, S, SH`.
  `Neutral` is implicit zero and must not be mapped.
- A viseme cue may map several morph targets; each target has an independent
  peak weight. Duplicate target indices in one viseme are rejected.
- Co-articulation is a linear blend whose window ends at the cue frame. The cue
  frame itself is exact. The default window is 0.08 s; the first cue is
  anticipated so frame 0 does not jump.
- Blink is a sine pulse over `[frame, frame + duration_frames)`, with zero at
  both endpoints and one at the midpoint. Gaze is the same pulse mapped to
  left/right morphs by `horizontal`.
- Applying a performance replaces every mapped morph track with dense keys at
  the program frame rate. Unmapped tracks (for example clip morph animation)
  are left untouched. The caller must rebuild `RigProgramEvaluator` afterwards;
  evaluators are immutable snapshots.
- The bake remains `RigFrameGeometry` positions/normals, so C3D receives baked
  `inline_mesh.animation` and needs no face runtime.

## Fail-closed rules

Unknown geometry, missing morphs, out-of-range indices, duplicate mappings,
explicit `Neutral`, non-monotonic viseme cues, cue frames outside
`[first_frame, last_frame]`, weights outside `[0,1]`, zero-length pulses,
gaze outside `[-1,1]`, empty performances, non-finite/non-positive fps and
negative co-articulation all throw `std::invalid_argument` with the `FaceRig:`
prefix.

## CLI

`chrononmotion_character_plan` accepts `--face-map <json>` and
`--face-cues <json>` for a **single clip** (not `--sequence`). The manifest
records both input hashes, the morph-track count, cue counts and the face
signature under `face`. A rig without morph targets fails before writing a
plan.

## Limits

- No speech-to-text, TTS, forced alignment or FACS 52.
- No facial bones, eyelids geometry, wrinkles or skin simulation.
- No face asset is generated here: the GLB must already carry the morph
  targets. The current low-poly fixture has none, so the CLI success path is
  covered by `chrononmotion_face_rig`, not by that asset.
- C3D software/Vulkan render of a face sequence is not certified by this
  contract.
