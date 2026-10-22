# ChrononMotion boundary guardrail.
#
# The rule this enforces:
#
#   Chronon owns content — text assets, shaping, layout, glyphs, raster, image
#                         and video decoding.
#   ChrononMotion owns space and time — layers, transforms, tracks, cameras, the
#                         frame→value mapping.
#
# A motion engine that grows a font stack, a text shaper or an image decoder has
# started to become a second content engine, and that is paid for later as two
# layout engines, two definitions of "bounds" and two sets of text bugs. This
# test fails the build the moment this tree *references* that machinery or
# acquires a dependency at configure time — the mechanical form of "if you need
# HarfBuzz here, the boundary is wrong".
#
# It reads CODE, not prose: comments are stripped first (a doc comment may
# mention a font shaper; an include may not). Line numbers refer to the original
# file, because comments are blanked in place rather than removed.
#
# Run directly:
#   cmake -DSOURCE_DIR=<repo> -P tests/check_motion_boundary.cmake
#
# A line may opt out with a `boundary-guardrail: allowed` comment on the same
# line, and that comment must say why. Using it is a design decision, not a
# convenience.

cmake_minimum_required(VERSION 3.20)

if(NOT DEFINED SOURCE_DIR)
    message(FATAL_ERROR "check_motion_boundary: pass -DSOURCE_DIR=<repo root>")
endif()

set(_guardrails_text
        "harfbuzz" "hb.h" "hb_shape" "hb-shape" "hb_buffer"
        "freetype" "ft2build" "fribidi" "ubidi" "unicode/bidi" "libicu"
        "mtsdf" "glyph" "typeface" "fontface" "font_face" "font_cache" "fontcache"
        "glyph_atlas" "glyphatlas" "shaping" "text_layout" "textlayout"
        "line_break" "linebreak" "kerning" "bidi"
        ".ttf" ".otf" ".woff" ".typeface")

set(_guardrails_media
        "stb_image" "stb_truetype" "libpng" "png.h" "jpeglib" "jpeg.h"
        "libwebp" "webp/decode" "libavcodec" "avcodec" "av_frame" "libavformat"
        "ffmpeg" "nvdec" "nvenc" "exrloader" "ddsloader")

set(_guardrails_raster
        "blend2d" "blendcontext" "blfont" "skia" "cairo"
        "vulkan" "opengl" "glsl" "shader" "swapchain" "vkcmd" "glfw")

set(_guardrails_deps
        "find_package(" "fetchcontent" "pkg_check_modules" "vcpkg")

set(_guardrails ${_guardrails_text} ${_guardrails_media} ${_guardrails_raster} ${_guardrails_deps})

file(GLOB_RECURSE _sources
        "${SOURCE_DIR}/include/*.hpp"
        "${SOURCE_DIR}/include/*.h"
        "${SOURCE_DIR}/src/*.cpp"
        "${SOURCE_DIR}/src/*.hpp"
        "${SOURCE_DIR}/tests/*.cpp"
        "${SOURCE_DIR}/tests/*.cmake"
        "${SOURCE_DIR}/*.txt")
list(FILTER _sources EXCLUDE REGEX "${SOURCE_DIR}/build/.*")
# Local build trees (build-*/) are ignored build output, never source.
list(FILTER _sources EXCLUDE REGEX "${SOURCE_DIR}/build-[^/]*/.*")
# The offscreen GL render sibling (render/) is deliberately out of scope: it is
# a separate static library whose whole job is pixels (GL renderer, image
# decoders, windowing for headless contexts). The guardrail pins the CORE
# motion library (include/, src/, tests/); render/ is where content-adjacent
# machinery is allowed to live, fed only by Motion-baked data.
list(FILTER _sources EXCLUDE REGEX "${SOURCE_DIR}/render/.*")

# These scripts name every forbidden token they look for, so they would
# trivially fail on themselves.
list(FILTER _sources EXCLUDE REGEX "check_motion_boundary\\.cmake$")
list(FILTER _sources EXCLUDE REGEX "check_render_scope\\.cmake$")

if(NOT _sources)
    message(FATAL_ERROR "check_motion_boundary: no sources found under ${SOURCE_DIR}")
endif()

set(_violations "")

foreach(_file IN LISTS _sources)
    file(READ "${_file}" _text)

    # Blank out /* ... */ without touching newlines, so line numbers stay true.
    # (CMake's regex has no lazy quantifier, so this walks the markers itself.)
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
        string(REGEX REPLACE "[^\n]" " " _blanked "${_span}")
        string(SUBSTRING "${_text}" 0 ${_start} _head)
        math(EXPR _after "${_start} + ${_tail}")
        math(EXPR _rest "${_len} - ${_after}")
        string(SUBSTRING "${_text}" ${_after} ${_rest} _tailtext)
        set(_text "${_head}${_blanked}${_tailtext}")
        if(_end EQUAL -1)
            break()
        endif()
    endwhile()

    get_filename_component(_name "${_file}" NAME)
    if(_name STREQUAL "CMakeLists.txt")
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
        # Preserve the opt-out marker before stripping CMake's '#' comments.
        # For CMakeLists.txt, the marker is necessarily written as a comment.
        string(TOLOWER "${_line}" _raw_lower)
        string(FIND "${_raw_lower}" "boundary-guardrail: allowed" _raw_allowed)
        if(NOT _raw_allowed EQUAL -1)
            continue()
        endif()
        set(_code "${_line}")

        # An explicit, reasoned opt-out lives in a comment on the same line, so
        # it has to be recognised *before* comments are stripped from the code
        # under test. Otherwise the remedy named in this check's own failure
        # message ("add a 'boundary-guardrail: allowed' comment") could never
        # match, because the comment is already gone by then.
        string(TOLOWER "${_line}" _raw_lower)
        string(FIND "${_raw_lower}" "boundary-guardrail: allowed" _allowed)
        if(NOT _allowed EQUAL -1)
            continue()
        endif()

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

        foreach(_token IN LISTS _guardrails)
            string(FIND "${_lower}" "${_token}" _found)
            if(NOT _found EQUAL -1)
                file(RELATIVE_PATH _rel "${SOURCE_DIR}" "${_file}")
                list(APPEND _violations "  ${_rel}:${_lineno}: '${_token}'")
            endif()
        endforeach()
    endforeach()
endforeach()

# The library itself links nothing: a dependency would have to appear here.
file(READ "${SOURCE_DIR}/CMakeLists.txt" _cmake)
string(REGEX MATCH "target_link_libraries\\(chrononmotion[ )]" _lib_link "${_cmake}")
if(_lib_link)
    list(APPEND _violations "  CMakeLists.txt: the library target links something (${_lib_link})")
endif()

if(_violations)
    list(LENGTH _violations _count)
    string(REPLACE ";" "\n" _pretty "${_violations}")
    message(FATAL_ERROR
            "check_motion_boundary: ${_count} content-machinery reference(s) in a motion-side tree.\n"
            "ChrononMotion owns space and time; Chronon owns content (text, fonts, shaping,\n"
            "glyphs, raster, image/video decoding). Either move this to the content side, or\n"
            "add a 'boundary-guardrail: allowed' comment explaining why this tree needs it.\n\n"
            "${_pretty}")
endif()

list(LENGTH _sources _scanned)
message(STATUS "check_motion_boundary: ${_scanned} files scanned, boundary intact")
