#include "chrononmotion/motion/OfflineBaking.hpp"
#include "OfflineBakingInternal.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace chrononmotion::motion {
using detail::kEpsilon;
using detail::kMaximumBakedFrames;
using detail::kMaximumBakedPathCommands;
using detail::kMaximumBakedWork;
using detail::validateBakedPath;
using detail::validateFrameRange;
using detail::validateFrameTimes;
using detail::validatePathPair;

BakedPath interpolateBakedPaths(const BakedPath& from, const BakedPath& to, float amount) {
    validatePathPair(from, to);
    if (!std::isfinite(amount)) throw std::invalid_argument("interpolateBakedPaths: amount must be finite");
    amount = std::clamp(amount, 0.f, 1.f);
    BakedPath result = from;
    for (std::size_t i = 0; i < result.size(); ++i) {
        result[i].point.lerp(to[i].point, amount);
        result[i].control1.lerp(to[i].control1, amount);
        result[i].control2.lerp(to[i].control2, amount);
        if (!math::isFinite(result[i].point) || !math::isFinite(result[i].control1) || !math::isFinite(result[i].control2))
            throw std::invalid_argument("interpolateBakedPaths: commands must have finite coordinates");
    }
    return result;
}

std::vector<BakedPath> bakeSvgPathMorph(std::string_view from, std::string_view to,
                                         int firstFrame, int lastFrame) {
    validateFrameRange(firstFrame, lastFrame);
    const BakedPath start = parseSvgPath(from), end = parseSvgPath(to);
    validatePathPair(start, end);
    std::vector<BakedPath> frames;
    const std::size_t count = static_cast<std::size_t>(lastFrame - firstFrame) + 1;
    if (start.size() > kMaximumBakedPathCommands / count)
        throw std::invalid_argument("bakeSvgPathMorph: requested geometry exceeds the offline command budget");
    frames.reserve(count);
    for (std::size_t frame = 0; frame < count; ++frame) {
        const float t = count <= 1 ? 0.f : static_cast<float>(frame) / static_cast<float>(count - 1);
        frames.push_back(interpolateBakedPaths(start, end, t));
    }
    return frames;
}

std::vector<BakedPath> bakeWaveDeform(const BakedPath& rest, const WaveDeformerSpec& spec,
                                        int firstFrame, int lastFrame, float fps) {
    validateFrameRange(firstFrame, lastFrame);
    validateBakedPath(rest, "bakeWaveDeform");
    if (!std::isfinite(fps) || fps <= 0.f || !std::isfinite(spec.amplitude) ||
        !std::isfinite(spec.wavelength) || spec.wavelength <= 0.f ||
        !std::isfinite(spec.speed) || !std::isfinite(spec.phase) ||
        !std::isfinite(spec.speed * (static_cast<float>(lastFrame) / fps)) ||
        (spec.axis != WaveAxis::X && spec.axis != WaveAxis::Y))
        throw std::invalid_argument("bakeWaveDeform: invalid wave or frame-rate parameters");
    validateFrameTimes(fps, firstFrame, lastFrame, "bakeWaveDeform");
    const std::size_t frameCount = static_cast<std::size_t>(lastFrame - firstFrame) + 1;
    if (rest.size() > kMaximumBakedPathCommands / frameCount)
        throw std::invalid_argument("bakeWaveDeform: requested geometry exceeds the offline command budget");
    const auto deformPoint = [&](Vector2 point, float time) {
        const float coordinate = spec.axis == WaveAxis::X ? point.x : point.y;
        const float angular = 2.f * math::PI * coordinate / spec.wavelength + spec.phase;
        const float temporalPhase = spec.speed * time;
        if (!std::isfinite(angular) || !std::isfinite(temporalPhase))
            throw std::invalid_argument("bakeWaveDeform: wave phase overflowed finite range");
        const float displacement = spec.amplitude *
                (std::sin(angular - temporalPhase) - std::sin(angular)) * spec.falloff.evaluate(point);
        if (spec.axis == WaveAxis::X) point.x += displacement;
        else point.y += displacement;
        if (!math::isFinite(point)) throw std::invalid_argument("bakeWaveDeform: deformation overflowed finite coordinates");
        return point;
    };
    std::vector<BakedPath> result;
    result.reserve(frameCount);
    for (std::size_t frame = 0; frame < frameCount; ++frame) {
        const float time = static_cast<float>(firstFrame + static_cast<int>(frame)) / fps;
        BakedPath path = rest;
        for (auto& command : path) {
            if (command.type == BakedPathCommandType::Close) continue;
            command.point = deformPoint(command.point, time);
            if (command.type == BakedPathCommandType::CubicTo) {
                command.control1 = deformPoint(command.control1, time);
                command.control2 = deformPoint(command.control2, time);
            }
        }
        result.push_back(std::move(path));
    }
    return result;
}

