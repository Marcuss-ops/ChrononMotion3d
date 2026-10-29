# Headless clip library

Four tools, one pipeline, no GUI and no Blender:

| Tool | Role |
| --- | --- |
| `render/spike/glb.cpp` | plays a glTF clip (or a chained sequence) offline, frame by frame |
| `render/spike/scene03_seated_desk.cpp` | the seated desk shot; accepts a chain instead of one clip |
| `tools/autorig/add_gestures.py` | authors gesture clips onto an existing humanoid rig |
| `tools/autorig/render_clip_library.py` | every clip (or one sequence) -> MP4 + `library.json` |
| `tools/autorig/verify_clip_library.py` | re-checks the delivered MP4s (frames, crop, motion) |

## Commands

## Runtime API for imported motion clips

The motion-only glTF importer exposes `GltfMotionClipLibrary` as the runtime
selection authority. Construct it from a validated `GltfMotionAsset`, list
available clip names with `clipNames()`, inspect `duration(name)`, and sample
an absolute frame with `sample(name, frame, fps)`. Selection is explicit;
unknown names and inconsistent morph channel topology throw instead of
silently choosing a clip. Sampling is independent of call order.

```cpp
const auto asset = loadGltfMotion(json, resolver);
const GltfMotionClipLibrary library(asset);
const auto poseAndGeometry = library.sample("Walk", frame, 30.f);
```

```bash
# what is inside a GLB
build/dev-render/render/chrononmotion_render_glb --list-anims model.glb

# one clip -> PPM frames, camera pinned so the sequence stays registered
build/dev-render/render/chrononmotion_render_glb --anim=Walk_Loop --anim-loop \
    --camera-pos=1,1.7,1 --camera-target=0,0.9,0 --camera-fov=60 \
    --background=0x9d9d9d model.glb out_frames/ 40 540 540

# one chained performance: clips overlap by --anim-blend seconds
build/dev-render/render/chrononmotion_render_glb \
    --anim-sequence=Idle,G_Point,Idle,G_Shrug --anim-blend=0.22 \
    --anim-fps=30 --background=0x9d9d9d --no-proof model.glb out_frames/ 120 540 540

# a whole library -> MP4 per clip + library.json
python3 tools/autorig/render_clip_library.py model.glb --out library/

# a chained performance -> one MP4
python3 tools/autorig/render_clip_library.py model.glb --out chain/ \
    --sequence "Idle,G_Point,Idle,G_Shrug,Idle" --blend 0.22 --max-seconds 12

# gestures onto a rig, then render them
python3 tools/autorig/add_gestures.py render/spike/assets/scene03/low-poly_man.glb \
    -o models/output_mrbeast/lowpoly_gestures.glb
python3 tools/autorig/add_gestures.py rig.glb --check     # peak-pose measurements

# the seated desk shot with gesture chains (comma separated animation argument)
build/dev-render/render/chrononmotion_render_scene_seated_desk --video \
    room.glb out_frames/ character_gestures.glb \
    "Sitting_Idle_Loop,G_Point,Sitting_Idle_Loop,G_Shrug" 4 24 1080 1080

# prove the delivered files are intact
python3 tools/autorig/verify_clip_library.py models/output_mrbeast/library_gestures
```

Every flag is opt-in: without them the renderer behaves exactly as before (clear
colour still `0x393633` -> `(10, 9, 8)` after exposure, single-clip playback).

## Transitions: why chaining matters

Clips are authored rest-anchored, so a short overlap is enough to join them. The
renderer builds one `AnimationAction` per clip, sets an absolute local time and
an explicit weight on each, and composites with `mixer->update(0)`, so frame N
never depends on frame N-1 and any frame can be rendered alone.

- Actions outside their window are explicitly weighted 0. Forgetting that leaves
  a clip contributing after it ended, which is invisible until a chain has three
  clips.
- Weights are normalised, so the boundary is a true blend rather than a dip.
- Blend length is clamped to half the shortest clip.

