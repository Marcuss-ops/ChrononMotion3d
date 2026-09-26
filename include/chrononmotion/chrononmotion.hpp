#ifndef CHRONONMOTION_HPP
#define CHRONONMOTION_HPP

// ChrononMotion — the motion-engine half of threepp.
//
// Everything that needs a windowing system, a GPU, a physics engine, a file
// format loader or a material zoo is gone; what remains is the scene-graph,
// math, camera, curve and keyframe core a deterministic motion compiler runs
// on. See CHRONONMOTION.md for the keep/delete ledger.

#include "chrononmotion/constants.hpp"

#include "chrononmotion/math/MathUtils.hpp"
#include "chrononmotion/math/Ray.hpp"
#include "chrononmotion/math/Frustum.hpp"

#include "chrononmotion/core/BufferAttribute.hpp"
#include "chrononmotion/core/BufferGeometry.hpp"
#include "chrononmotion/core/Object3D.hpp"
#include "chrononmotion/assets/GltfReader.hpp"

#include "chrononmotion/animation/AnimationClip.hpp"
#include "chrononmotion/animation/AnimationUtils.hpp"
#include "chrononmotion/animation/KeyframeTrack.hpp"
#include "chrononmotion/animation/tracks/ColorKeyframeTrack.hpp"
#include "chrononmotion/animation/tracks/NumberKeyframeTrack.hpp"
#include "chrononmotion/animation/tracks/QuaternionKeyframeTrack.hpp"
#include "chrononmotion/animation/tracks/VectorKeyframeTrack.hpp"

#include "chrononmotion/extras/core/Curve.hpp"
#include "chrononmotion/extras/core/CurvePath.hpp"
#include "chrononmotion/extras/core/Interpolations.hpp"
#include "chrononmotion/extras/core/Path.hpp"

#include "chrononmotion/extras/curves/CatmullRomCurve3.hpp"
#include "chrononmotion/extras/curves/CubicBezierCurve.hpp"
#include "chrononmotion/extras/curves/CubicBezierCurve3.hpp"
#include "chrononmotion/extras/curves/EllipseCurve.hpp"
#include "chrononmotion/extras/curves/LineCurve.hpp"
#include "chrononmotion/extras/curves/QuadraticBezierCurve.hpp"
#include "chrononmotion/extras/curves/QuadraticBezierCurve3.hpp"
#include "chrononmotion/extras/curves/SplineCurve.hpp"

#include "chrononmotion/geometries/geometries.hpp"

#include "chrononmotion/materials/Material.hpp"
#include "chrononmotion/materials/Descriptors.hpp"

#include "chrononmotion/objects/Group.hpp"
#include "chrononmotion/objects/Mesh.hpp"
#include "chrononmotion/objects/ObjectWithMaterials.hpp"

#include "chrononmotion/scenes/Scene.hpp"

#include "chrononmotion/cameras/Camera.hpp"
#include "chrononmotion/cameras/OrthographicCamera.hpp"
#include "chrononmotion/cameras/PerspectiveCamera.hpp"

#include "chrononmotion/motion/Motion.hpp"

#endif//CHRONONMOTION_HPP
