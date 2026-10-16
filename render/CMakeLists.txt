# ChrononMotion offscreen GL render module.
#
# What this is: the threepp OpenGL renderer + the minimum scene/material/
# loader/texture/canvas support it needs, at the pinned upstream revision
# (see README.md), built as a SEPARATE static library `chrononmotion_render`.
# The core `chrononmotion` library stays dependency-free and renderer-free;
# this module is its pixel-producing sibling, fed by Motion-baked scenes.
#
# Deliberately NOT here: the Vulkan renderer, post-processing, audio,
# controls/input, physics/sensors, examples, python, the editor. Those stay
# deleted — Chronon3D already owns compositing, effects and encoding.
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

# --- spikes: headless proofs (cube, glb, bridge, image_mesh) -> PPM/MP4 ---
add_executable(chrononmotion_render_spike spike/cube.cpp)
target_link_libraries(chrononmotion_render_spike PRIVATE chrononmotion_render)
add_executable(chrononmotion_render_glb spike/glb.cpp)
target_include_directories(chrononmotion_render_glb PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/stb")
target_link_libraries(chrononmotion_render_glb PRIVATE chrononmotion_render)
add_executable(chrononmotion_render_bridge spike/bridge.cpp)
target_link_libraries(chrononmotion_render_bridge PRIVATE chrononmotion_render)
add_executable(chrononmotion_render_image_mesh spike/image_mesh.cpp)
target_link_libraries(chrononmotion_render_image_mesh PRIVATE chrononmotion_render)

