cmake_minimum_required(VERSION 3.20)

# ChrononMotion — the motion-engine fork of threepp.
#
# Upstream threepp builds a GL/Vulkan renderer, a windowing frontend, a physics
# and sensor stack, a loader zoo and a Python module. This tree keeps the
# scene-graph/math/animation core and drops the rest, so the build is a single
# header-only-dependency-free static library with no third-party fetch at
# configure time.
project(ChrononMotion
        VERSION 0.1.0
        DESCRIPTION "Deterministic motion/transform engine for Chronon"
        LANGUAGES CXX)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "ChrononMotion is Linux-only")
endif()

option(CHRONONMOTION_BUILD_TESTS "Build the ChrononMotion smoke tests" ${PROJECT_IS_TOP_LEVEL})
option(CHRONONMOTION_BUILD_BENCHMARKS "Build the standalone evaluation benchmark" OFF)
option(CHRONONMOTION_WERROR "Turn compiler warnings into errors" OFF)
set(CHRONON3D_INCLUDE_DIR "" CACHE PATH "Optional sibling Chronon3D include root for building the offline RenderPlan adapter")
set(CHRONON3D_DEPENDENCY_INCLUDE_DIR "" CACHE PATH "Optional Chronon3D dependency include root")

if(PROJECT_IS_TOP_LEVEL AND NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

add_library(chrononmotion STATIC
        # math — the transform/curve foundation
        src/chrononmotion/math/Box2.cpp
        src/chrononmotion/math/Box3.cpp
        src/chrononmotion/math/Color.cpp
        src/chrononmotion/math/Euler.cpp
        src/chrononmotion/math/Interpolant.cpp
        src/chrononmotion/math/Line3.cpp
        src/chrononmotion/math/MathUtils.cpp
        src/chrononmotion/math/Matrix3.cpp
        src/chrononmotion/math/Matrix4.cpp
        src/chrononmotion/math/Plane.cpp
        src/chrononmotion/math/Ray.cpp
        src/chrononmotion/math/Frustum.cpp
        src/chrononmotion/math/Quaternion.cpp
        src/chrononmotion/math/Sphere.cpp
        src/chrononmotion/math/Spherical.cpp
        src/chrononmotion/math/Triangle.cpp
        src/chrononmotion/math/Vector2.cpp
        src/chrononmotion/math/Vector3.cpp
        src/chrononmotion/math/Vector4.cpp
        # core — the node/transform graph
        src/chrononmotion/core/BufferGeometry.cpp
        src/chrononmotion/core/EventDispatcher.cpp
        src/chrononmotion/core/Object3D.cpp
        # geometries — the primitives a Mesh can carry
        src/chrononmotion/geometries/BoxGeometry.cpp
        src/chrononmotion/geometries/PlaneGeometry.cpp
        src/chrononmotion/geometries/SphereGeometry.cpp
        # animation — clips and keyframe tracks
        src/chrononmotion/animation/AnimationClip.cpp
        src/chrononmotion/animation/KeyframeTrack.cpp
        # materials — the abstract material base only
        src/chrononmotion/materials/Material.cpp
        # objects
        src/chrononmotion/objects/Group.cpp
        src/chrononmotion/objects/Mesh.cpp
        src/chrononmotion/objects/ObjectWithMaterials.cpp
        # cameras
        src/chrononmotion/cameras/Camera.cpp
        src/chrononmotion/cameras/OrthographicCamera.cpp
        src/chrononmotion/cameras/PerspectiveCamera.cpp
        # scenes
        src/chrononmotion/scenes/Scene.cpp
        # motion — the layer/transform/timeline/camera model Chronon samples from
        src/chrononmotion/motion/Easing.cpp
        src/chrononmotion/motion/Transform.cpp
        src/chrononmotion/motion/Layer.cpp
        src/chrononmotion/motion/MotionScene.cpp
        src/chrononmotion/motion/SceneExport.cpp
        src/chrononmotion/motion/MotionPath.cpp
        src/chrononmotion/motion/MotionPlane.cpp
        src/chrononmotion/motion/CameraRig.cpp
        src/chrononmotion/motion/Lighting.cpp
        src/chrononmotion/motion/Procedural.cpp
        src/chrononmotion/motion/Constraints.cpp
        src/chrononmotion/motion/OfflineBaking.cpp
        # presets — one translation unit per family, plus the shared vocabulary
        src/chrononmotion/motion/Presets_Common.cpp
        src/chrononmotion/motion/Presets_Place.cpp
        src/chrononmotion/motion/Presets_Transform.cpp
        src/chrononmotion/motion/Presets_Motion.cpp
        src/chrononmotion/motion/Presets_Camera.cpp
        # curves
        src/chrononmotion/extras/core/Curve.cpp
        src/chrononmotion/extras/core/CurvePath.cpp
        src/chrononmotion/extras/core/Path.cpp
        src/chrononmotion/extras/curves/CatmullRomCurve3.cpp
        src/chrononmotion/extras/curves/CubicBezierCurve.cpp
        src/chrononmotion/extras/curves/CubicBezierCurve3.cpp
        src/chrononmotion/extras/curves/EllipseCurve.cpp
        src/chrononmotion/extras/curves/LineCurve.cpp
        src/chrononmotion/extras/curves/QuadraticBezierCurve.cpp
        src/chrononmotion/extras/curves/QuadraticBezierCurve3.cpp
        src/chrononmotion/extras/curves/SplineCurve.cpp)

add_library(ChrononMotion::chrononmotion ALIAS chrononmotion)
# ChrononMotion's retained threepp sources use translation-unit-local scratch
# names and must not be merged by a parent project's global unity setting.
set_target_properties(chrononmotion PROPERTIES UNITY_BUILD OFF)

# The opt-in adapter is an offline authoring executable boundary, never a
# dependency of the lean ChrononMotion library or Chronon3D render path.
if(CHRONON3D_INCLUDE_DIR)
    if(NOT EXISTS "${CHRONON3D_INCLUDE_DIR}/chronon3d/render_plan/render_plan.hpp")
        message(FATAL_ERROR "CHRONON3D_INCLUDE_DIR must name Chronon3D's include root")
    endif()
    add_library(chrononmotion_chronon3d_adapter STATIC
        src/chrononmotion/motion/RenderPlanLowering.cpp)
    target_link_libraries(chrononmotion_chronon3d_adapter PUBLIC chrononmotion)
    target_include_directories(chrononmotion_chronon3d_adapter SYSTEM PUBLIC "${CHRONON3D_INCLUDE_DIR}")
    if(DEFINED CHRONON3D_DEPENDENCY_INCLUDE_DIR)
        target_include_directories(chrononmotion_chronon3d_adapter SYSTEM PUBLIC "${CHRONON3D_DEPENDENCY_INCLUDE_DIR}")
    endif()
    target_compile_features(chrononmotion_chronon3d_adapter PUBLIC cxx_std_20)
    target_compile_options(chrononmotion_chronon3d_adapter PRIVATE -Wall -Wextra)
    target_compile_definitions(chrononmotion_chronon3d_adapter PUBLIC CHRONONMOTION_HAS_CHRONON3D_ADAPTER=1)
    if(CHRONONMOTION_WERROR)
        target_compile_options(chrononmotion_chronon3d_adapter PRIVATE -Werror)
    endif()
endif()

if(CHRONONMOTION_BUILD_BENCHMARKS)
    add_executable(chrononmotion_eval_bench bench/eval_bench.cpp)
    target_link_libraries(chrononmotion_eval_bench PRIVATE chrononmotion)
    target_compile_features(chrononmotion_eval_bench PRIVATE cxx_std_20)
endif()

target_include_directories(chrononmotion PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>)

target_compile_features(chrononmotion PUBLIC cxx_std_20)

target_compile_options(chrononmotion PRIVATE -Wall -Wextra)
if(CHRONONMOTION_WERROR)
    target_compile_options(chrononmotion PRIVATE -Werror)
endif()

if(CHRONONMOTION_BUILD_TESTS)
    enable_testing()
    add_executable(chrononmotion_smoke tests/motion_smoke.cpp)
    target_link_libraries(chrononmotion_smoke PRIVATE chrononmotion)
    add_test(NAME chrononmotion_smoke COMMAND chrononmotion_smoke)

    # The motion model: value primitives, the evaluated hierarchy, the 2.5D view
    # and the camera rig, and finally the canary scene the presets are built for.
    add_executable(chrononmotion_primitives tests/motion_primitives.cpp)
    target_link_libraries(chrononmotion_primitives PRIVATE chrononmotion)
    add_test(NAME chrononmotion_primitives COMMAND chrononmotion_primitives)

    add_executable(chrononmotion_timeline tests/motion_timeline.cpp)
    target_link_libraries(chrononmotion_timeline PRIVATE chrononmotion)
    add_test(NAME chrononmotion_timeline COMMAND chrononmotion_timeline)

    add_executable(chrononmotion_view tests/motion_view.cpp)
    target_link_libraries(chrononmotion_view PRIVATE chrononmotion)
    add_test(NAME chrononmotion_view COMMAND chrononmotion_view)

    add_executable(chrononmotion_presets tests/motion_presets.cpp)
    target_link_libraries(chrononmotion_presets PRIVATE chrononmotion)
    add_test(NAME chrononmotion_presets COMMAND chrononmotion_presets)

    add_executable(chrononmotion_canary tests/motion_canary.cpp)
    target_link_libraries(chrononmotion_canary PRIVATE chrononmotion)
    add_test(NAME chrononmotion_canary COMMAND chrononmotion_canary)

    add_executable(chrononmotion_foundation tests/motion_foundation.cpp)
    target_link_libraries(chrononmotion_foundation PRIVATE chrononmotion)
    add_test(NAME chrononmotion_foundation COMMAND chrononmotion_foundation)

    # Motion kinematics: sub-frame time, easing derivative, per-channel velocity
    # and the scene-level motion API.
    add_executable(chrononmotion_kinematics tests/motion_kinematics.cpp)
    target_link_libraries(chrononmotion_kinematics PRIVATE chrononmotion)
    add_test(NAME chrononmotion_kinematics COMMAND chrononmotion_kinematics)

    # The motion-authoring gaps that were open against After Effects: the track
    # modifier chain (loop/posterize/delay/link), the keyframed time remap, lens
    # data on the camera rig, mesh layers and the scene JSON export.
    add_executable(chrononmotion_authoring tests/motion_authoring.cpp)
    target_link_libraries(chrononmotion_authoring PRIVATE chrononmotion)
    if(CHRONON3D_INCLUDE_DIR)
        target_link_libraries(chrononmotion_authoring PRIVATE chrononmotion_chronon3d_adapter)
    endif()
    add_test(NAME chrononmotion_authoring COMMAND chrononmotion_authoring)

    # Architectural guardrail: this tree must not reference content machinery
    # (text shaping, fonts, glyphs, raster, image/video decoding) or acquire a
    # dependency. See tests/check_motion_boundary.cmake.
    add_test(NAME chrononmotion_boundary
            COMMAND ${CMAKE_COMMAND}
            -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_motion_boundary.cmake)
endif()