std::vector<std::vector<Vector2>> bakeFieldSimulation(
        std::span<const Vector2> restPositions, std::span<const FieldEffectorSpec> effectors,
        int firstFrame, int lastFrame, float fps) {
    validateFrameRange(firstFrame, lastFrame);
    if (!std::isfinite(fps) || fps <= 0.f || restPositions.size() > 100000 || effectors.size() > 1024 ||
        (restPositions.size() != 0 && effectors.size() > kMaximumBakedWork / restPositions.size()) ||
        (restPositions.size() != 0 && effectors.size() != 0 &&
         static_cast<std::size_t>(lastFrame - firstFrame + 1) >
                 kMaximumBakedWork / restPositions.size() / effectors.size()))
        throw std::invalid_argument("bakeFieldSimulation: invalid frame rate or offline work budget");
    validateFrameTimes(fps, firstFrame, lastFrame, "bakeFieldSimulation");
    const std::size_t frameCount = static_cast<std::size_t>(lastFrame - firstFrame) + 1;
    if (restPositions.size() != 0 && frameCount > kMaximumBakedFrames / restPositions.size())
        throw std::invalid_argument("bakeFieldSimulation: requested samples exceed the offline budget");
    for (const Vector2& point : restPositions)
        if (!math::isFinite(point)) throw std::invalid_argument("bakeFieldSimulation: rest positions must be finite");
    for (const auto& field : effectors) {
        if (!math::isFinite(field.origin) || !std::isfinite(field.strength) ||
            !std::isfinite(field.falloffRadius) || field.falloffRadius <= 0.f ||
            !std::isfinite(field.coreRadius) || field.coreRadius < 0.f ||
            field.coreRadius >= field.falloffRadius ||
            !std::isfinite(field.innerFalloffRadius) || field.innerFalloffRadius < 0.f ||
            field.innerFalloffRadius >= field.falloffRadius ||
            static_cast<unsigned>(field.kind) > static_cast<unsigned>(FieldEffectorSpec::Kind::Vortex) ||
            static_cast<unsigned>(field.falloffKind) > static_cast<unsigned>(FalloffField::Kind::Box))
            throw std::invalid_argument("bakeFieldSimulation: invalid effector settings");
    }
    std::vector<std::vector<Vector2>> result;
    result.reserve(frameCount);
    for (std::size_t frame = 0; frame < frameCount; ++frame) {
        const float time = static_cast<float>(firstFrame + static_cast<int>(frame)) / fps;
        std::vector<Vector2> pose;
        pose.reserve(restPositions.size());
        for (const Vector2& rest : restPositions) {
            Vector2 displaced = rest;
            for (const auto& field : effectors) {
                Vector2 radial;
                radial.subVectors(rest, field.origin);
                const float radius = std::hypot(radial.x, radial.y);
                if (!std::isfinite(radius) || radius >= field.falloffRadius) continue;
                const FalloffField falloffField{field.origin, field.falloffRadius,
                                                field.innerFalloffRadius, field.falloffKind};
                const float falloff = falloffField.evaluate(rest);
                if (falloff <= 0.f) continue;
                if (field.kind == FieldEffectorSpec::Kind::Vortex) {
                    if (radius >= field.coreRadius) {
                        const float angle = field.strength * time * falloff / radius;
                        const float cosine = std::cos(angle);
                        const float sine = std::sin(angle);
                        displaced.set(field.origin.x + radial.x * cosine - radial.y * sine,
                                      field.origin.y + radial.x * sine + radial.y * cosine);
                    } else {
                        const float outward = (field.coreRadius - radius) *
                                              std::fabs(field.strength) * time * falloff;
                        Vector2 direction = radius > 1e-6f ? radial / radius : Vector2(1.f, 0.f);
                        displaced.addScaledVector(direction, outward);
                        displaced.rotateAround(field.origin, field.strength * time * falloff / field.coreRadius);
                    }
                } else {
                    if (radius <= 1e-6f) continue;
                    const float inverseRadius = 1.f / std::max(radius, std::max(field.coreRadius, 1e-4f));
                    const float sign = field.kind == FieldEffectorSpec::Kind::Attractor ? -1.f : 1.f;
                    const float amount = sign * field.strength * time * falloff * inverseRadius;
                    displaced.x += radial.x * amount;
                    displaced.y += radial.y * amount;
                }
            }
            if (!math::isFinite(displaced)) throw std::invalid_argument("bakeFieldSimulation: field output overflowed finite range");
            pose.push_back(displaced);
        }
        result.push_back(std::move(pose));
    }
    return result;
}

