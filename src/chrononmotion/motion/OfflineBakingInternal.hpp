#pragma once

#include "chrononmotion/motion/OfflineBaking.hpp"
#include "chrononmotion/math/Finite.hpp"
#include "chrononmotion/math/MathUtils.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <string>

namespace chrononmotion::motion::detail {

inline constexpr float kEpsilon = 1e-6f;
inline constexpr std::size_t kMaximumBakedFrames = 1'000'001;
inline constexpr std::size_t kMaximumBakedWork = 100'000'000;
inline constexpr std::size_t kMaximumBakedPathCommands = 1'000'000;

inline void validateFrameRange(int firstFrame, int lastFrame) {
    if (firstFrame < 0 || lastFrame < firstFrame ||
        static_cast<std::size_t>(lastFrame - firstFrame) + 1 > kMaximumBakedFrames)
        throw std::invalid_argument("offline bake requires 0 <= firstFrame <= lastFrame <= firstFrame + 1000000");
}

inline void validateFrameTimes(float fps, int firstFrame, int lastFrame, const char* function) {
    const float firstTime = static_cast<float>(firstFrame) / fps;
    const float lastTime = static_cast<float>(lastFrame) / fps;
    const float previousLastTime = lastFrame > firstFrame
        ? static_cast<float>(lastFrame - 1) / fps : lastTime;
    if (!std::isfinite(firstTime) || !std::isfinite(lastTime) ||
        (firstFrame < lastFrame &&
         (!(static_cast<float>(firstFrame + 1) / fps > firstTime) || !(lastTime > previousLastTime))))
        throw std::invalid_argument(std::string(function) + ": frame times are not representable as strictly increasing finite floats");
}

inline float wrapAngle(float angle) {
    return std::remainder(angle, math::TWO_PI);
}

inline void addVectorKey(Track<Vector3>& track, int frame, float fps, const Vector2& value) {
    track.add(static_cast<float>(frame) / fps, Vector3(value.x, value.y, 0.f));
}

inline Vector2 transformed(const Vector2& point, const Vector2& anchor,
                           const Vector2& scale, float angle, const Vector2& translation) {
    const float x = (point.x - anchor.x) * scale.x;
    const float y = (point.y - anchor.y) * scale.y;
    const float c = std::cos(angle);
    const float s = std::sin(angle);
    return Vector2(anchor.x + x * c - y * s + translation.x,
                   anchor.y + x * s + y * c + translation.y);
}

inline void validateBakedPath(const BakedPath& path, const char* function) {
    if (path.empty() || path.size() > 65536 || path.front().type != BakedPathCommandType::MoveTo)
        throw std::invalid_argument(std::string(function) + ": path must begin with moveto and contain 1..65536 commands");
    bool hasCurrentSubpath = false;
    for (const auto& command : path) {
        if (static_cast<unsigned>(command.type) > static_cast<unsigned>(BakedPathCommandType::Close))
            throw std::invalid_argument(std::string(function) + ": path contains an unsupported command type");
        if (command.type == BakedPathCommandType::Close) {
            if (!hasCurrentSubpath)
                throw std::invalid_argument(std::string(function) + ": close command has no preceding subpath");
            continue;
        }
        if (command.type == BakedPathCommandType::MoveTo) hasCurrentSubpath = true;
        if (!math::isFinite(command.point))
            throw std::invalid_argument(std::string(function) + ": path coordinates must be finite");
        if (command.type == BakedPathCommandType::CubicTo &&
            (!math::isFinite(command.control1) || !math::isFinite(command.control2)))
            throw std::invalid_argument(std::string(function) + ": cubic controls must be finite");
    }
}

inline void validatePathPair(const BakedPath& a, const BakedPath& b) {
    validateBakedPath(a, "path morph");
    validateBakedPath(b, "path morph");
    if (a.size() != b.size())
        throw std::invalid_argument("path morph requires identical non-empty command topology");
    for (std::size_t i = 0; i < a.size(); ++i)
        if (a[i].type != b[i].type)
            throw std::invalid_argument("path morph requires matching command types");
}

} // namespace chrononmotion::motion::detail
