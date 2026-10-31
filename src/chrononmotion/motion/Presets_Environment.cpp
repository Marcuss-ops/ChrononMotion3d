#include "CameraRigTrackBatch.hpp"
// ChrononMotion — presets for environments, 3D backgrounds, ground grids, and focus.
//
// Background plane fitting, horizon ground grids, ambient 3D floating fields,
// organic aura motion, and camera-linked depth of field.

#include "Presets_detail.hpp"
#include "chrononmotion/motion/CameraMotion.hpp"

#include "chrononmotion/math/MathUtils.hpp"

#include <algorithm>
#include <cmath>

namespace chrononmotion::motion::presets {

    using detail::authorizePresetForCamera;
    using detail::authorizePresetForLayer;
    using detail::keyVector3;
    using detail::restOf;

    Vector2 frustumCoverage(float fovDegrees, float aspect, float distance, float overscan) {

        if (distance <= 0.f || overscan <= 0.f || fovDegrees <= 0.f || aspect <= 0.f) {
            return Vector2(0.f, 0.f);
        }
        const float halfFov = math::degToRad(fovDegrees) * 0.5f;
        const float h = 2.f * distance * std::tan(halfFov);
        const float w = h * aspect;
        return Vector2(w * overscan, h * overscan);
    }

    void setupBackgroundPlane(Layer& layer, float depth, float fovDegrees, float aspect, float overscan) {
        authorizePresetForLayer(layer, "setupBackgroundPlane");

        const Vector2 coverage = frustumCoverage(fovDegrees, aspect, std::abs(depth), overscan);
        layer.transform.position.z = depth;
        layer.transform.scale.set(coverage.x, coverage.y, 1.f);
        layer.flat();
    }

    void horizonGrid(Layer& layer, float floorY, float depthZ, float tiltDegrees) {
        authorizePresetForLayer(layer, "horizonGrid");

        layer.transform.position.set(0.f, floorY, depthZ);
        layer.transform.rotation.set(math::degToRad(tiltDegrees), 0.f, 0.f);
        layer.flat();
    }

    void groundTravel(Layer& layer, int inFrame, int duration, float distanceZ, float fps,
                      const Easing& easing) {
        authorizePresetForLayer(layer, "groundTravel");

        const Transform rest = restOf(layer, inFrame, fps);
        const float t0 = frameToTime(inFrame, fps);
        const float t1 = frameToTime(inFrame + duration, fps);

        Vector3 to = rest.position;
        to.z += distanceZ;

        keyVector3(layer.tracks.position, t0, t1, rest.position, to, easing);
    }

    void ambientDriftField(const std::vector<Layer*>& layers, int inFrame, int duration, float fps,
                           std::uint32_t seed, float maxRadius, float speed) {
        for (const auto* layer : layers) if (layer) authorizePresetForLayer(*layer, "ambientDriftField");

        if (layers.empty() || duration <= 0) return;
        const float t0 = frameToTime(inFrame, fps);
        const float t1 = frameToTime(inFrame + duration, fps);
        constexpr int kKeyCount = 6;

        for (std::size_t i = 0; i < layers.size(); ++i) {
            Layer* layer = layers[i];
            if (!layer) continue;
            const Transform rest = restOf(*layer, inFrame, fps);

            const float phaseX = static_cast<float>((seed ^ (i * 7919U)) % 360U) * math::DEG2RAD;
            const float phaseY = static_cast<float>((seed ^ (i * 6271U + 13U)) % 360U) * math::DEG2RAD;
            const float phaseZ = static_cast<float>((seed ^ (i * 3571U + 29U)) % 360U) * math::DEG2RAD;
            const float freqX = 1.0f + 0.3f * static_cast<float>((i % 5) - 2);
            const float freqY = 1.2f + 0.25f * static_cast<float>((i % 4) - 1);
            const float freqZ = 0.8f + 0.2f * static_cast<float>((i % 3) - 1);

            for (int k = 0; k <= kKeyCount; ++k) {
                const float progress = static_cast<float>(k) / static_cast<float>(kKeyCount);
                const float t = t0 + (t1 - t0) * progress;
                const float angle = progress * math::TWO_PI * speed;
                const float dx = std::sin(angle * freqX + phaseX) * maxRadius;
                const float dy = std::cos(angle * freqY + phaseY) * maxRadius;
                const float dz = std::sin(angle * freqZ + phaseZ) * (maxRadius * 0.5f);

                layer->tracks.position.add(t,
                    Vector3(rest.position.x + dx, rest.position.y + dy, rest.position.z + dz),
                    Easing::easeInOut());
            }
        }
    }

    void auraBlobMotion(Layer& layer, int inFrame, int duration, const Vector3& center,
                        const Vector2& radius, float frequency, float fps, std::uint32_t seed) {
        authorizePresetForLayer(layer, "auraBlobMotion");

        if (duration <= 0) return;
        const float t0 = frameToTime(inFrame, fps);
        const float t1 = frameToTime(inFrame + duration, fps);
        constexpr int kSteps = 8;
        const float phase = static_cast<float>(seed % 360U) * math::DEG2RAD;

        for (int i = 0; i <= kSteps; ++i) {
            const float progress = static_cast<float>(i) / static_cast<float>(kSteps);
            const float t = t0 + (t1 - t0) * progress;
            const float angle = progress * math::TWO_PI * frequency + phase;
            const float x = center.x + std::sin(angle) * radius.x;
            const float y = center.y + std::sin(angle * 1.5f + 0.5f) * radius.y;
            const float z = center.z + std::cos(angle * 0.7f) * (std::min(radius.x, radius.y) * 0.25f);

            layer.tracks.position.add(t, Vector3(x, y, z), Easing::easeInOut());
        }
    }

    void autoFocus(CameraRig& rig, const Layer& targetLayer, int inFrame, int duration, float fps) {
        authorizePresetForCamera(rig, "autoFocus");
        authorizePresetForLayer(targetLayer, "autoFocus");

        if (duration <= 0) return;
        const float t0 = frameToTime(inFrame, fps);
        const float t1 = frameToTime(inFrame + duration, fps);
        constexpr int kSamples = 6;
        CameraRigTrackBatch tracks(rig);

        for (int i = 0; i <= kSamples; ++i) {
            const float progress = static_cast<float>(i) / static_cast<float>(kSamples);
            const float t = t0 + (t1 - t0) * progress;
            const CameraPose camPose = rig.sample(t);
            const Transform targetPose = targetLayer.sample(t);
            const float dist = camPose.position.distanceTo(targetPose.position);
            tracks.focusDistanceTrack().add(t, dist, Easing::easeInOut());
        }
        tracks.apply(rig);
    }

}// namespace chrononmotion::motion::presets
