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
option(CHRONONMOTION_BUILD_RENDER "Build the offscreen GL render module (explicit opt-in; headless core is the default)" OFF)
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
        src/chrononmotion/math/Color_css.cpp
        src/chrononmotion/geospatial/MapFlyover.cpp
        src/chrononmotion/geospatial/WebMercator.cpp
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
        # assets — dependency-free, explicit-buffer glTF input
        src/chrononmotion/assets/GltfReader.cpp
        src/chrononmotion/assets/GltfReaderInternal.hpp
        src/chrononmotion/assets/GltfReaderScene.cpp
        src/chrononmotion/assets/GltfReaderSerialization.cpp
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
        src/chrononmotion/motion/MotionScene_layers.cpp
        src/chrononmotion/motion/MotionScene_evaluation.cpp
        src/chrononmotion/motion/MotionScene_validation.cpp
        src/chrononmotion/motion/SceneExport.cpp
        src/chrononmotion/motion/SceneImport.cpp
        src/chrononmotion/motion/Interaction.cpp
        src/chrononmotion/motion/RigRetarget.cpp
        src/chrononmotion/motion/MotionPath.cpp
        src/chrononmotion/motion/CameraTrajectory.cpp
        src/chrononmotion/motion/MotionPlane.cpp
        src/chrononmotion/motion/CameraRig.cpp
        src/chrononmotion/motion/CameraFraming.cpp
        src/chrononmotion/motion/CameraValidation.cpp
        src/chrononmotion/motion/CameraMotion.cpp
        src/chrononmotion/motion/CameraMotionRecipes.cpp
        src/chrononmotion/motion/CameraMotionRecipesAdvanced.cpp
        src/chrononmotion/motion/CameraMotionRecipesExtended.cpp
        src/chrononmotion/motion/CameraRecipe.cpp
        src/chrononmotion/motion/CameraMotionRecipeRegistry.cpp
        src/chrononmotion/motion/CameraSequence.cpp
        src/chrononmotion/motion/DocumentaryCameraPath.cpp
        src/chrononmotion/motion/CameraPanelChoreography.cpp
        src/chrononmotion/motion/CameraFocusSequence.cpp
        src/chrononmotion/motion/TitleCameraRecipe.cpp
        src/chrononmotion/motion/CameraShake.cpp
        src/chrononmotion/motion/Lighting.cpp
        src/chrononmotion/motion/AuthoringPolicy.cpp
        src/chrononmotion/motion/Procedural.cpp
        src/chrononmotion/motion/Constraints.cpp
        src/chrononmotion/motion/RigGraph.cpp
        src/chrononmotion/motion/RigDrivers.cpp
        src/chrononmotion/motion/RigDeformers.cpp
        src/chrononmotion/motion/RigBaking.cpp
        src/chrononmotion/motion/RigProgram.cpp
        src/chrononmotion/motion/RigOperators.cpp
        src/chrononmotion/motion/OfflineBaking.cpp
        src/chrononmotion/motion/OfflineAudio.cpp
        src/chrononmotion/motion/OfflineLayout.cpp
        src/chrononmotion/motion/OfflinePathBaking.cpp
        src/chrononmotion/motion/OfflineCollections.cpp
        src/chrononmotion/motion/SvgPathParser.cpp
        # presets — one translation unit per family, plus the shared vocabulary
        src/chrononmotion/motion/Presets_Common.cpp
        src/chrononmotion/motion/Presets_Place.cpp
        src/chrononmotion/motion/Presets_Transform.cpp
        src/chrononmotion/motion/Presets_Motion.cpp
        src/chrononmotion/motion/Presets_Camera.cpp
        src/chrononmotion/motion/Presets_Environment.cpp
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
    # RenderPlan's public headers use fmt formatting/validation APIs; resolve
    # them even in adapter builds that do not inherit C3D's vcpkg toolchain.
    find_package(fmt CONFIG REQUIRED) # boundary-guardrail: allowed — public C3D RenderPlan headers expose fmt in this offline adapter only.
    add_library(chrononmotion_chronon3d_adapter STATIC
        src/chrononmotion/motion/RenderPlanLowering.cpp
        src/chrononmotion/motion/RenderPlanEmission.cpp
        src/chrononmotion/motion/RenderPlanEmissionCommon.cpp
        src/chrononmotion/motion/RenderPlanEmissionMedia.cpp
        src/chrononmotion/motion/RenderPlanEmissionShape.cpp
        src/chrononmotion/motion/RenderPlanEmissionText.cpp
        src/chrononmotion/motion/RenderPlanEmissionValidation.cpp
        src/chrononmotion/motion/RenderPlanEmissionValidationBounds.cpp
        src/chrononmotion/motion/RenderPlanEmissionValidateEffects.cpp
        src/chrononmotion/motion/RenderPlanEmissionValidateLayer.cpp
        src/chrononmotion/motion/RenderPlanEmissionValidateText.cpp
        src/chrononmotion/motion/RenderPlanEmissionValidateTracks.cpp)
    target_link_libraries(chrononmotion_chronon3d_adapter PUBLIC chrononmotion fmt::fmt-header-only)
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

    # The showreel generator: the one place in this tree that turns a Motion
    # scene into a renderable `chronon.render-plan.v2` file. A tool, never part
    # of the library or of Chronon3d's runtime.
    add_executable(chrononmotion_plan_showreel tools/plan_showreel.cpp)
    target_link_libraries(chrononmotion_plan_showreel PRIVATE chrononmotion chrononmotion_chronon3d_adapter)
    target_compile_features(chrononmotion_plan_showreel PRIVATE cxx_std_20)
    target_compile_options(chrononmotion_plan_showreel PRIVATE -Wall -Wextra)
    if(CHRONONMOTION_WERROR)
        target_compile_options(chrononmotion_plan_showreel PRIVATE -Werror)
    endif()

    # The word rack-focus generator: the reference Screenrecording proof of
    # item 3, one place authors the united camera+DoF scene and lowers it V3.
    add_executable(chrononmotion_plan_word_rack_focus tools/plan_word_rack_focus.cpp)
    target_link_libraries(chrononmotion_plan_word_rack_focus PRIVATE chrononmotion chrononmotion_chronon3d_adapter)
    target_compile_features(chrononmotion_plan_word_rack_focus PRIVATE cxx_std_20)
    target_compile_options(chrononmotion_plan_word_rack_focus PRIVATE -Wall -Wextra)
    if(CHRONONMOTION_WERROR)
        target_compile_options(chrononmotion_plan_word_rack_focus PRIVATE -Werror)
    endif()

    # The map flyover generator: the geospatial counterpart of the showreel, and
    # the only place a georeferenced Motion scene becomes a renderable plan.
    add_executable(chrononmotion_map_flyover tools/plan_map_flyover.cpp)
    target_link_libraries(chrononmotion_map_flyover PRIVATE chrononmotion chrononmotion_chronon3d_adapter)
    target_compile_features(chrononmotion_map_flyover PRIVATE cxx_std_20)
    target_compile_options(chrononmotion_map_flyover PRIVATE -Wall -Wextra)
    if(CHRONONMOTION_WERROR)
        target_compile_options(chrononmotion_map_flyover PRIVATE -Werror)
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
# Internal translation-unit headers (JsonDom, JsonWriter, OfflineBakingInternal): shared by motion
# sources across directories, never installed, never public API.
target_include_directories(chrononmotion PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/chrononmotion/motion)