Measured on `Idle -> G_Shrug -> Idle` at 24 fps: with `--blend 0` the change is a
single-frame step; with `--blend 0.25` it is spread over six frames, with the
first and last steps as small as any normal in-between frame.

## Gesture vocabulary authored here

`add_gestures.py` writes real glTF animation channels (rotation, plus root
translation where a pose needs it) onto an existing skin, so they play in the
same renderer as any imported clip. Poses are world-space bone aims derived from
the rest pose, which means the same spec works on a 53-joint Sketchfab mannequin
and on the 65-joint UE4 rig.

Motion quality comes from three layers, all generated:

1. **Overshoot.** Each curve climbs past its peak (factor > 1.0) and settles back.
2. **Follow-through.** Every bone evaluates the same curve with its own delay:
   shoulder 20 ms, upper arm 30, forearm 60, hand 90, head 80, chest 30. The
   delayed curve is time-compressed so the bone is still at rest on the last
   frame.
3. **Dense sampling.** Channels are baked at ~24 samples/s, so the exported clip
   carries the shape instead of four linear keys.

| Clip | Duration | Shape |
| --- | --- | --- |
| `G_Wave` | 1.15 s | arm up, forearm wave, head tilt |
| `G_Victory` | 1.15 s | both fists up, chest open |
| `G_Shrug` | 1.05 s | palms up, shoulders lifted, head roll |
| `G_Facepalm` | 1.20 s | hand to forehead, torso sink |
| `G_Think` | 1.30 s | hand to chin, gaze up |
| `G_Point` | 1.15 s | arm out to the side, weight shift |
| `G_Explain` | 1.10 s | both hands open at chest height |
| `G_CrossArms` | 1.20 s | arms folded, chest turned |
| `G_Listen` | 1.30 s | hand to ear, lean in |
| `G_Shh` | 1.25 s | index to lips, slight lean |
| `G_Nod` | 0.95 s | two head nods with recoil |
| `G_ShakeHead` | 0.95 s | head shake, neck lagging |
| `G_Typing` | 1.30 s | both hands forward, fingers drumming |
| `G_Scroll` | 1.15 s | right hand swiping down a feed |
| `G_Paper` | 1.60 s | both hands holding a sheet, head down |
| `G_Count` | 1.55 s | hand at chin, fingers counting |
| `G_Watch` | 1.20 s | forearm across the chest, glance down |
| `G_Knock` | 0.95 s | knuckles toward the lens, three beats |
| `G_ThumbsUp` | 1.20 s | forearm up, thumb out |
| `G_Offer` | 1.35 s | both palms presenting forward |
| `G_Refuse` | 0.95 s | both palms pushing away |
| `G_Sneak` | 1.40 s | knees bent, hips lowered 18 % of height, torso in |

`--check` measures each peak pose in bone space and rejects a clip that does not
do what its name says: hand near the skull, hand above the head, hands forward,
head actually rotated, body actually lowered. The hand-to-head limit is expressed
as a fraction of that rig's own rest reach, because a long-armed rig sits further
from the skull with an identical pose. Current state: 22/22 accepted on both the
53-joint mannequin and the 65-joint UE4 rig.

## What is in the repository and what came out

| Asset | Skeleton | Clips | Output |
| --- | --- | --- | --- |
| `render/spike/assets/scene03/UAL1_Standard.glb` | UE4, 65 joints | 43 | `models/output_mrbeast/library_ual1/` |
| `render/spike/assets/scene03/low-poly_man.glb` | 53 joints | 4 | `models/output_mrbeast/library_lowpoly/` |
| `models/output_mrbeast/lowpoly_gestures.glb` | same 53 joints | 4 + 22 | `models/output_mrbeast/library_gestures/` |
| same | same | 8 key gestures at 1080x1080 | `models/output_mrbeast/library_gestures_1080/` |
| same | same | 1 chained performance | `models/output_mrbeast/library_transitions/` |
| `models/output_mrbeast/ual1_seated_gestures.glb` | UE4, 65 joints | 43 + 22 | used by the seated desk shot |

