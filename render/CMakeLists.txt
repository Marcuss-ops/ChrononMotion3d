# ChrononMotion offscreen GL render module.
#
# What this is: the threepp OpenGL renderer + the minimum scene/material/
# loader/texture/canvas support it needs, at the pinned upstream revision
# (see README.md), built as a SEPARATE static library `chrononmotion_render`.
# The core `chrononmotion` library stays dependency-free and renderer-free;
# this module is its pixel-producing sibling, fed by Motion-baked scenes.
#
# Deliberately NOT here: the Vulkan renderer, audio, interactive controls
# (the passive PeripheralsEventSource that Canvas subclasses IS here),
# physics/sensors, examples, python, the editor. Those stay
# deleted — Chronon3D already owns compositing, effects and encoding.
# NOTE: postprocessing (EffectComposer / UnrealBloomPass / BokehPass) lives
# here: the DoF and scene certification programs consume it.
#
# NOTE: this file intentionally lives outside the boundary-guardrail scan
# (tests/check_motion_boundary.cmake only covers include/, src/, tests/ and
# the root CMakeLists). The guardrail still pins the core library clean.

cmake_minimum_required(VERSION 3.20)

# C is needed for glad + libwebp; the top project declares CXX only.
enable_language(C)

# --- vendored GLFW (display-free headless via Null platform; no system glfw) ---
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "" FORCE)
add_subdirectory(thirdparty/glfw)

# --- vendored libwebp decoder subset (upstream's own webpdecoder manifest) ---
set(libwebpSources
        "thirdparty/libwebp/src/dec/alpha_dec.c"
        "thirdparty/libwebp/src/dec/buffer_dec.c"
        "thirdparty/libwebp/src/dec/frame_dec.c"
        "thirdparty/libwebp/src/dec/idec_dec.c"
        "thirdparty/libwebp/src/dec/io_dec.c"
        "thirdparty/libwebp/src/dec/quant_dec.c"
        "thirdparty/libwebp/src/dec/tree_dec.c"
        "thirdparty/libwebp/src/dec/vp8_dec.c"
        "thirdparty/libwebp/src/dec/vp8l_dec.c"
        "thirdparty/libwebp/src/dec/webp_dec.c"
        "thirdparty/libwebp/src/dsp/alpha_processing.c"
        "thirdparty/libwebp/src/dsp/alpha_processing_mips_dsp_r2.c"
        "thirdparty/libwebp/src/dsp/alpha_processing_neon.c"
        "thirdparty/libwebp/src/dsp/alpha_processing_sse2.c"
        "thirdparty/libwebp/src/dsp/alpha_processing_sse41.c"
        "thirdparty/libwebp/src/dsp/cpu.c"
        "thirdparty/libwebp/src/dsp/dec.c"
        "thirdparty/libwebp/src/dsp/dec_clip_tables.c"
        "thirdparty/libwebp/src/dsp/dec_mips32.c"
        "thirdparty/libwebp/src/dsp/dec_mips_dsp_r2.c"
        "thirdparty/libwebp/src/dsp/dec_msa.c"
        "thirdparty/libwebp/src/dsp/dec_neon.c"
        "thirdparty/libwebp/src/dsp/dec_sse2.c"
        "thirdparty/libwebp/src/dsp/dec_sse41.c"
        "thirdparty/libwebp/src/dsp/filters.c"
        "thirdparty/libwebp/src/dsp/filters_mips_dsp_r2.c"
        "thirdparty/libwebp/src/dsp/filters_msa.c"
        "thirdparty/libwebp/src/dsp/filters_neon.c"
        "thirdparty/libwebp/src/dsp/filters_sse2.c"
        "thirdparty/libwebp/src/dsp/lossless.c"
        "thirdparty/libwebp/src/dsp/lossless_avx2.c"
        "thirdparty/libwebp/src/dsp/lossless_mips_dsp_r2.c"
        "thirdparty/libwebp/src/dsp/lossless_msa.c"
        "thirdparty/libwebp/src/dsp/lossless_neon.c"
        "thirdparty/libwebp/src/dsp/lossless_sse2.c"
        "thirdparty/libwebp/src/dsp/lossless_sse41.c"
        "thirdparty/libwebp/src/dsp/rescaler.c"
        "thirdparty/libwebp/src/dsp/rescaler_mips32.c"
        "thirdparty/libwebp/src/dsp/rescaler_mips_dsp_r2.c"
        "thirdparty/libwebp/src/dsp/rescaler_msa.c"
        "thirdparty/libwebp/src/dsp/rescaler_neon.c"
        "thirdparty/libwebp/src/dsp/rescaler_sse2.c"
        "thirdparty/libwebp/src/dsp/upsampling.c"
        "thirdparty/libwebp/src/dsp/upsampling_mips_dsp_r2.c"
        "thirdparty/libwebp/src/dsp/upsampling_msa.c"
        "thirdparty/libwebp/src/dsp/upsampling_neon.c"
        "thirdparty/libwebp/src/dsp/upsampling_sse2.c"
        "thirdparty/libwebp/src/dsp/upsampling_sse41.c"
        "thirdparty/libwebp/src/dsp/yuv.c"
        "thirdparty/libwebp/src/dsp/yuv_mips32.c"
        "thirdparty/libwebp/src/dsp/yuv_mips_dsp_r2.c"
        "thirdparty/libwebp/src/dsp/yuv_neon.c"
        "thirdparty/libwebp/src/dsp/yuv_sse2.c"
        "thirdparty/libwebp/src/dsp/yuv_sse41.c"
        "thirdparty/libwebp/src/utils/bit_reader_utils.c"
        "thirdparty/libwebp/src/utils/color_cache_utils.c"
        "thirdparty/libwebp/src/utils/filters_utils.c"
        "thirdparty/libwebp/src/utils/huffman_utils.c"
        "thirdparty/libwebp/src/utils/palette.c"
        "thirdparty/libwebp/src/utils/quant_levels_dec_utils.c"
        "thirdparty/libwebp/src/utils/random_utils.c"
        "thirdparty/libwebp/src/utils/rescaler_utils.c"
        "thirdparty/libwebp/src/utils/thread_utils.c"
        "thirdparty/libwebp/src/utils/utils.c"
)
if (CMAKE_C_COMPILER_ID STREQUAL "GNU" OR CMAKE_C_COMPILER_ID STREQUAL "Clang")
    set_source_files_properties(${libwebpSources} PROPERTIES COMPILE_OPTIONS "-w")
