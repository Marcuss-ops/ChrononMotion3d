// ChrononMotion — the motion model in one include.
//
//   Transform     Position/Anchor/Scale XYZ, Rotation X/Y/Z, Orientation, Opacity
//   Track         keyframes, per-segment easing, deterministic sampling
//   Easing        linear / ease in-out / custom cubic Bézier / overshoot / bounce
//   Layer         identity, parent, content, in/out lifetime, animated transform
//   MotionScene   hierarchy (Parent, Null) + deterministic frame/PTS evaluation
//   SceneExport   deterministic JSON export of space and time, never content
//   Track         + modifiers: loop / posterize / delay / cross-layer link
//   MotionPath    line / cubic Bézier / Catmull-Rom + orient-to-path
//   MotionPlane   2.5D text and image planes, Z depth, perspective + parallax
//   CameraRig     perspective, look-at, dolly/pan/tilt/orbit/roll, FOV/focal
//   Presets       the YouTube-oriented presets, built on the primitives above
//
// The content side stays Chronon's: this header only sees ContentRef, a neutral
// rectangle plus an opaque id. See CHRONONMOTION.md for the boundary rule.

#ifndef CHRONONMOTION_MOTION_HPP
#define CHRONONMOTION_MOTION_HPP

#include "chrononmotion/motion/Content.hpp"
#include "chrononmotion/motion/Easing.hpp"
#include "chrononmotion/motion/Transform.hpp"
#include "chrononmotion/motion/Track.hpp"
#include "chrononmotion/motion/Layer.hpp"
#include "chrononmotion/motion/MotionScene.hpp"
#include "chrononmotion/motion/SceneExport.hpp"
#include "chrononmotion/motion/MotionPath.hpp"
#include "chrononmotion/motion/MotionPlane.hpp"
#include "chrononmotion/motion/CameraRig.hpp"
#include "chrononmotion/motion/Lighting.hpp"
#include "chrononmotion/motion/CompiledScene.hpp"
#include "chrononmotion/motion/Presets.hpp"
#include "chrononmotion/motion/Procedural.hpp"
#include "chrononmotion/motion/Constraints.hpp"
#include "chrononmotion/motion/Precomp.hpp"
#include "chrononmotion/motion/OfflineBaking.hpp"
#if defined(CHRONONMOTION_HAS_CHRONON3D_ADAPTER)
#include "chrononmotion/motion/RenderPlanLowering.hpp"
#endif

#endif//CHRONONMOTION_MOTION_HPP
