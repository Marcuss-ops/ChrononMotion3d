# Character plan — `low-poly_man`, 150 frame at 1080×1920

Local producer-to-renderer evidence captured 2026-11-09. It proves that the
production command `chrononmotion_character_plan` can lower a real rigged GLB
to RenderPlan V3 and that Chronon3D's software backend renders every sample.
It is **not** a release certification: both worktrees were dirty and no
same-SHA run exists.

## Inputs

| Input | Value |
|---|---|
| Rig | `render/spike/assets/scene03/low-poly_man.glb` (928,760 bytes, FNV-1a `677ba43cd78bbb90`) |
| Rig topology | 5,088 vertices, 7,632 indices, 53 joints, 4 animation clips |
| Clip | `Idle2`, 0–149 at 30 fps |
| Static base | `.tmp/character/lowpoly_base.glb` (887,788 bytes, FNV-1a `7ecbfa45aa5b46a3`) |
| Base texture | `lowpoly_diffuse.png`, 32×32 RGB, extracted from the GLB |
| Canvas | 1080×1920, 30 fps, 5.000 s |

The base is a static copy of the same GLB with `animations`, `skins`,
`materials`, `textures`, `images`, `samplers`, node `skin`, primitive
`material`/`targets`, and non-POSITION/NORMAL/TEXCOORD_0 attributes removed.
This was necessary because the static mesh loader rejects JOINTS/WEIGHTS and
animation/materials in the base document. Deriving that copy is still a manual
step; the tool does not do it.

Source revisions at capture: CM3D `0420d088f822b6c245eab21130433997583fafb2`
and C3D `bbd47db8e04ad4d07196f8d35b1c93f39f2ad2c5`; both worktrees dirty.
Producer binary SHA-256:
`ec60b47585d908e6cba65600131ae357d2daa0e7ab523337e8a6daec813c594e`.
Software CLI SHA-256:
`49517b0c6b2d6e4509d62033bcdf8cafa3b89f59a4737c9767ebe6c3ba46c1a4`.

## Producer

```bash
ChrononMotion3D/build/character-plan/chrononmotion_character_plan \
  --rig ChrononMotion3D/render/spike/assets/scene03/low-poly_man.glb \
  --mesh .tmp/character/lowpoly_base.glb \
  --strip-rig-materials \
  --clip Idle2 --frames 150 --fps 30 \
  --width 1080 --height 1920 \
  --texture .tmp/character/lowpoly_diffuse.png \
  --base-color 1,1,1,1 --metallic 0 --roughness 0.96 \
  --camera 0,0.858,-2.2 --rotation 0,0,0 --fov 50 \
  --out .tmp/character/lowpoly_idle.plan.json \
  --manifest .tmp/character/lowpoly_idle.manifest.json
```

Two consecutive generations produced byte-identical plan and manifest files:

- plan: 180,637,852 bytes,
  SHA-256 `ed4df0155e4c197ffa52d5b7c7f05d2d207bc7ed354e2d5b11c5f7eb72a9d1b1`;
- manifest: 824 bytes,
  SHA-256 `95c26dedcc5b1ac0b41d0b409837a3b111d333ae1a32b23f089151a76044adb7`;
- `bake_signature=15382456531100992501`, 150 animation samples, 5,088
  vertices, 7,632 indices.

The manifest records 64-bit FNV-1a input/plan digests, not the SHA-256 digests
required by the ticket's acceptance checklist; the plan SHA-256 above was
computed externally. That format mismatch is open.

The 180 MB plan is not stored here; the manifest is.

Three real-asset defects were found and fixed in the tool during this run:

1. root-level `samplers` survived `--strip-rig-materials`;
2. nodes carrying a 4×4 `matrix` were not readable by the motion path; they are
   now decomposed to TRS, failing closed on shear/mirror;
3. Sketchfab `asset.extras` was not removed and broke the static loader.

## Renderer

```bash
cd .tmp/character
Chronon3d/build/chronon/linux-video-release/apps/chronon3d_cli/chronon3d_cli \
  render --plan lowpoly_idle.plan.json --assets-root "$PWD" \
  --backend software --profile preview --frames 0-2 \
  -o lowpoly_probe.mp4
```

On this `--plan` route `--frames 0-2` was ignored and the full 150-sample plan
was rendered. That is a CLI behavior, not evidence of a 3-frame render; use
`--start-frame 0 --end-frame 149` explicitly when the range matters.

The resulting MP4 is stored here:

- 335,725 bytes, H.264, 1080×1920, 30/1 fps, 5.000 s, 150 frames;
- SHA-256 `b08bc719e49bbaa83484671d97f20ae4fa80ff218c5020073f0864c1ca6f69da`.

Checks on the decoded video:

| Check | Result |
|---|---|
| Empty frames | 0 of 150 |
| Non-black pixels per full-resolution frame | 293,049–295,813 (mean 294,913.9) |
| Animated bounds (x/y/z) | x −0.256…0.258, y −0.009…1.716, z −0.129…0.112 |
| Mean luma per frame | 9.623–9.690 (deliberately dark studio framing) |
| Consecutive-frame MAD | mean 0.0180, max 0.0352 |
| Frame 0→37 MAD | 0.2582 |
| Frame 0→75 MAD | 0.1772 |
| Frame 0→112 MAD | 0.2210 |
| Frame 75→149 MAD | 0.1742 |
| Distinct decoded frames | 150 of 150 (SHA-256 in `frame-stats.csv`) |

The motion is a subtle idle: the character moves away from the start pose and
returns close to it by frame 149. It is not a large gesture test.

Software render cost: 71.50 s for 150 frames at 1080×1920, peak RSS 1,860,888
KB (about 1.86 GB). This is a CPU reference run, not a Vulkan performance
claim.

## Artifacts

- `lowpoly_idle_150f.mp4` — the rendered sequence;
- `manifest.json` — exact producer manifest;
- `frame_075.png`, `frame_149.png` — full-resolution samples;
- `frame-stats.csv` — per-frame non-black count, mean luma, consecutive MAD
  and decoded-frame SHA-256.

## Scope

**PASS locally for this asset and this 150-frame idle.** The command exists, the
plan and manifest are deterministic, and all 150 frames contain geometry. Not
covered: a second clip or two-gesture sequence, malformed input tests, Vulkan
or software/Vulkan numerical parity, automatic static-mesh/rig topology
matching, a clean-checkout same-SHA run, or human review of the sequence. The
evidence stores the manifest, MP4, sample frames and hashes, not the 180 MB
plan, so the checklist item "evidence with plan" is not fully satisfied.