endif ()

# --- embedded GL shaders (same mechanism as upstream cmake/embed.cmake,
# --- trimmed to the shader half: no favicon, no fonts) ---
set(RENDER_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${RENDER_GENERATED_DIR}")

set(RENDER_SHADER_INCLUDES "")
set(RENDER_SHADERCHUNK_CODE "")
set(RENDER_SHADERLIB_CODE "")
set(RENDER_GENERATED_SHADER_HEADERS "")

function(render_embed_glsl_dir subdir ns codeVar)
    file(GLOB shaderFiles CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/shaders/${subdir}/*.glsl")
    set(_includes "${RENDER_SHADER_INCLUDES}")
    set(_code "${${codeVar}}")
    set(_headers "${RENDER_GENERATED_SHADER_HEADERS}")
    foreach (shaderFile IN LISTS shaderFiles)
        get_filename_component(fileName "${shaderFile}" NAME_WLE)
        set(header_file "${RENDER_GENERATED_DIR}/threepp/renderers/shaders/${subdir}/${fileName}.hpp")
        add_custom_command(
                OUTPUT "${header_file}"
                COMMAND "${CMAKE_COMMAND}"
                        "-DIN=${shaderFile}"
                        "-DOUT=${header_file}"
                        "-DNS=${ns}"
                        "-DNAME=${fileName}"
                        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedGlsl.cmake"
                DEPENDS "${shaderFile}" "${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedGlsl.cmake"
                COMMENT "Embedding GL shader ${subdir}/${fileName}.glsl"
                VERBATIM)
        list(APPEND _headers "${header_file}")
        set(_includes "${_includes}\n#include \"${subdir}/${fileName}.hpp\"")
        set(_code "${_code}\tdata_[\"${fileName}\"] = ${ns}::${fileName};\n")
    endforeach ()
    set(RENDER_SHADER_INCLUDES "${_includes}" PARENT_SCOPE)
    set(${codeVar} "${_code}" PARENT_SCOPE)
    set(RENDER_GENERATED_SHADER_HEADERS "${_headers}" PARENT_SCOPE)
endfunction()

render_embed_glsl_dir(ShaderChunk shaderchunk RENDER_SHADERCHUNK_CODE)
render_embed_glsl_dir(ShaderLib shaderlib RENDER_SHADERLIB_CODE)

set(THREEPP_SHADER_INCLUDES "${RENDER_SHADER_INCLUDES}")
set(THREEPP_SHADERCHUNK_CODE "${RENDER_SHADERCHUNK_CODE}")
set(THREEPP_SHADERLIB_CODE "${RENDER_SHADERLIB_CODE}")
configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/src/threepp/renderers/shaders/ShaderChunk.cpp.in"
        "${RENDER_GENERATED_DIR}/threepp/renderers/shaders/ShaderChunk.cpp"
        @ONLY
)

# --- the render library ---
file(GLOB_RECURSE renderSources CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/src/threepp/*.cpp"
)

list(LENGTH renderSources renderSourcesCount)
message(STATUS "render: ${renderSourcesCount} threepp sources")
add_library(chrononmotion_render STATIC
        ${renderSources}
        "${RENDER_GENERATED_DIR}/threepp/renderers/shaders/ShaderChunk.cpp"
        ${RENDER_GENERATED_SHADER_HEADERS}
        thirdparty/glad/glad.c
        thirdparty/meshoptimizer/allocator.cpp
        thirdparty/meshoptimizer/indexcodec.cpp
        thirdparty/meshoptimizer/indexgenerator.cpp
        thirdparty/meshoptimizer/simplifier.cpp
        thirdparty/meshoptimizer/vertexcodec.cpp
        thirdparty/meshoptimizer/vertexfilter.cpp
        ${libwebpSources}
)

option(CHRONONMOTION_BUILD_SPIKE_PROGRAMS
        "Build render/spike demo + certification programs (default OFF: lean core+renderer build; enable for certification runs)"
        OFF)

# --- shared CUDA/FFmpeg interop discovery + requirements (used by the
# --- certification programs below; single INTERFACE target replaces the
# --- previously repeated 4-line block per executable) ---
# Keep CUDA/FFmpeg interop optional for build hosts without the NVIDIA SDK,
# but never substitute a CPU encoder in the certification runner. Such hosts
# can build ChrononMotion3D; T06/T14 then reports that the GPU route is absent.
find_path(CHRONON_CUDA_INCLUDE_DIR cuda_gl_interop.h)
find_library(CHRONON_CUDART_LIBRARY NAMES cudart)
find_library(CHRONON_CUDA_DRIVER_LIBRARY NAMES cuda)
find_package(PkgConfig QUIET)
if (PkgConfig_FOUND)
    pkg_check_modules(CHRONON_FFMPEG QUIET IMPORTED_TARGET libavcodec libavformat libavutil)
endif ()
if (CHRONON_CUDA_INCLUDE_DIR AND CHRONON_CUDART_LIBRARY AND CHRONON_CUDA_DRIVER_LIBRARY AND TARGET PkgConfig::CHRONON_FFMPEG)
    set(CHRONON_GL_CUDA_NVENC_AVAILABLE TRUE)
    message(STATUS "threepp certification: enabling GL PBO -> CUDA -> NVENC path")
else ()
    message(STATUS "threepp certification: CUDA/FFmpeg interop unavailable; GPU certification runner will refuse to render")
endif ()
add_library(chrononmotion_gl_cuda_nvenc INTERFACE)
target_link_libraries(chrononmotion_gl_cuda_nvenc INTERFACE
        ${CHRONON_CUDART_LIBRARY} ${CHRONON_CUDA_DRIVER_LIBRARY})
if (TARGET PkgConfig::CHRONON_FFMPEG)
    target_link_libraries(chrononmotion_gl_cuda_nvenc INTERFACE PkgConfig::CHRONON_FFMPEG)
endif ()
if (CHRONON_GL_CUDA_NVENC_AVAILABLE)
    target_include_directories(chrononmotion_gl_cuda_nvenc INTERFACE "${CHRONON_CUDA_INCLUDE_DIR}")
    target_compile_definitions(chrononmotion_gl_cuda_nvenc INTERFACE CHRONONMOTION_HAS_GL_CUDA_NVENC=1)
endif ()

target_include_directories(chrononmotion_render PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${RENDER_GENERATED_DIR}"
)
target_include_directories(chrononmotion_render PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/glad"
        "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/stb"
        "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/nlohmann"
        "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/meshoptimizer"
        "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/quickhull"
        "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/glfw/include"
)
# libwebp internal includes are rooted at its package dir ("src/dec/..."),
# while the public header is "webp/decode.h" under src/.
target_include_directories(chrononmotion_render BEFORE PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/libwebp"
        "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/libwebp/src"
)
target_link_libraries(chrononmotion_render PUBLIC glfw chrononmotion)
target_compile_features(chrononmotion_render PUBLIC cxx_std_20)
find_package(PkgConfig QUIET)
if (PkgConfig_FOUND)
    pkg_check_modules(CHRONON_EGL QUIET IMPORTED_TARGET egl)
endif ()
if (TARGET PkgConfig::CHRONON_EGL)
    target_compile_definitions(chrononmotion_render PRIVATE CHRONONMOTION_EGL_HEADLESS=1)
    target_link_libraries(chrononmotion_render PUBLIC PkgConfig::CHRONON_EGL)
    message(STATUS "threepp render: EGL device/pbuffer headless OpenGL enabled")
endif ()

# --- spikes: headless proofs (cube, glb, bridge, image_mesh) -> PPM/MP4 ---
# Gated: OFF by default so `cmake --build` stops compiling ~25 demo binaries.
# Certification runs build with -DCHRONONMOTION_BUILD_SPIKE_PROGRAMS=ON.
if (NOT CHRONONMOTION_BUILD_SPIKE_PROGRAMS)
    message(STATUS "threepp render: spike/demo programs disabled (CHRONONMOTION_BUILD_SPIKE_PROGRAMS=OFF)")
    return()
endif ()
add_executable(chrononmotion_render_spike spike/cube.cpp)
target_link_libraries(chrononmotion_render_spike PRIVATE chrononmotion_render)
add_executable(chrononmotion_render_glb spike/glb.cpp)
target_include_directories(chrononmotion_render_glb PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/stb")
target_link_libraries(chrononmotion_render_glb PRIVATE chrononmotion_render)
add_executable(chrononmotion_render_glb_certification spike/glb_certification.cpp)
target_include_directories(chrononmotion_render_glb_certification PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/stb"
        "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/glad")
target_link_libraries(chrononmotion_render_glb_certification PRIVATE chrononmotion_render chrononmotion_gl_cuda_nvenc)
add_executable(chrononmotion_render_camera_certification spike/camera_certification.cpp)
target_include_directories(chrononmotion_render_camera_certification PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/stb"
        "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/glad")
target_link_libraries(chrononmotion_render_camera_certification PRIVATE chrononmotion_render chrononmotion_gl_cuda_nvenc)

add_executable(chrononmotion_render_alpha_handoff spike/alpha_handoff.cpp)
target_include_directories(chrononmotion_render_alpha_handoff PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/stb"
        "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/glad")
target_link_libraries(chrononmotion_render_alpha_handoff PRIVATE chrononmotion_render)
add_executable(chrononmotion_render_bridge spike/bridge.cpp)
target_link_libraries(chrononmotion_render_bridge PRIVATE chrononmotion_render)
add_executable(chrononmotion_render_clip_sequence_demo spike/clip_sequence_demo.cpp)
target_include_directories(chrononmotion_render_clip_sequence_demo PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/glad")
target_link_libraries(chrononmotion_render_clip_sequence_demo PRIVATE chrononmotion_render)

add_executable(chrononmotion_render_clip_sequence_weights tests/clip_sequence_weights.cpp)
target_link_libraries(chrononmotion_render_clip_sequence_weights PRIVATE chrononmotion_render)
add_test(NAME chrononmotion_render_clip_sequence_weights COMMAND chrononmotion_render_clip_sequence_weights)
add_executable(chrononmotion_render_image_mesh spike/image_mesh.cpp)
target_link_libraries(chrononmotion_render_image_mesh PRIVATE chrononmotion_render)
add_executable(chrononmotion_render_animation_certification spike/animation_certification.cpp)
target_include_directories(chrononmotion_render_animation_certification PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/stb"
        "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/glad")
target_link_libraries(chrononmotion_render_animation_certification PRIVATE chrononmotion_render chrononmotion_gl_cuda_nvenc)
add_executable(chrononmotion_render_humanoid_shadow_certification spike/humanoid_shadow_certification.cpp)
target_include_directories(chrononmotion_render_humanoid_shadow_certification PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/stb"
        "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/glad")
target_link_libraries(chrononmotion_render_humanoid_shadow_certification PRIVATE chrononmotion_render chrononmotion_gl_cuda_nvenc)
add_executable(chrononmotion_render_scene_lowpoly_crowd spike/scene01_lowpoly_crowd.cpp)
target_include_directories(chrononmotion_render_scene_lowpoly_crowd PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/stb"
        "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/glad")
target_link_libraries(chrononmotion_render_scene_lowpoly_crowd PRIVATE chrononmotion_render chrononmotion_gl_cuda_nvenc)
add_executable(chrononmotion_render_scene_hoodie_portrait spike/scene02_hoodie_portrait.cpp)
target_include_directories(chrononmotion_render_scene_hoodie_portrait PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/stb"
        "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/glad")
target_link_libraries(chrononmotion_render_scene_hoodie_portrait PRIVATE chrononmotion_render chrononmotion_gl_cuda_nvenc)
add_executable(chrononmotion_render_pbr_certification spike/pbr_certification.cpp)
target_include_directories(chrononmotion_render_pbr_certification PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/stb"
        "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/glad")
target_link_libraries(chrononmotion_render_pbr_certification PRIVATE chrononmotion_render chrononmotion_gl_cuda_nvenc)
add_executable(chrononmotion_render_postprocessing spike/postprocessing.cpp)
target_include_directories(chrononmotion_render_postprocessing PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/stb"
        "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/glad")
target_link_libraries(chrononmotion_render_postprocessing PRIVATE chrononmotion_render chrononmotion_gl_cuda_nvenc)
add_executable(chrononmotion_render_dof_certification spike/dof_certification.cpp)
target_include_directories(chrononmotion_render_dof_certification PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/stb"
        "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/glad")
target_link_libraries(chrononmotion_render_dof_certification PRIVATE chrononmotion_render chrononmotion_gl_cuda_nvenc)
add_executable(chrononmotion_render_shadow_certification spike/shadow_certification.cpp)
target_include_directories(chrononmotion_render_shadow_certification PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/stb"
        "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/glad")
target_link_libraries(chrononmotion_render_shadow_certification PRIVATE chrononmotion_render chrononmotion_gl_cuda_nvenc)
add_executable(chrononmotion_render_portrait_mesh spike/portrait_mesh.cpp)
target_link_libraries(chrononmotion_render_portrait_mesh PRIVATE chrononmotion_render)
add_executable(chrononmotion_render_mrbeast_rig spike/mrbeast_rig.cpp)
target_link_libraries(chrononmotion_render_mrbeast_rig PRIVATE chrononmotion_render)
add_executable(chrononmotion_render_mrbeast_particles spike/mrbeast_particles.cpp)
target_link_libraries(chrononmotion_render_mrbeast_particles PRIVATE chrononmotion_render)
target_compile_definitions(chrononmotion_render_mrbeast_particles PRIVATE
    CHRONONMOTION_SPIKE_ASSET_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/spike/assets\")
add_executable(chrononmotion_render_creator_sway spike/creator_sway_rig.cpp)
target_link_libraries(chrononmotion_render_creator_sway PRIVATE chrononmotion_render)
add_executable(chrononmotion_render_creator_puppet spike/creator_puppet_advanced.cpp)
target_link_libraries(chrononmotion_render_creator_puppet PRIVATE chrononmotion_render)
add_executable(chrononmotion_render_creator_anatomical spike/creator_anatomical_rig.cpp)
target_link_libraries(chrononmotion_render_creator_anatomical PRIVATE chrononmotion_render)
add_executable(chrononmotion_render_creator_v2 spike/creator_character_v2.cpp)
target_link_libraries(chrononmotion_render_creator_v2 PRIVATE chrononmotion_render)
add_executable(chrononmotion_render_creator_v2_1 spike/creator_character_v2_1.cpp)
target_link_libraries(chrononmotion_render_creator_v2_1 PRIVATE chrononmotion_render)
add_executable(chrononmotion_render_documentary_presets spike/creator_documentary_presets.cpp)
target_link_libraries(chrononmotion_render_documentary_presets PRIVATE chrononmotion_render)

