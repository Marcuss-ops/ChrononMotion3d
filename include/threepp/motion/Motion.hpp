// ChrononMotion — the motion model in one include.
//
//   Transform     Position/Anchor/Scale XYZ, Rotation X/Y/Z, Orientation, Opacity
//   Track         keyframes, per-segment easing, deterministic sampling
//   Easing        linear / ease in-out / custom cubic Bézier / overshoot / bounce
//   Layer         identity, parent, content, in/out lifetime, animated transform
//   MotionScene   hierarchy (Parent, Null) + deterministic frame/PTS evaluation
//   MotionPath    line / cubic Bézier / Catmull-Rom + orient-to-path
//   MotionPlane   2.5D text and image planes, Z depth, perspective + parallax
//   CameraRig     perspective, look-at, dolly/pan/tilt/orbit/roll, FOV/focal
//   Presets       the YouTube-oriented presets, built on the primitives above
//
// The content side stays Chronon's: this header only sees ContentRef, a neutral
// rectangle plus an opaque id. See CHRONONMOTION.md for the boundary rule.

#ifndef THREEPP_MOTION_MOTION_HPP
#define THREEPP_MOTION_MOTION_HPP

#include "threepp/motion/Content.hpp"
#include "threepp/motion/Easing.hpp"
#include "threepp/motion/Transform.hpp"
#include "threepp/motion/Track.hpp"
#include "threepp/motion/Layer.hpp"
#include "threepp/motion/MotionScene.hpp"
#include "threepp/motion/MotionPath.hpp"
#include "threepp/motion/MotionPlane.hpp"
#include "threepp/motion/CameraRig.hpp"
#include "threepp/motion/Presets.hpp"

#endif//THREEPP_MOTION_MOTION_HPP
