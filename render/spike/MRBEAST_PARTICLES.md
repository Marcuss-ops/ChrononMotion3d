# MrBeast particle rig preview

`mrbeast_particles.cpp` creates a 10-second 1920×1080 composition with a
stylized volumetric MrBeast character, a curved face surface mapped from the
licensed portrait, a hierarchical C++ bone rig (hips, spine, neck, head,
shoulders, elbows), and twelve floating snack-pack props. Head turn, torso
follow-through, and arm motion are driven by the actual bone pivots; the old
full-body portrait plane is no longer used as the character.

Build from `ChrononMotion3D`:

```sh
cmake --build build/release-fast --target chrononmotion_render_mrbeast_particles -j 6
```

Render from `ChrononMotion3D` (use Xvfb on a machine without a display):

```sh
xvfb-run -a env LIBGL_ALWAYS_SOFTWARE=1 \
  build/release-fast/render/chrononmotion_render_mrbeast_particles \
  render/spike/mrbeast_particles_rig_1920x1080_10s.mp4 1920 1080 301
```

The image source and required CC BY 4.0 attribution are recorded in
`assets/mrbeast_chromo/ATTRIBUTION.txt`.