std::vector<std::vector<Vector2>> bakeFieldSimulation(
        std::span<const Vector2> restPositions, const FieldEffectorSpec& spec,
        int firstFrame, int lastFrame, float fps) {
    return bakeFieldSimulation(restPositions, std::span<const FieldEffectorSpec>(&spec, 1),
                               firstFrame, lastFrame, fps);
}

std::vector<std::vector<BakedPath>> bakeSvgSkinDeform(
        const BakedPath& rest, std::span<const SvgBone> bones,
        std::span<const std::vector<SvgSkinBinding>> bindings,
        std::span<const std::vector<Matrix4>> boneWorldPerFrame,
        int firstFrame, int lastFrame) {
    validateFrameRange(firstFrame, lastFrame);
    validateBakedPath(rest, "bakeSvgSkinDeform");
    if (bones.empty() || bones.size() > 4096 || boneWorldPerFrame.size() !=
        static_cast<std::size_t>(lastFrame - firstFrame) + 1 ||
        (bindings.size() != rest.size() * 3))
        throw std::invalid_argument("bakeSvgSkinDeform: bones, frame matrices, and per-coordinate bindings must agree");
    for (std::size_t bone = 0; bone < bones.size(); ++bone) {
        if (!math::isFinite(bones[bone].head) || !math::isFinite(bones[bone].tail) ||
            bones[bone].head.distanceTo(bones[bone].tail) <= kEpsilon ||
            bones[bone].parentIndex >= static_cast<int>(bone) || bones[bone].parentIndex < -1)
            throw std::invalid_argument("bakeSvgSkinDeform: bones must have finite nonzero segments and parents before children");
    }
    for (std::size_t coordinate = 0; coordinate < bindings.size(); ++coordinate) {
        float totalWeight = 0.f;
        if (bindings[coordinate].empty() || bindings[coordinate].size() > 2)
            throw std::invalid_argument("bakeSvgSkinDeform: each coordinate needs one or two bone bindings");
        for (const auto& binding : bindings[coordinate]) {
            if (binding.boneIndex < 0 || static_cast<std::size_t>(binding.boneIndex) >= bones.size() ||
                !std::isfinite(binding.weight) || binding.weight < 0.f)
                throw std::invalid_argument("bakeSvgSkinDeform: invalid bone binding");
            totalWeight += binding.weight;
        }
        if (!(totalWeight > 0.f) || !std::isfinite(totalWeight))
            throw std::invalid_argument("bakeSvgSkinDeform: binding weights must have a finite positive sum");
    }
    for (const auto& frameMatrices : boneWorldPerFrame) {
        if (frameMatrices.size() != bones.size())
            throw std::invalid_argument("bakeSvgSkinDeform: every frame needs one matrix per bone");
        for (const auto& matrix : frameMatrices)
            for (float component : matrix.elements)
                if (!std::isfinite(component)) throw std::invalid_argument("bakeSvgSkinDeform: matrices must be finite");
    }
    const std::size_t frameCount = boneWorldPerFrame.size();
    if (rest.size() > kMaximumBakedPathCommands / frameCount ||
        bones.size() > kMaximumBakedWork / frameCount)
        throw std::invalid_argument("bakeSvgSkinDeform: requested geometry or matrix work exceeds the offline budget");
    std::vector<std::vector<BakedPath>> result;
    result.reserve(boneWorldPerFrame.size());
    for (const auto& frameMatrices : boneWorldPerFrame) {
        BakedPath path = rest;
        std::size_t coordinate = 0;
        for (auto& command : path) {
            if (command.type == BakedPathCommandType::Close) {
                coordinate += 3;
                continue;
            }
            const auto skinPoint = [&](const Vector2& point, std::size_t bindingIndex) {
                Vector3 source(point.x, point.y, 0.f);
                Vector3 output{};
                float totalWeight = 0.f;
                for (const auto& binding : bindings[bindingIndex]) {
                    Vector3 transformedPoint = source;
                    transformedPoint.applyMatrix4(frameMatrices[static_cast<std::size_t>(binding.boneIndex)]);
                    output.addScaledVector(transformedPoint, binding.weight);
                    totalWeight += binding.weight;
                }
                output.multiplyScalar(1.f / totalWeight);
                Vector2 solved(output.x, output.y);
                if (!math::isFinite(solved)) throw std::invalid_argument("bakeSvgSkinDeform: skinned point overflowed finite coordinates");
                return solved;
            };
            command.point = skinPoint(command.point, coordinate++);
            command.control1 = skinPoint(command.control1, coordinate++);
            command.control2 = skinPoint(command.control2, coordinate++);
        }
        result.push_back({std::move(path)});
    }
    return result;
}