target_compile_features(chrononmotion PUBLIC cxx_std_20)

target_compile_options(chrononmotion PRIVATE -Wall -Wextra)
if(CHRONONMOTION_WERROR)
    target_compile_options(chrononmotion PRIVATE -Werror)
endif()

if(CHRONONMOTION_BUILD_TESTS)
    enable_testing()
    add_executable(chrononmotion_rig tests/motion_rig.cpp)
    target_link_libraries(chrononmotion_rig PRIVATE chrononmotion)
    target_compile_options(chrononmotion_rig PRIVATE -Wall -Wextra)
    if(CHRONONMOTION_WERROR)
        target_compile_options(chrononmotion_rig PRIVATE -Werror)
    endif()
    add_test(NAME chrononmotion_rig COMMAND chrononmotion_rig)
    add_executable(chrononmotion_two_bone tests/motion_two_bone.cpp)
    target_link_libraries(chrononmotion_two_bone PRIVATE chrononmotion)
    target_compile_options(chrononmotion_two_bone PRIVATE -Wall -Wextra)
    if(CHRONONMOTION_WERROR)
        target_compile_options(chrononmotion_two_bone PRIVATE -Werror)
    endif()
    add_test(NAME chrononmotion_two_bone COMMAND chrononmotion_two_bone)
    add_executable(chrononmotion_rig_program tests/motion_rig_program.cpp)
    target_link_libraries(chrononmotion_rig_program PRIVATE chrononmotion)
    target_compile_options(chrononmotion_rig_program PRIVATE -Wall -Wextra)
    if(CHRONONMOTION_WERROR)
        target_compile_options(chrononmotion_rig_program PRIVATE -Werror)
    endif()
    add_test(NAME chrononmotion_rig_program COMMAND chrononmotion_rig_program)
    add_executable(chrononmotion_rig_pipeline tests/motion_rig_pipeline.cpp)
    target_link_libraries(chrononmotion_rig_pipeline PRIVATE chrononmotion)
    target_compile_options(chrononmotion_rig_pipeline PRIVATE -Wall -Wextra)
    if(CHRONONMOTION_WERROR)
        target_compile_options(chrononmotion_rig_pipeline PRIVATE -Werror)
    endif()
    add_test(NAME chrononmotion_rig_pipeline COMMAND chrononmotion_rig_pipeline)
    add_executable(chrononmotion_rig_canaries tests/motion_rig_canaries.cpp)
    target_link_libraries(chrononmotion_rig_canaries PRIVATE chrononmotion)
    target_compile_options(chrononmotion_rig_canaries PRIVATE -Wall -Wextra)
    if(CHRONONMOTION_WERROR)
        target_compile_options(chrononmotion_rig_canaries PRIVATE -Werror)
    endif()
    add_test(NAME chrononmotion_rig_canaries COMMAND chrononmotion_rig_canaries --all)
    foreach(_rig_case fk_arm two_bone_ik eight_bone_snake pole_knee ik_reach_limits fk_ik_switch
            space_switch aim_turret rotation_limit stretch_arm spline_tail secondary_tail svg_rigid svg_puppet
            svg_lattice skin_mesh pose_mixer controller_joystick gltf_skeleton mega_character)
        add_test(NAME chrononmotion_rig_${_rig_case} COMMAND chrononmotion_rig_canaries --case ${_rig_case})
    endforeach()
    add_executable(chrononmotion_rig_performance tests/motion_rig_performance.cpp)
    target_link_libraries(chrononmotion_rig_performance PRIVATE chrononmotion)
    target_compile_options(chrononmotion_rig_performance PRIVATE -Wall -Wextra)
    if(CHRONONMOTION_WERROR)
        target_compile_options(chrononmotion_rig_performance PRIVATE -Werror)
    endif()
    add_test(NAME chrononmotion_rig_performance COMMAND chrononmotion_rig_performance)

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

    add_executable(chrononmotion_camera_v1 tests/motion_camera_v1.cpp)
    target_link_libraries(chrononmotion_camera_v1 PRIVATE chrononmotion)
    add_test(NAME chrononmotion_camera_v1 COMMAND chrononmotion_camera_v1)

    add_executable(chrononmotion_camera_focus_sequence tests/motion_camera_focus_sequence.cpp)
    target_link_libraries(chrononmotion_camera_focus_sequence PRIVATE chrononmotion)
    add_test(NAME chrononmotion_camera_focus_sequence COMMAND chrononmotion_camera_focus_sequence)

    add_executable(chrononmotion_camera_trajectory tests/motion_camera_trajectory.cpp)
    target_link_libraries(chrononmotion_camera_trajectory PRIVATE chrononmotion)
    add_test(NAME chrononmotion_camera_trajectory COMMAND chrononmotion_camera_trajectory)

    add_executable(chrononmotion_camera_shake tests/motion_camera_shake.cpp)
    target_link_libraries(chrononmotion_camera_shake PRIVATE chrononmotion)
    add_test(NAME chrononmotion_camera_shake COMMAND chrononmotion_camera_shake)

    add_executable(chrononmotion_camera_framing tests/motion_camera_framing.cpp)
    target_link_libraries(chrononmotion_camera_framing PRIVATE chrononmotion)
    add_test(NAME chrononmotion_camera_framing COMMAND chrononmotion_camera_framing)

    add_executable(chrononmotion_camera_sequence tests/motion_camera_sequence.cpp)
    target_link_libraries(chrononmotion_camera_sequence PRIVATE chrononmotion)
    add_test(NAME chrononmotion_camera_sequence COMMAND chrononmotion_camera_sequence)

    add_executable(chrononmotion_documentary_camera_path tests/motion_documentary_camera_path.cpp)
    target_link_libraries(chrononmotion_documentary_camera_path PRIVATE chrononmotion)
    add_test(NAME chrononmotion_documentary_camera_path COMMAND chrononmotion_documentary_camera_path)

    # Eye-trace continuity, screen-space camera math, analytic pose derivatives
    # and multi-aspect auto-reframing.
    add_executable(chrononmotion_eye_trace tests/motion_eye_trace.cpp)
    target_link_libraries(chrononmotion_eye_trace PRIVATE chrononmotion)
    add_test(NAME chrononmotion_eye_trace COMMAND chrononmotion_eye_trace)

    add_executable(chrononmotion_export_camera_v1 tools/export_camera_trajectories.cpp)
    target_link_libraries(chrononmotion_export_camera_v1 PRIVATE chrononmotion)

    # The innovation animations: eye-trace handoff/director, multi-aspect
    # reframe triplet and the dolly-zoom derivative map, self-certifying JSON.
    add_executable(chrononmotion_export_camera_innovations tools/export_camera_innovations.cpp)
    target_link_libraries(chrononmotion_export_camera_innovations PRIVATE chrononmotion)

    # The preview renderer: software-rasterized frame sequences for the four
    # innovation animations, for ffmpeg to turn into shareable MP4s.
    add_executable(chrononmotion_render_innovation_previews tools/render_innovation_previews.cpp)
    target_link_libraries(chrononmotion_render_innovation_previews PRIVATE chrononmotion)

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

    add_executable(chrononmotion_advanced_authoring tests/motion_advanced_authoring.cpp)
    target_link_libraries(chrononmotion_advanced_authoring PRIVATE chrononmotion)
    add_test(NAME chrononmotion_advanced_authoring COMMAND chrononmotion_advanced_authoring)

    add_executable(chrononmotion_offline_golden tests/motion_offline_golden.cpp)
    target_link_libraries(chrononmotion_offline_golden PRIVATE chrononmotion)
    add_test(NAME chrononmotion_offline_golden COMMAND chrononmotion_offline_golden)
    add_executable(chrononmotion_procedural_certification tests/motion_procedural_certification.cpp)
    target_link_libraries(chrononmotion_procedural_certification PRIVATE chrononmotion)
    target_compile_options(chrononmotion_procedural_certification PRIVATE -Wall -Wextra)
    if(CHRONONMOTION_WERROR)
        target_compile_options(chrononmotion_procedural_certification PRIVATE -Werror)
    endif()
    add_test(NAME chrononmotion_procedural_certification COMMAND chrononmotion_procedural_certification)

    add_executable(chrononmotion_geospatial tests/motion_geospatial.cpp)
    target_link_libraries(chrononmotion_geospatial PRIVATE chrononmotion)
    if(CHRONON3D_INCLUDE_DIR)
        target_link_libraries(chrononmotion_geospatial PRIVATE chrononmotion_chronon3d_adapter)
    endif()
    add_test(NAME chrononmotion_geospatial COMMAND chrononmotion_geospatial)

    add_executable(chrononmotion_gltf tests/motion_gltf.cpp)
    target_link_libraries(chrononmotion_gltf PRIVATE chrononmotion)
    add_test(NAME chrononmotion_gltf COMMAND chrononmotion_gltf)

    # Retained upstream threepp math/core/animation assertions, adapted to the
    # local no-dependency harness and pinned provenance in docs.
    add_executable(chrononmotion_upstream_regressions tests/motion_upstream_regressions.cpp)
    target_link_libraries(chrononmotion_upstream_regressions PRIVATE chrononmotion)
    add_test(NAME chrononmotion_upstream_regressions COMMAND chrononmotion_upstream_regressions)

    # Showcase acceptance is independently selectable at the case id/number and
    # emits deterministic motion/plan signatures; no pixel renderer is required.
    add_executable(chrononmotion-showcase tests/motion_showcase.cpp)
    target_link_libraries(chrononmotion-showcase PRIVATE chrononmotion)
    target_compile_features(chrononmotion-showcase PRIVATE cxx_std_20)
    target_compile_options(chrononmotion-showcase PRIVATE -Wall -Wextra)
    target_compile_definitions(chrononmotion-showcase PRIVATE
        CHRONONMOTION_SHOWCASE_GOLDEN="${CMAKE_CURRENT_SOURCE_DIR}/tests/showcase_golden.json")
    if(CHRONONMOTION_WERROR)
        target_compile_options(chrononmotion-showcase PRIVATE -Werror)
    endif()
    if(CHRONON3D_INCLUDE_DIR)
        target_link_libraries(chrononmotion-showcase PRIVATE chrononmotion_chronon3d_adapter)
        target_compile_definitions(chrononmotion-showcase PRIVATE CHRONONMOTION_SHOWCASE_HAS_ADAPTER=1)
    endif()
    add_test(NAME chrononmotion_showcase_all COMMAND chrononmotion-showcase --all)
    add_test(NAME chrononmotion_showcase_catalog COMMAND chrononmotion-showcase --list)
    add_test(NAME chrononmotion_showcase_renderplan COMMAND chrononmotion-showcase --case renderplan_roundtrip)
    if(NOT CHRONON3D_INCLUDE_DIR)
        set_tests_properties(chrononmotion_showcase_renderplan PROPERTIES DISABLED TRUE)
    endif()

    # Architectural guardrail: this tree must not reference content machinery
    # (text shaping, fonts, glyphs, raster, image/video decoding) or acquire a
    # dependency. See tests/check_motion_boundary.cmake.
    add_test(NAME chrononmotion_boundary
            COMMAND ${CMAKE_COMMAND}
            -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_motion_boundary.cmake)

    # Render-sibling scope guard: render/ is a look-dev and proof harness,
    # never a production renderer. See docs/SCOPE_CLOSEOUT.md and
    # tests/check_render_scope.cmake.
    add_test(NAME chrononmotion_render_scope
            COMMAND ${CMAKE_COMMAND}
            -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_render_scope.cmake)

    # Project-file round trip: export -> import -> identical bytes and state.
    add_executable(chrononmotion_scene_import tests/motion_scene_import.cpp)
    target_link_libraries(chrononmotion_scene_import PRIVATE chrononmotion)
    add_test(NAME chrononmotion_scene_import COMMAND chrononmotion_scene_import)

    # Tool-builder interaction: picking, gizmo deltas and the scrub session.
    add_executable(chrononmotion_interaction tests/motion_interaction.cpp)
    target_link_libraries(chrononmotion_interaction PRIVATE chrononmotion)
    add_test(NAME chrononmotion_interaction COMMAND chrononmotion_interaction)

    # FK retargeting between rigs.
    add_executable(chrononmotion_retarget tests/motion_retarget.cpp)
    target_link_libraries(chrononmotion_retarget PRIVATE chrononmotion)
    add_test(NAME chrononmotion_retarget COMMAND chrononmotion_retarget)

    if(CHRONON3D_INCLUDE_DIR)
        add_executable(chrononmotion_renderplan_easing_parity tests/motion_renderplan_easing_parity.cpp)
        target_link_libraries(chrononmotion_renderplan_easing_parity PRIVATE chrononmotion_chronon3d_adapter)
        target_compile_options(chrononmotion_renderplan_easing_parity PRIVATE -Wall -Wextra)
        if(CHRONONMOTION_WERROR)
            target_compile_options(chrononmotion_renderplan_easing_parity PRIVATE -Werror)
        endif()
        add_test(NAME chrononmotion_renderplan_easing_parity COMMAND chrononmotion_renderplan_easing_parity)

        get_filename_component(_chronon3d_root "${CHRONON3D_INCLUDE_DIR}" DIRECTORY)
        add_executable(chrononmotion_projection_parity tests/motion_projection_parity.cpp
            "${_chronon3d_root}/src/scene/camera/camera_projection_source.cpp")
        target_link_libraries(chrononmotion_projection_parity PRIVATE chrononmotion_chronon3d_adapter)
        target_compile_options(chrononmotion_projection_parity PRIVATE -Wall -Wextra)
        if(CHRONONMOTION_WERROR)
            target_compile_options(chrononmotion_projection_parity PRIVATE -Werror)
        endif()
        add_test(NAME chrononmotion_projection_parity COMMAND chrononmotion_projection_parity)

        add_executable(chrononmotion_camera_bake_roundtrip tests/motion_camera_bake_roundtrip.cpp)
        target_link_libraries(chrononmotion_camera_bake_roundtrip PRIVATE chrononmotion_chronon3d_adapter)
        target_compile_options(chrononmotion_camera_bake_roundtrip PRIVATE -Wall -Wextra)
        if(CHRONONMOTION_WERROR)
            target_compile_options(chrononmotion_camera_bake_roundtrip PRIVATE -Werror)
        endif()
        add_test(NAME chrononmotion_camera_bake_roundtrip COMMAND chrononmotion_camera_bake_roundtrip)

        add_test(NAME chrononmotion_renderplan_camera_parity
                COMMAND ${CMAKE_COMMAND}
                -DCHRONON3D_ROOT=${_chronon3d_root}
                -DMOTION_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_renderplan_camera_parity.cmake)
    endif()
endif()

# Offscreen GL render module (separate library; the core stays renderer-free).
if(CHRONONMOTION_BUILD_RENDER)
    add_subdirectory(render)
endif()
