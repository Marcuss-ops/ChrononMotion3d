#pragma once

#include <cstdlib>
#include <stdexcept>
#include <string>
#include <string_view>

namespace chrononmotion::certification {

// Certification renders must run on the physical NVIDIA adapter paired with
// NVENC. A software GL implementation or an implicitly selected CPU encoder
// is a hard error, never a usable fallback.
inline void require_nvidia_renderer(std::string_view renderer) {
    if (renderer.find("NVIDIA") == std::string_view::npos) {
        throw std::runtime_error("certification requires NVIDIA hardware GL; selected: " +
                                 std::string(renderer));
    }
}

inline std::string require_nvenc_encoder() {
    const char* requested = std::getenv("CHRONON_CERT_ENCODER");
    if (requested && std::string_view(requested) != "h264_nvenc") {
        throw std::runtime_error("certification requires CHRONON_CERT_ENCODER=h264_nvenc; software encoders are disabled");
    }
    return "h264_nvenc";
}

inline void prepare_nvidia_egl_headless() {
    const char* x11 = std::getenv("DISPLAY");
    const char* wayland = std::getenv("WAYLAND_DISPLAY");
    const char* platform = std::getenv("THREEPP_GLFW_PLATFORM");
    if ((!x11 || !*x11) && (!wayland || !*wayland) && (!platform || !*platform))
        setenv("THREEPP_GLFW_PLATFORM", "egl", 0);
}

} // namespace chrononmotion::certification
