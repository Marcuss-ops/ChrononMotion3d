# ChrononMotion render-sibling scope guard.
#
# The rule this enforces (see docs/SCOPE_CLOSEOUT.md):
#
#   render/ is a look-dev and proof harness, never a production renderer.
#   Production pixels stay in Chronon3D.
#
# Allowed in render/: GL preview and look-dev, MotionSceneBridge, the headless
# canvas, and spike/certification proofs behind CHRONONMOTION_BUILD_SPIKE_PROGRAMS
# (default OFF). GPU certification encoders live inside spike-gated proofs only.
#
# Forbidden: codec SDK linkage in the shipped library/tools, a compositing or
# effects catalog, Chronon3D content-engine includes (chronon3d/render_plan is
# the one approved vocabulary), and new thirdparty/ additions without an ADR.
#
# It reads CODE, not prose: // and /* */ comments are stripped first, so a doc
# comment may name an encoder while the shipped library may not link one.
#
# Run directly:
#   cmake -DSOURCE_DIR=<repo> -P tests/check_render_scope.cmake
#
# A line may opt out with a `render-scope: allowed` comment on the same line,
# and that comment must say why. Using it is a design decision, not a
# convenience.

cmake_minimum_required(VERSION 3.20)

if(NOT DEFINED SOURCE_DIR)
    message(FATAL_ERROR "check_render_scope: pass -DSOURCE_DIR=<repo root>")
endif()

# Codec SDK linkage: forbidden in the shipped library and tools. Spike-gated
# certification proofs under render/spike/ may shell out to a user-installed
# ffmpeg binary or drive NVENC through their own interop headers; that is
# certification evidence, not a production encoder, and it stays behind the
# spike gate checked below.
set(_shipped_dirs "src" "include" "tools")
set(_encoder_tokens "libavcodec" "libavformat" "libavutil" "libswscale"
        "libx264" "libx265" "libvpx" "cuda_nvenc" "nvEncodeAPI")

# Chronon3D content-engine includes: only the render_plan vocabulary is
# approved. Anything reaching into text, fonts, shaping or raster from the
# render sibling means the sibling is becoming a second content engine.
set(_content_tokens "chronon3d/text" "chronon3d/font" "chronon3d/shaping"
        "chronon3d/raster" "harfbuzz" "freetype")

# Vendored third-party set: additions need an ADR (see docs/SCOPE_CLOSEOUT.md).
set(_thirdparty_allowlist "glad" "glfw" "stb" "nlohmann" "meshoptimizer" "quickhull" "libwebp")

set(_violations "")

function(_strip_comments _text_var)
    set(_text "${${_text_var}}")
    set(_guard 0)
    while(_guard LESS 10000)
        math(EXPR _guard "${_guard} + 1")
        string(FIND "${_text}" "/*" _start)
        if(_start EQUAL -1)
            break()
        endif()
        string(FIND "${_text}" "*/" _end)
        string(LENGTH "${_text}" _len)
        if(_end EQUAL -1)
            math(EXPR _tail "${_len} - ${_start}")
        else()
            math(EXPR _tail "${_end} - ${_start} + 2")
        endif()
        string(SUBSTRING "${_text}" ${_start} ${_tail} _span)
        string(REGEX REPLACE "[^\\n]" " " _blanked "${_span}")
        string(SUBSTRING "${_text}" 0 ${_start} _head)
        math(EXPR _after "${_start} + ${_tail}")
        math(EXPR _rest "${_len} - ${_after}")
        string(SUBSTRING "${_text}" ${_after} ${_rest} _tailtext)
        set(_text "${_head}${_blanked}${_tailtext}")
        if(_end EQUAL -1)
            break()
        endif()
    endwhile()
    set(${_text_var} "${_text}" PARENT_SCOPE)
endfunction()