std::vector<std::vector<BakedPath>> bakePathPinDeform(
        const BakedPath& rest, std::span<const PathPin> pins,
        float stiffnessFalloff, int firstFrame, int lastFrame) {
    validateFrameRange(firstFrame, lastFrame);
    validateBakedPath(rest, "bakePathPinDeform");
    if (pins.empty() || pins.size() > 4096 || !std::isfinite(stiffnessFalloff) || stiffnessFalloff <= 0.f)
        throw std::invalid_argument("bakePathPinDeform: pins and positive stiffness falloff are required");
    std::vector<std::array<float, 3>> arc(rest.size());
    float totalLength = 0.f;
    Vector2 current{};
    Vector2 subpathStart{};
    bool hasCurrent = false;
    constexpr unsigned arcSamples = 24;
    const auto cubicPoint = [](const Vector2& p0, const Vector2& p1,
                               const Vector2& p2, const Vector2& p3, float t) {
        const float u = 1.f - t;
        return p0 * (u * u * u) + p1 * (3.f * u * u * t) +
               p2 * (3.f * u * t * t) + p3 * (t * t * t);
    };
    for (std::size_t i = 0; i < rest.size(); ++i) {
        const auto& command = rest[i];
        if (command.type == BakedPathCommandType::Close) {
            if (hasCurrent) {
                totalLength += current.distanceTo(subpathStart);
                current = subpathStart;
            }
            continue;
        }
        if (command.type == BakedPathCommandType::MoveTo) {
            current = command.point;
            subpathStart = current;
            hasCurrent = true;
            arc[i] = {totalLength, totalLength, totalLength};
            continue;
        }
        if (!hasCurrent) throw std::invalid_argument("bakePathPinDeform: drawing command has no current subpath");
        std::array<float, arcSamples + 1> sampledLength{};
        Vector2 previous = current;
        for (unsigned sample = 1; sample <= arcSamples; ++sample) {
            const float t = static_cast<float>(sample) / static_cast<float>(arcSamples);
            const Vector2 point = cubicPoint(current, command.control1, command.control2, command.point, t);
            sampledLength[sample] = sampledLength[sample - 1] + previous.distanceTo(point);
            previous = point;
        }
        const float segmentStart = totalLength;
        const auto lengthAt = [&](float t) {
            const float samplePosition = t * static_cast<float>(arcSamples);
            const unsigned lower = static_cast<unsigned>(samplePosition);
            if (lower >= arcSamples) return segmentStart + sampledLength.back();
            const float fraction = samplePosition - static_cast<float>(lower);
            return segmentStart + sampledLength[lower] +
                   (sampledLength[lower + 1] - sampledLength[lower]) * fraction;
        };
        arc[i] = {lengthAt(1.f / 3.f), lengthAt(2.f / 3.f), lengthAt(1.f)};
        totalLength += sampledLength.back();
        current = command.point;
    }
    if (!(totalLength > kEpsilon)) throw std::invalid_argument("bakePathPinDeform: path must have positive arc length");
    const std::size_t frameCount = static_cast<std::size_t>(lastFrame - firstFrame) + 1;
    if (rest.size() > kMaximumBakedPathCommands / frameCount ||
        (rest.size() != 0 && pins.size() > kMaximumBakedWork / frameCount / rest.size()))
        throw std::invalid_argument("bakePathPinDeform: requested geometry or pin work exceeds the offline budget");
    for (const auto& pin : pins)
        if (!std::isfinite(pin.arcLengthT) || pin.arcLengthT < 0.f || pin.arcLengthT > 1.f ||
            !math::isFinite(pin.target) || !math::isFinite(pin.oscillation) || !std::isfinite(pin.stiffness) ||
            pin.stiffness < 0.f || pin.stiffness > 1.f)
            throw std::invalid_argument("bakePathPinDeform: invalid pin data");
    std::vector<std::vector<BakedPath>> result;
    result.reserve(frameCount);
    for (std::size_t frame = 0; frame < frameCount; ++frame) {
        const float time = frameCount <= 1 ? 0.f :
                static_cast<float>(frame) / static_cast<float>(frameCount - 1);
        BakedPath path = rest;
        for (std::size_t i = 0; i < path.size(); ++i) {
            if (path[i].type == BakedPathCommandType::Close) continue;
            const auto deformPoint = [&](Vector2& point, float arcLength) {
                const float pointT = arcLength / totalLength;
                Vector2 displacement{};
                for (const auto& pin : pins) {
                    const float distance = std::fabs(pointT - pin.arcLengthT);
                    const float decay = std::exp(-distance / stiffnessFalloff);
                    const float targetWeight = pin.stiffness * decay;
                    const float oscillationWeight = pin.stiffness * (1.f - decay);
                    const Vector2 animatedTarget = pin.target + pin.oscillation * std::sin(2.f * math::PI * time);
                    displacement.addScaledVector(pin.target - point, targetWeight);
                    displacement.addScaledVector(animatedTarget - pin.target, oscillationWeight);
                }
                point.add(displacement);
                if (!math::isFinite(point)) throw std::invalid_argument("bakePathPinDeform: deformation overflowed finite coordinates");
            };
            deformPoint(path[i].point, arc[i][2]);
            if (path[i].type == BakedPathCommandType::CubicTo) {
                deformPoint(path[i].control1, arc[i][0]);
                deformPoint(path[i].control2, arc[i][1]);
            }
        }
        result.push_back({std::move(path)});
    }
    return result;
}

} // namespace chrononmotion::motion
