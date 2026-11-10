cmake_minimum_required(VERSION 3.20)

find_package(Threads REQUIRED) # boundary-guardrail: allowed — the preview application layer owns worker threads; the core library still links nothing.

add_library(chrononmotion_preview STATIC
        src/chrononmotion/preview/AssetServer.cpp
        src/chrononmotion/preview/DebugDraw.cpp
        src/chrononmotion/preview/Diagnostics.cpp
        src/chrononmotion/preview/PreviewHost.cpp)

target_link_libraries(chrononmotion_preview PUBLIC chrononmotion Threads::Threads)
target_include_directories(chrononmotion_preview PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>)
target_compile_features(chrononmotion_preview PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(chrononmotion_preview PRIVATE /W4)
    if(CHRONONMOTION_WERROR)
        target_compile_options(chrononmotion_preview PRIVATE /WX)
    endif()
else()
    target_compile_options(chrononmotion_preview PRIVATE -Wall -Wextra)
    if(CHRONONMOTION_WERROR)
        target_compile_options(chrononmotion_preview PRIVATE -Werror)
    endif()
endif()

if(CHRONONMOTION_BUILD_TESTS)
    add_executable(chrononmotion_preview_tests tests/preview_tests.cpp)
    target_link_libraries(chrononmotion_preview_tests PRIVATE chrononmotion_preview)
    target_include_directories(chrononmotion_preview_tests PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/../tests)
    target_compile_features(chrononmotion_preview_tests PRIVATE cxx_std_20)
    target_compile_options(chrononmotion_preview_tests PRIVATE -Wall -Wextra)
    if(CHRONONMOTION_WERROR)
        target_compile_options(chrononmotion_preview_tests PRIVATE -Werror)
    endif()
    add_test(NAME chrononmotion_preview_tests COMMAND chrononmotion_preview_tests)
    set_tests_properties(chrononmotion_preview_tests PROPERTIES TIMEOUT 180)
endif()
