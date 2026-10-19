# T19-B Apollo CameraRig + DOF scene contract

## Subject and assets

Use the five locally stored Wikimedia Commons NASA photographs in `manifest.json`: Buzz Aldrin on the Moon, Apollo 11 launch, Apollo 11 lunar module, Apollo 8 Earthrise, and The Blue Marble. Preserve the original files and their proportions. Every image layer has a separate transparent RGBA frame and a caption layer. No GLB, Blender, network fetch, or generated substitute imagery is used.

## Composition and camera

Canvas 1920×1080, 30 fps, 300 frames. Chronon3D world convention: camera starts at (0,0,-1200), looks toward +Z, vertical FOV 45°, near 10 and far 5000. Photo planes start at world Z -650, -350, -50, +230 and +550. Camera samples are authored and checked by ChrononMotion3D CameraRig, then exported to the RenderPlan V3 camera tracks.

## Motion and timing

- Frames 0–44: establishing view of all five photo planes.
- Frames 45–104: camera orbit/ring move.
- Frames 105–164: rack focus from 550 to 1150 units.
- Frames 165–224: continued orbit and focus hold.
- Frames 225–299: lateral dolly/push and final hold.

## Required layers

24-layer scene: night field; five RGBA frames; five photos; five captions; three SVG path layers; two focus rings; title; HUD; atmospheric overlay. Stress plan adds six small elements to reach 30 layers. The no-DOF and no-camera variants preserve the same composition.

## Elements excluded

No fabricated Apollo imagery, 3D characters, GLB meshes, Blender scene, remote asset access at render time, or post-render scaling used to imitate camera motion.

## Preview acceptance

At least four photo subjects must be identifiable in the 854×480 preview (or its 1920×1080 source), the title and all captions must be readable, all five photo planes must remain inside frame, frames must show transparent centers, and the initial astronaut image must retain visible detail with DOF enabled. Full-video acceptance additionally requires visible parallax at frames 0/75/150/225/299, a measurable rack-focus sharpness change, stable 30 fps/300-frame output, and inspection of PNG samples every 15 frames. Camera interpolation success alone does not certify the cinematic shot.
