# Autorig GLB (no DCC)

`tools/autorig/autorig_glb.py` rigs a static character GLB, attaches garment
meshes and bakes animation to static frames, with no Blender and no
third-party dependency.

Why it exists: the static mesh loader rejects skinned GLB, and the motion
reader imports skins but rejects materials. So rigging happens here, and
motion reaches the renderer as one static GLB per frame (CPU skinning).

## Commands

```bash
python3 tools/autorig/autorig_glb.py inspect mesh.glb
python3 tools/autorig/autorig_glb.py rig mesh.glb -o rigged.glb \
    [--arms down|t] [--clothes tee.glb] [--clothes-bone chest]
python3 tools/autorig/autorig_glb.py validate rigged.glb
python3 tools/autorig/autorig_glb.py bake rigged.glb -o frames/ \
    --clip idle|walk|wave --frames 48
python3 tools/autorig/autorig_glb.py validate frames/frame_000000.glb --expect-static
```

## What `rig` produces

- A 20-bone humanoid skeleton placed from the mesh bounds (spine, neck, head,
  arms, legs), rest pose = translation-only, so bind == identity.
- Envelope weights from bone-segment distance, Laplacian-smoothed over the
  mesh graph, pruned to 4 normalized influences.
- `JOINTS_0` (uint8 VEC4), `WEIGHTS_0` (float VEC4) and `inverseBindMatrices`
  (float MAT4), with node transforms baked into the vertices.
- Garments are merged as extra skinned meshes on the same rig (auto weights, or
  rigid with `--clothes-bone`); materials, textures and UVs are preserved.

`validate` fails on the mistakes that make an engine import fail: missing or
singular inverse binds, bind pose that disagrees with the inverse binds, a mesh
node under a joint, joint indices out of range, weights that do not sum to 1,
more than 4 influences, unweighted vertices. `--expect-static` checks the other
side: no skins, no skin attributes, triangle POSITION with min/max.

## Rendering the frames to MP4

`chrononmotion_render_glb` renders one static GLB per invocation, so a baked
sequence is rendered frame by frame with a locked camera (auto framing would
re-frame every frame because the animated bounds change):

```bash
RENDER=/path/to/ChrononMotion3d/build/dev-render/render/chrononmotion_render_glb
$RENDER --camera-pos=0.805,0.503,1.802 --camera-target=0.000,0.002,0.013 \
        --camera-fov=32 --exposure=0.35 frames/frame_000000.glb out/frame_000000.ppm 1080 1080
ffmpeg -framerate 30 -i out/frame_%06d.ppm -c:v libx264 -crf 18 \
       -pix_fmt yuv420p -movflags +faststart clip.mp4
```

Camera values for a character of height `H` centred at `c`, filling `fit` of a
`fov` lens: `d = H / (2*tan(fov/2)*fit)`, position `c + d*normalize(0.45,0.28,1)`.
The renderer's default lighting needs `--exposure` around 0.35 for a single
character; the default 2.0 clips to white.

## Limits

- Weights are envelope + smoothing, not a heat solver: a character in an
  unusual pose (arms crossed, holding a prop) will need `--arms t` or a manual
  cleanup pass.
- No cloth simulation. Garments deform with the body they are bound to.
- Garments are plain meshes: no UVs are generated, so they render with a
  `baseColorFactor` material unless the source garment ships its own texture.
- Clips are procedural (`idle`, `walk`, `wave`). Arbitrary motion is authored
  elsewhere and imported through `loadGltfMotion`.

Engine motion import additionally requires the geometry-only contract:
`POSITION`, `JOINTS_0`, `WEIGHTS_0` only and no materials
(`rig --motion-only`, or JSON plus buffer resolver).