The seated desk shot consumes the same chain syntax: passing
`"Sitting_Idle_Loop,G_Point,Sitting_Idle_Loop,G_Shrug,Sitting_Idle_Loop"` as the
animation argument renders the character performing the gestures in the room,
with the same 0.25 s crossfades. Measured on the shipped 6 s 1920x1080 render,
frame-to-frame change is 2.6x the same shot with `Sitting_Idle_Loop` alone
(mean 1.93 vs 0.74, peak 10.7 vs 1.5), which is how the gestures were confirmed
to reach the scene and not just the library.

The 43 UE4 clips cover locomotion, combat, interactions and posture:
`A_TPose, Crouch_Fwd_Loop, Crouch_Idle_Loop, Dance_Loop, Death01, Driving_Loop,
Fixing_Kneeling, Hit_Chest, Hit_Head, Idle_Loop, Idle_Talking_Loop,
Idle_Torch_Loop, Interact, Jog_Fwd_Loop, Jump_Land, Jump_Loop, Jump_Start,
PickUp_Table, Pistol_Aim_Down/Neutral/Up, Pistol_Idle_Loop, Pistol_Reload,
Pistol_Shoot, Punch_Cross, Punch_Jab, Push_Loop, Roll, Sitting_Enter,
Sitting_Exit, Sitting_Idle_Loop, Sitting_Talking_Loop, Spell_Simple_Enter/Exit/
Idle_Loop/Shoot, Sprint_Loop, Swim_Fwd_Loop, Swim_Idle_Loop, Sword_Attack,
Sword_Idle, Walk_Formal_Loop, Walk_Loop`.

## Framing: the reason for `--background`

The loader's measured bounds are unreliable for these assets (`low-poly_man.glb`
is scaled 0.00167), so the batch never trusts the renderer's auto framing. It
renders a 9-frame probe across the whole clip, measures the silhouette against a
flat studio colour, and solves position/target/fov from that. Three details
matter:

- **The probe needs a background the subject cannot be confused with.** The
  mannequins are dark; on the default near-black clear colour the silhouette is
  underestimated and the camera ends up far too close. `--background=0x9d9d9d`
  (studio grey, `(86, 86, 86)` after exposure) makes the measurement stable.
- **One measure-then-place step is not enough.** Closing in magnifies parallax, so
  the silhouette centre drifts even though the look-at point did not move. The
  batch re-measures the frames it produced and re-solves, up to five times.
- **A clipped frame cannot be measured.** When the subject leaves the frame the
  fill ratio saturates at 1.0, so the solver stops trusting it and pushes out by a
  fixed minimum instead; every correction is clamped (0.92..1.45 per step) so the
  next iteration cannot overshoot back inside.

The border test itself is full resolution on the outer 3-pixel band, because the
subsampled silhouette search can walk past a fingertip and H.264 chroma
subsampling bleeds a couple of pixels outwards: a one-frame crop is still a crop.

## Verification

`verify_clip_library.py` reads the MP4s, not the renderer's report, and fails a
clip when the decoded frame count disagrees with the manifest, when any frame
touches the border, when the subject height leaves the 45-99 % band, or when the
picture does not change for a clip that is meant to move.

Current state: 26/26 gestures (including the 4 locomotion clips in that GLB),
4/4 lowpoly, 43/43 UE4 — zero failures.

## Known limits

- Framing is still solved per clip, so a clip whose pose changes size a lot trades
  fill for guaranteed no-crop (`Swim_Fwd_Loop` fills 53 % of the height because it
  is horizontal).
- Chains are linear: no branching, no additive layers, no IK. A clip cannot react
  to where the previous one stopped beyond the crossfade.
- Gestures are authored, not captured. They read as clean stylised animation, not
  mocap; blending two very different poses over 0.2 s still looks fast.
- Retargeting a foreign clip pack onto this 20-bone MrBeast rig is **not**
  implemented. Imported packs must ship their own skeleton, which the pipeline
  handles unchanged.
