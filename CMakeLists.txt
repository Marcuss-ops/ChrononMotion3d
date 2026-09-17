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

option(CHRONONMOTION_BUILD_TESTS "Build the ChrononMotion smoke tests" ${PROJECT_IS_TOP_LEVEL})
option(CHRONONMOTION_WERROR "Turn compiler warnings into errors" OFF)

if(PROJECT_IS_TOP_LEVEL AND NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

add_library(chrononmotion STATIC
        # math — the transform/curve foundation
        src/threepp/math/Box2.cpp
        src/threepp/math/Box3.cpp
        src/threepp/math/Color.cpp
        src/threepp/math/Euler.cpp
        src/threepp/math/Interpolant.cpp
        src/threepp/math/Line3.cpp
        src/threepp/math/MathUtils.cpp
        src/threepp/math/Matrix3.cpp
        src/threepp/math/Matrix4.cpp
        src/threepp/math/Plane.cpp
        src/threepp/math/Quaternion.cpp
        src/threepp/math/Sphere.cpp
        src/threepp/math/Spherical.cpp
        src/threepp/math/Triangle.cpp
        src/threepp/math/Vector2.cpp
        src/threepp/math/Vector3.cpp
        src/threepp/math/Vector4.cpp
        # core — the node/transform graph
        src/threepp/core/BufferGeometry.cpp
        src/threepp/core/EventDispatcher.cpp
        src/threepp/core/Object3D.cpp
        # animation — clips and keyframe tracks
        src/threepp/animation/AnimationClip.cpp
        src/threepp/animation/KeyframeTrack.cpp
        # materials — the abstract material base only
        src/threepp/materials/Material.cpp
        # objects
        src/threepp/objects/Group.cpp
        src/threepp/objects/Mesh.cpp
        src/threepp/objects/ObjectWithMaterials.cpp
        # cameras
        src/threepp/cameras/Camera.cpp
        src/threepp/cameras/OrthographicCamera.cpp
        src/threepp/cameras/PerspectiveCamera.cpp
        # scenes
        src/threepp/scenes/Scene.cpp
        # motion — the layer/transform/timeline/camera model Chronon samples from
        src/threepp/motion/Easing.cpp
        src/threepp/motion/Transform.cpp
        src/threepp/motion/Layer.cpp
        src/threepp/motion/MotionScene.cpp
        src/threepp/motion/MotionPath.cpp
        src/threepp/motion/MotionPlane.cpp
        src/threepp/motion/CameraRig.cpp
        src/threepp/motion/Presets.cpp
        # curves
        src/threepp/extras/core/Curve.cpp
        src/threepp/extras/core/CurvePath.cpp
        src/threepp/extras/core/Path.cpp
        src/threepp/extras/curves/CatmullRomCurve3.cpp
        src/threepp/extras/curves/CubicBezierCurve.cpp
        src/threepp/extras/curves/CubicBezierCurve3.cpp
        src/threepp/extras/curves/EllipseCurve.cpp
        src/threepp/extras/curves/LineCurve.cpp
        src/threepp/extras/curves/QuadraticBezierCurve.cpp
        src/threepp/extras/curves/QuadraticBezierCurve3.cpp
        src/threepp/extras/curves/SplineCurve.cpp)

add_library(ChrononMotion::chrononmotion ALIAS chrononmotion)

target_include_directories(chrononmotion PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>)

target_compile_features(chrononmotion PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(chrononmotion PRIVATE /W4 /permissive-)
else()
    target_compile_options(chrononmotion PRIVATE -Wall -Wextra)
    if(CHRONONMOTION_WERROR)
        target_compile_options(chrononmotion PRIVATE -Werror)
    endif()
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

    # Architectural guardrail: this tree must not reference content machinery
    # (text shaping, fonts, glyphs, raster, image/video decoding) or acquire a
    # dependency. See tests/check_motion_boundary.cmake.
    add_test(NAME chrononmotion_boundary
            COMMAND ${CMAKE_COMMAND}
            -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_motion_boundary.cmake)
endif()