function(_scan_file _file _tokens_var)
    file(READ "${_file}" _text)
    _strip_comments(_text)
    get_filename_component(_ext "${_file}" EXT)
    if(_ext STREQUAL ".txt")
        set(_is_cmake TRUE)
    else()
        set(_is_cmake FALSE)
    endif()
    string(REPLACE "\r\n" "\n" _text "${_text}")
    string(REPLACE ";" "\\;" _text "${_text}")
    string(REPLACE "\n" ";" _lines "${_text}")
    set(_lineno 0)
    foreach(_line IN LISTS _lines)
        math(EXPR _lineno "${_lineno} + 1")
        set(_code "${_line}")
        if(_is_cmake)
            string(FIND "${_code}" "#" _hash)
            if(NOT _hash EQUAL -1)
                string(SUBSTRING "${_code}" 0 ${_hash} _code)
            endif()
        else()
            string(FIND "${_code}" "//" _slash)
            if(NOT _slash EQUAL -1)
                string(SUBSTRING "${_code}" 0 ${_slash} _code)
            endif()
        endif()
        string(STRIP "${_code}" _code)
        if(_code STREQUAL "")
            continue()
        endif()
        string(TOLOWER "${_code}" _lower)
        string(FIND "${_lower}" "render-scope: allowed" _allowed)
        if(NOT _allowed EQUAL -1)
            continue()
        endif()
        foreach(_token IN LISTS ${_tokens_var})
            string(FIND "${_lower}" "${_token}" _found)
            if(NOT _found EQUAL -1)
                file(RELATIVE_PATH _rel "${SOURCE_DIR}" "${_file}")
                list(APPEND _violations "  ${_rel}:${_lineno}: '${_token}'")
            endif()
        endforeach()
    endforeach()
    set(_violations "${_violations}" PARENT_SCOPE)
endfunction()

foreach(_dir IN LISTS _shipped_dirs)
    file(GLOB_RECURSE _shipped
            "${SOURCE_DIR}/render/${_dir}/*.cpp"
            "${SOURCE_DIR}/render/${_dir}/*.hpp"
            "${SOURCE_DIR}/render/${_dir}/*.h"
            "${SOURCE_DIR}/render/${_dir}/*.txt")
    foreach(_file IN LISTS _shipped)
        _scan_file("${_file}" _encoder_tokens)
        _scan_file("${_file}" _content_tokens)
    endforeach()
endforeach()

# The whole render tree (spikes included) must never reach into the
# content engine; only the render_plan vocabulary is approved.
file(GLOB_RECURSE _render_all
        "${SOURCE_DIR}/render/src/*.cpp"
        "${SOURCE_DIR}/render/src/*.hpp"
        "${SOURCE_DIR}/render/include/*.hpp"
        "${SOURCE_DIR}/render/tools/*.cpp"
        "${SOURCE_DIR}/render/tools/*.hpp"
        "${SOURCE_DIR}/render/spike/*.cpp"
        "${SOURCE_DIR}/render/spike/*.hpp")
foreach(_file IN LISTS _render_all)
    _scan_file("${_file}" _content_tokens)
endforeach()

# Third-party allowlist.
file(GLOB _thirdparty_dirs RELATIVE "${SOURCE_DIR}/render/thirdparty" "${SOURCE_DIR}/render/thirdparty/*")
foreach(_entry IN LISTS _thirdparty_dirs)
    if(IS_DIRECTORY "${SOURCE_DIR}/render/thirdparty/${_entry}")
        list(FIND _thirdparty_allowlist "${_entry}" _known)
        if(_known EQUAL -1)
            list(APPEND _violations "  render/thirdparty/${_entry}: not in the allowlist (needs an ADR per docs/SCOPE_CLOSEOUT.md)")
        endif()
    endif()
endforeach()

# The spike gate stays opt-in: spike/demo programs must default OFF.
# (Matched textually: the option doc string itself contains parentheses, so a
# single regex up to the first ')' would stop inside the documentation.)
file(READ "${SOURCE_DIR}/render/CMakeLists.txt" _render_cmake)
string(FIND "${_render_cmake}" "option(CHRONONMOTION_BUILD_SPIKE_PROGRAMS" _opt_pos)
if(_opt_pos EQUAL -1)
    list(APPEND _violations "  render/CMakeLists.txt: CHRONONMOTION_BUILD_SPIKE_PROGRAMS option missing (spike gate removed?)")
else()
    string(SUBSTRING "${_render_cmake}" ${_opt_pos} 600 _opt_block)
    if(NOT _opt_block MATCHES "\n[ \t]*OFF[ \t]*\r?\n?[ \t]*\\)")
        list(APPEND _violations "  render/CMakeLists.txt: spike programs must default OFF")
    endif()
endif()

if(_violations)
    list(LENGTH _violations _count)
    string(REPLACE ";" "\n" _pretty "${_violations}")
    message(FATAL_ERROR
            "check_render_scope: ${_count} render-scope violation(s).\n"
            "render/ is a look-dev and proof harness, never a production renderer.\n"
            "See docs/SCOPE_CLOSEOUT.md. Add a 'render-scope: allowed' comment\n"
            "explaining why, or move the capability to its owning side.\n\n"
            "${_pretty}")
endif()

message(STATUS "check_render_scope: render sibling scope intact")
