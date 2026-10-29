#include "chrononmotion/motion/LightRigRenderPlan.hpp"
#include <chronon3d/render_plan/color_utils.hpp>

#include <cmath>
#include <stdexcept>
#include <string>

namespace chrononmotion::motion {
namespace {
float quantizeColor(float component) {
    const float encoded = std::round(chronon3d::render_plan::linear_to_srgb(component) * 255.f) / 255.f;
    return chronon3d::render_plan::srgb_to_linear(encoded);
}

chronon3d::render_plan::AnimationKeyframePlan keyframe(chronon3d::Frame frame, std::vector<float> value) {
    chronon3d::render_plan::AnimationKeyframePlan result;
    result.frame = frame;
    result.value = std::move(value);
    return result;
}
}

void appendLightRigToRenderPlan(chronon3d::render_plan::RenderPlan& plan,
                                const LightRig& rig, float fps,
                                std::size_t frameCount) {
    using namespace chronon3d::render_plan;
    rig.validate();
    if (!std::isfinite(fps) || fps <= 0.f || frameCount == 0 || frameCount > 4096)
        throw std::invalid_argument("LightRig RenderPlan: fps or frame count is outside V1 bounds");
    std::vector<std::vector<LightSample>> samples(frameCount);
    for (std::size_t frame = 0; frame < frameCount; ++frame)
        samples[frame] = rig.sample(static_cast<float>(frame) / fps);

    for (std::size_t sourceIndex = 0; sourceIndex < rig.sources.size(); ++sourceIndex) {
        const auto& source = rig.sources[sourceIndex];
        const LightSample& first = samples.front()[sourceIndex];
        LayerPlan layer;
        layer.id = "motion_lightrig_" + rig.id + "_" + std::to_string(source.id);
        layer.type = LayerType::Light;
        layer.start_frame = chronon3d::Frame{0};
        layer.duration_frames = chronon3d::Frame{static_cast<std::int64_t>(frameCount)};
        layer.position = {first.local.position.x, first.local.position.y, -first.local.position.z};
        layer.position_dimensions = 3;
        layer.enable_3d = true;

        LightLayerPlan payload;
        payload.kind = source.kind == LightKind::Point ? LightKindPlan::Point
                     : source.kind == LightKind::Spot ? LightKindPlan::Spot
                     : LightKindPlan::Directional;
        payload.radius = 256.f;
        payload.color = {quantizeColor(first.color.r), quantizeColor(first.color.g),
                         quantizeColor(first.color.b), 1.f};
        payload.intensity = first.intensity;
        payload.direction = {first.direction.x, first.direction.y, -first.direction.z};
        payload.range = first.range;
        payload.decay = first.decay;
        payload.angle = first.angle;
        payload.penumbra = first.penumbra;
        payload.cast_shadow = first.castsShadow;
        layer.light = payload;

        AnimationTrackPlan position, intensity, color, direction;
        position.property = "position";
        intensity.property = "light_intensity";
        color.property = "light_color";
        direction.property = "light_direction";
        bool positionChanges = false, intensityChanges = false, colorChanges = false, directionChanges = false;
        for (std::size_t frame = 0; frame < frameCount; ++frame) {
            const auto& state = samples[frame][sourceIndex];
            const auto key = chronon3d::Frame{static_cast<std::int64_t>(frame)};
            const float px = state.local.position.x - first.local.position.x;
            const float py = state.local.position.y - first.local.position.y;
            const float pz = -(state.local.position.z - first.local.position.z);
            position.keyframes.push_back(keyframe(key, {px, py, pz}));
            intensity.keyframes.push_back(keyframe(key, {state.intensity}));
            color.keyframes.push_back(keyframe(key, {quantizeColor(state.color.r),
                quantizeColor(state.color.g), quantizeColor(state.color.b), 1.f}));
            direction.keyframes.push_back(keyframe(key, {state.direction.x, state.direction.y, -state.direction.z}));
            positionChanges = positionChanges || px != 0.f || py != 0.f || pz != 0.f;
            intensityChanges = intensityChanges || state.intensity != first.intensity;
            colorChanges = colorChanges || state.color.r != first.color.r || state.color.g != first.color.g || state.color.b != first.color.b;
            directionChanges = directionChanges || state.direction != first.direction;
        }
        AnimationPlan animation;
        if (positionChanges) animation.tracks.push_back(std::move(position));
        if (intensityChanges) animation.tracks.push_back(std::move(intensity));
        if (colorChanges) animation.tracks.push_back(std::move(color));
        if (directionChanges && payload.kind != LightKindPlan::Point) animation.tracks.push_back(std::move(direction));
        if (!animation.tracks.empty()) layer.animation = std::move(animation);
        plan.layers.push_back(std::move(layer));
    }
}
} // namespace chrononmotion::motion
