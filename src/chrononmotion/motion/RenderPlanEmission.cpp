// ChrononMotion — the producer's half of the RenderPlan contract.
//
// `lowerToRenderPlan` decides *what* a plan says; this decides how it is
// written down. The two are kept apart on purpose: a writer that also lowered
// would be a second place an anchor or a singular transform could be accepted
// or rejected, and the lowering is the one that owns that judgement.
//
// Every value here is copied from the plan and nothing is derived from it, so
// the emitted document is a faithful transcription and never a second opinion.
// Anything the schema has no field for is refused by name rather than dropped:
// a plan that loses a mask on the way out renders as a different composition,
// and a file that renders differently from its model is worse than no file.

#include "chrononmotion/motion/RenderPlanEmission.hpp"
#include "RenderPlanEmissionInternal.hpp"

#include "JsonWriter.hpp"

#include <chronon3d/render_plan/color_utils.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <initializer_list>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace chrononmotion::motion {

    namespace {

        using chronon3d::render_plan::AnimationKeyframePlan;
        using chronon3d::render_plan::AnimationPlan;
        using chronon3d::render_plan::AnimationTrackPlan;
        using chronon3d::render_plan::CameraPlan;
        using chronon3d::render_plan::CameraSamplePlan;
        using chronon3d::render_plan::FitMode;
        using chronon3d::render_plan::InlineMeshPlan;
        using chronon3d::render_plan::LayerPlan;
        using chronon3d::render_plan::LayerStylePlan;
        using chronon3d::render_plan::LayerType;
        using chronon3d::render_plan::OutputFormat;
        using chronon3d::render_plan::PathOperatorKindPlan;
        using chronon3d::render_plan::RenderBudget;
        using chronon3d::render_plan::RenderPlan;
        using chronon3d::render_plan::ShapeContentPlan;
        using chronon3d::render_plan::ShapeContentTypePlan;
        using chronon3d::render_plan::ShapePathCommandPlan;
        using chronon3d::render_plan::ShapePathCommandTypePlan;


        void writeAnimation(detail::Writer& writer, const AnimationPlan& animation) {
            writer.beginObject();
            writer.key("tracks");
            writer.beginArray();
            for (const AnimationTrackPlan& track : animation.tracks) {
                writer.item();
                writer.beginObject();
                writer.key("property");
                writer.text(track.property);
                writer.key("easing");
                writer.text(track.easing);
                writer.key("keyframes");
                writeKeyframes(writer, track.keyframes, false);
                writer.endObject();
            }
            writer.endArray();
            writer.endObject();
        }

        void writeLayer(detail::Writer& writer, const LayerPlan& layer, const std::string& schema) {

            writer.beginObject();

            writer.key("id");
            writer.text(layer.id);
            writer.key("type");
            writer.token(layerTypeName(layer.type));

            if (layer.type == LayerType::Adjustment) {
                writer.key("effects");
                writeEffects(writer, layer.effects);
                writer.endObject();
                return;
            }

            if (!layer.asset.empty()) {
                writer.key("asset");
                writer.text(layer.asset);
            }
            if (!layer.source.empty()) {
                writer.key("source");
                writer.text(layer.source);
            }

            const bool nativeFillShape = layer.shape &&
                (layer.shape->type == ShapeContentTypePlan::Grid ||
                 layer.shape->type == ShapeContentTypePlan::DotGrid || layer.shape->field ||
                 layer.shape->gradient_mesh || !layer.shape->fill_color);
            if (!nativeFillShape) {
                writer.key("color");
                writeFloats(writer, layer.color, layer.color.size());
            }

            if (layer.size_dimensions != 0) {
                writer.key("size");
                writeFloats(writer, layer.size, layer.size_dimensions);
            }
            if (layer.position_dimensions != 0) {
                writer.key("position");
                writeFloats(writer, layer.position, layer.position_dimensions);
            }
            if (layer.scale_dimensions != 0) {
                writer.key("scale");
                writeFloats(writer, layer.scale, layer.scale_dimensions);
            }
            if (layer.rotation_dimensions != 0) {
                writer.key("rotation");
                writeFloats(writer, layer.rotation, layer.rotation_dimensions);
            }

            writer.key("enable_3d");
            writer.boolean(layer.enable_3d);
            writer.key("screen_space");
            writer.boolean(layer.screen_space);
            writer.key("loop");
            writer.boolean(layer.loop);

            if (layer.blend_mode) {
                writer.key("blend_mode");
                writer.token(blendModeName(*layer.blend_mode));
            }
            if (layer.opacity) {
                writer.key("opacity");
                writer.number(*layer.opacity);
            }
            if (layer.start_frame) {
                writer.key("start_frame");
                writer.integer(layer.start_frame->integral());
            }
            if (layer.duration_frames) {
                writer.key("duration_frames");
                writer.integer(layer.duration_frames->integral());
            }
            if (layer.depth_role) {
                writer.key("depth_role");
                writer.text(*layer.depth_role);
            }
            if (layer.depth_offset != 0.f) {
                writer.key("depth_offset");
                writer.number(layer.depth_offset);
            }
            if (layer.safe_insets) {
                writer.key("safe_insets");
                writer.beginObject();
                writer.key("top");
                writer.number((*layer.safe_insets)[0]);
                writer.key("right");
                writer.number((*layer.safe_insets)[1]);
                writer.key("bottom");
                writer.number((*layer.safe_insets)[2]);
                writer.key("left");
                writer.number((*layer.safe_insets)[3]);
                writer.endObject();
            }
            if (layer.radius) {
                writer.key("radius");
                writer.number(*layer.radius);
            }
            if (layer.fit) {
                writer.key("fit");
                writer.token(fitName(*layer.fit));
            }
            if (layer.crop) {
                writer.key("crop");
                writer.beginObject();
                writer.key("enabled");
                writer.boolean(layer.crop->enabled);
                writer.key("origin");
                writeFloats(writer, layer.crop->origin, 2);
                writer.key("size");
                writeFloats(writer, layer.crop->size, 2);
                writer.key("flip_x");
                writer.boolean(layer.crop->flip_x);
                writer.endObject();
            }

            writeLayerMasks(writer, layer);
            if (layer.track_matte) { writer.key("track_matte"); writeTrackMatte(writer, *layer.track_matte); }

            if (layer.style && layer.style->background &&
                (layer.type == LayerType::Image || layer.type == LayerType::Video)) {
                writer.key("style");
                writeMediaStyle(writer, *layer.style);
            }
            if (!layer.effects.empty()) {
                writer.key("effects");
                writeEffects(writer, layer.effects);
            }

            if (layer.type == LayerType::Text) {
                writer.key("text");
                writer.text(layer.text);
                writer.key("style");
                writeStyle(writer, *layer.style);
            }
            if (layer.shape) {
                writer.key("shape");
                writeShape(writer, *layer.shape);
            }
            if (layer.particle_emitter) {
                writer.key("particle_emitter");
                writeParticleEmitter(writer, *layer.particle_emitter, schema);
            }
            if (layer.light) {
                writer.key("light");
                writeLight(writer, *layer.light);
            }
            if (!layer.text_spans.empty()) {
                writer.key("spans");
                writeTextSpans(writer, layer.text_spans);
            }
            if (!layer.text_animators.empty()) {
                writer.key("text_animators");
                writeTextAnimators(writer, layer.text_animators);
            }
            if (layer.animation) {
                writer.key("animation");
                writeAnimation(writer, *layer.animation);
            }
            if (layer.time_remap) {
                writer.key("time_remap");
                const auto& remap = *layer.time_remap;
                writer.beginObject();
                if (remap.speed) {
                    writer.key("speed");
                    writer.number(*remap.speed);
                }
                if (remap.reverse) {
                    writer.key("reverse");
                    writer.boolean(true);
                }
                if (remap.freeze_frame) {
                    writer.key("freeze_frame");
                    writer.integer(remap.freeze_frame->integral());
                }
                if (remap.animation) {
                    writer.key("animation");
                    writer.beginObject();
                    writer.key("easing");
                    writer.text(remap.animation->easing);
                    writer.key("keyframes");
                    writeKeyframes(writer, remap.animation->keyframes, true);
                    writer.endObject();
                }
                writer.endObject();
            }
            if (layer.inline_mesh) {
                writer.key("inline_mesh");
                writeInlineMesh(writer, *layer.inline_mesh);
            }

            if (!layer.effect_param_tracks.empty()) {
                writer.key("effect_param_tracks");
                writeEffectParameterTracks(writer, layer.effect_param_tracks);
            }

            writer.endObject();
        }

        void writeCamera(detail::Writer& writer, const CameraPlan& camera) {

            writer.beginObject();
            writer.key("type");
            writer.text(camera.type);
            writer.key("position");
            writeFloats(writer, camera.position, camera.position.size());
            writer.key("rotation_deg");
            writeFloats(writer, camera.rotation_deg, camera.rotation_deg.size());
            writer.key("fov_deg");
            writer.number(camera.fov_deg);
            writer.key("near");
            writer.number(camera.near);
            writer.key("far");
            writer.number(camera.far);
            writer.key("zoom");
            writer.number(camera.zoom);
            if (camera.focal_length_mm > 0.f) {
                writer.key("focal_length_mm");
                writer.number(camera.focal_length_mm);
            }
            writer.key("sensor_width_mm");
            writer.number(camera.sensor_width_mm);
            writer.key("lens_shift");
            writeFloats(writer, camera.lens_shift, camera.lens_shift.size());
            if (camera.f_stop > 0.f) {
                writer.key("f_stop");
                writer.number(camera.f_stop);
            }
            writer.key("iris_blades");
            writer.integer(camera.iris_blades);
            const CameraPlan defaults{};
            if (camera.dof_enabled || camera.dof_focus_distance != defaults.dof_focus_distance ||
                camera.dof_aperture != defaults.dof_aperture || camera.dof_max_blur != defaults.dof_max_blur ||
                camera.dof_f_stop != defaults.dof_f_stop || camera.dof_iris_blades != defaults.dof_iris_blades ||
                camera.dof_highlight_gain != defaults.dof_highlight_gain) {
                writer.key("dof");
                writer.beginObject();
                writer.key("enabled");
                writer.boolean(camera.dof_enabled);
                writer.key("focus_distance");
                writer.number(camera.dof_focus_distance);
                writer.key("aperture");
                writer.number(camera.dof_aperture);
                writer.key("max_blur");
                writer.number(camera.dof_max_blur);
                if (camera.dof_f_stop > 0.f) {
                    writer.key("f_stop");
                    writer.number(camera.dof_f_stop);
                }
                writer.key("iris_blades");
                writer.integer(camera.dof_iris_blades);
                writer.key("highlight_gain");
                writer.number(camera.dof_highlight_gain);
                writer.endObject();
            }
            writer.endObject();
        }

        void writeCameraSamples(detail::Writer& writer, const std::vector<CameraSamplePlan>& samples) {
            writer.beginArray();
            for (const auto& sample : samples) {
                writer.item();
                writer.beginObject();
                writer.key("frame");
                writer.exactNumber(sample.frame);
                writer.key("camera");
                writeCamera(writer, sample.camera);
                writer.endObject();
            }
            writer.endArray();
        }


    }// namespace

    std::string renderPlanEmissionProblem(const chronon3d::render_plan::RenderPlan& plan) {

        return detail::render_plan_emission_problem(plan);
    }

    std::string renderPlanToJson(const chronon3d::render_plan::RenderPlan& plan,
                                 const RenderPlanEmissionOptions& options) {

        const std::string problem = detail::render_plan_emission_problem(plan);
        if (!problem.empty()) throw std::invalid_argument("renderPlanToJson: " + problem);

        detail::Writer writer(options.indentSpaces);

        writer.beginObject();

        writer.key("schema");
        writer.text(plan.schema);
        writer.key("version");
        writer.integer(plan.schema == chronon3d::render_plan::kRenderPlanSchemaV3 ? 3 : 2);
        writer.key("job_id");
        writer.text(plan.job_id);

        writer.key("canvas");
        writer.beginObject();
        writer.key("width");
        writer.integer(plan.canvas.width);
        writer.key("height");
        writer.integer(plan.canvas.height);
        writer.key("fps_num");
        writer.integer(plan.canvas.fps.numerator);
        writer.key("fps_den");
        writer.integer(plan.canvas.fps.denominator);
        writer.key("duration_frames");
        writer.integer(plan.canvas.duration.integral());
        writer.endObject();

        // Only when the caller expressed one: the tree's default is "no opinion",
        // and writing it would lower a resolver ceiling nobody asked to lower.
        const RenderBudget defaultBudget{};
        if (plan.budget.max_temporal_pixels != defaultBudget.max_temporal_pixels) {
            writer.key("budget");
            writer.beginObject();
            writer.key("max_temporal_pixels");
            writer.unsignedInteger(plan.budget.max_temporal_pixels);
            writer.endObject();
        }

        if (plan.temporal_motion_blur) {
            writer.key("temporal_motion_blur");
            writer.beginObject();
            writer.key("shutter_angle");
            writer.number(plan.temporal_motion_blur->shutter_angle);
            writer.key("samples");
            writer.unsignedInteger(plan.temporal_motion_blur->samples);
            writer.endObject();
        }

        if (plan.camera) {
            writer.key("camera");
            writeCamera(writer, *plan.camera);
        }
        if (plan.camera_samples) {
            writer.key("camera_samples");
            writeCameraSamples(writer, *plan.camera_samples);
        }

        if (plan.lighting_rig) {
            writer.key("lighting_rig");
            writer.beginObject();
            writer.key("key_direction");
            writeFloats(writer, plan.lighting_rig->key_direction.data(), plan.lighting_rig->key_direction.size());
            writer.key("key_color");
            writeFloats(writer, plan.lighting_rig->key_color.data(), plan.lighting_rig->key_color.size());
            writer.key("key_intensity");
            writer.number(plan.lighting_rig->key_intensity);
            writer.key("ambient_color");
            writeFloats(writer, plan.lighting_rig->ambient_color.data(), plan.lighting_rig->ambient_color.size());
            writer.key("ambient_intensity");
            writer.number(plan.lighting_rig->ambient_intensity);
            writer.key("rim_enabled");
            writer.boolean(plan.lighting_rig->rim_enabled);
            writer.key("rim_color");
            writeFloats(writer, plan.lighting_rig->rim_color.data(), plan.lighting_rig->rim_color.size());
            writer.key("rim_intensity");
            writer.number(plan.lighting_rig->rim_intensity);
            writer.key("rim_power");
            writer.number(plan.lighting_rig->rim_power);
            writer.endObject();
        }

        if (plan.environment_light) {
            writer.key("environment_light");
            writer.beginObject();
            writer.key("probe_asset");
            writer.text(plan.environment_light->probe_asset);
            writer.key("intensity");
            writer.number(plan.environment_light->intensity);
            writer.key("rotation_y");
            writer.number(plan.environment_light->rotation_y);
            writer.key("irradiance_sh");
            writeFloats(writer, plan.environment_light->irradiance_sh.data(),
                        plan.environment_light->irradiance_sh.size());
            if (plan.environment_light->rotation_animation) {
                const AnimationTrackPlan& track = *plan.environment_light->rotation_animation;
                writer.key("rotation_animation");
                writer.beginObject();
                writer.key("property"); writer.text(track.property);
                writer.key("easing"); writer.text(track.easing);
                writer.key("keyframes"); writeKeyframes(writer, track.keyframes, false);
                writer.endObject();
            }
            writer.endObject();
        }

        writer.key("layers");
        writer.beginArray();
        for (const LayerPlan& layer : plan.layers) {
            writer.item();
            writeLayer(writer, layer, plan.schema);
        }
        writer.endArray();

        writer.key("output");
        writer.beginObject();
        writer.key("path");
        writer.text(plan.output.path);
        writer.key("format");
        writer.token(outputFormatName(plan.output.format));
        writer.endObject();

        writer.endObject();

        return writer.take();
    }

}// namespace chrononmotion::motion
