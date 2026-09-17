#ifndef THREEPP_THREEPP_HPP
#define THREEPP_THREEPP_HPP

// ChrononMotion — the motion-engine half of threepp.
//
// Everything that needs a windowing system, a GPU, a physics engine, a file
// format loader or a material zoo is gone; what remains is the scene-graph,
// math, camera, curve and keyframe core a deterministic motion compiler runs
// on. See CHRONONMOTION.md for the keep/delete ledger.

#include "threepp/constants.hpp"

#include "threepp/math/MathUtils.hpp"

#include "threepp/core/BufferAttribute.hpp"
#include "threepp/core/BufferGeometry.hpp"
#include "threepp/core/Object3D.hpp"

#include "threepp/animation/AnimationClip.hpp"
#include "threepp/animation/AnimationUtils.hpp"
#include "threepp/animation/KeyframeTrack.hpp"
#include "threepp/animation/tracks/ColorKeyframeTrack.hpp"
#include "threepp/animation/tracks/NumberKeyframeTrack.hpp"
#include "threepp/animation/tracks/QuaternionKeyframeTrack.hpp"
#include "threepp/animation/tracks/VectorKeyframeTrack.hpp"

#include "threepp/extras/core/Curve.hpp"
#include "threepp/extras/core/CurvePath.hpp"
#include "threepp/extras/core/Interpolations.hpp"
#include "threepp/extras/core/Path.hpp"

#include "threepp/extras/curves/CatmullRomCurve3.hpp"
#include "threepp/extras/curves/CubicBezierCurve.hpp"
#include "threepp/extras/curves/CubicBezierCurve3.hpp"
#include "threepp/extras/curves/EllipseCurve.hpp"
#include "threepp/extras/curves/LineCurve.hpp"
#include "threepp/extras/curves/QuadraticBezierCurve.hpp"
#include "threepp/extras/curves/QuadraticBezierCurve3.hpp"
#include "threepp/extras/curves/SplineCurve.hpp"

#include "threepp/materials/Material.hpp"

#include "threepp/objects/Group.hpp"
#include "threepp/objects/Mesh.hpp"
#include "threepp/objects/ObjectWithMaterials.hpp"

#include "threepp/scenes/Scene.hpp"

#include "threepp/cameras/Camera.hpp"
#include "threepp/cameras/OrthographicCamera.hpp"
#include "threepp/cameras/PerspectiveCamera.hpp"

#include "threepp/motion/Motion.hpp"

#endif//THREEPP_THREEPP_HPP
