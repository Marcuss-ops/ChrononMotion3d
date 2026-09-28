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

#include "JsonWriter.hpp"

#include <array>
#include <cstddef>
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
        using chronon3d::render_plan::CameraPropertyPlan;
        using chronon3d::render_plan::CameraTrackPlan;
        using chronon3d::render_plan::FitMode;
        using chronon3d::render_plan::LayerPlan;
        using chronon3d::render_plan::LayerStylePlan;
        using chronon3d::render_plan::LayerType;
        using chronon3d::render_plan::OutputFormat;
        using chronon3d::render_plan::RenderBudget;
        using chronon3d::render_plan::RenderPlan;

        // ---------------------------------------------------------------- names

        /// The easing vocabulary the plan contract defines. An unknown name is a
        /// write-time problem, not something to hand to the decoder to discover.
        [[nodiscard]] bool isKnownEasing(const std::string& easing) {

            static constexpr std::array<std::string_view, 24> known{
                    "linear",
                    "in_quad", "out_quad", "in_out_quad",
                    "in_cubic", "out_cubic", "in_out_cubic",
                    "in_expo", "out_expo", "in_out_expo",
                    "in_sine", "out_sine", "in_out_sine",
                    "in_back", "out_back", "in_out_back",
                    "in_elastic", "out_elastic", "in_out_elastic",
                    "in_bounce", "out_bounce", "in_out_bounce",
                    "smoothstep", "hold"};
            for (const std::string_view candidate : known) {
                if (candidate == easing) return true;
            }
            return false;
        }

        [[nodiscard]] const char* layerTypeName(LayerType type) {

            switch (type) {
                case LayerType::Image: return "image";
                case LayerType::Video: return "video";
                case LayerType::Text: return "text";
                case LayerType::Color: return "color";
                case LayerType::Shape: return "shape";
                case LayerType::Particles: return "particles";
                case LayerType::Light: return "light";
                case LayerType::Adjustment: return "adjustment";
                case LayerType::Mesh: return "mesh";
            }
            return "unknown";
        }

        [[nodiscard]] const char* fitName(FitMode fit) {

            switch (fit) {
                case FitMode::Cover: return "cover";
                case FitMode::Contain: return "contain";
                case FitMode::Stretch: return "stretch";
                case FitMode::None: return "none";
            }
            return "none";
        }

        [[nodiscard]] const char* outputFormatName(OutputFormat format) {

            switch (format) {
                case OutputFormat::Png: return "png";
                case OutputFormat::PngSequence: return "png_sequence";
                case OutputFormat::Gif: return "gif";
                case OutputFormat::Mp4: return "mp4";
                case OutputFormat::Mkv: return "mkv";
                case OutputFormat::WebM: return "webm";
            }
            return "png";
        }

        [[nodiscard]] const char* cameraPropertyName(CameraPropertyPlan property) {

            switch (property) {
                case CameraPropertyPlan::PositionX: return "camera_position_x";
                case CameraPropertyPlan::PositionY: return "camera_position_y";
                case CameraPropertyPlan::PositionZ: return "camera_position_z";
                case CameraPropertyPlan::RotationX: return "camera_rotation_x";
                case CameraPropertyPlan::RotationY: return "camera_rotation_y";
                case CameraPropertyPlan::RotationZ: return "camera_rotation_z";
                case CameraPropertyPlan::FovDeg: return "camera_fov_deg";
                case CameraPropertyPlan::Zoom: return "camera_zoom";
            }
            return "camera_position_z";
        }

        [[nodiscard]] std::string layerOwner(const LayerPlan& layer) {

            return layer.id.empty() ? std::string("a layer") : "layer '" + layer.id + "'";
        }

        // ------------------------------------------------- what cannot be written

        /// Every reason a text style cannot be written down, in the order a
        /// reader would look for them.
        [[nodiscard]] std::string styleProblem(const LayerPlan& layer) {

            if (!layer.style) return {};
            const LayerStylePlan& style = *layer.style;
            const std::string owner = layerOwner(layer);

            if (style.text_fit_mode != "none") {
                return owner + " carries style text_fit_mode '" + style.text_fit_mode +
                       "', which this emitter does not write";
            }
            if (style.min_font_size || style.max_font_size) {
                return owner + " carries an explicit font-size range, which this emitter does not write";
            }
            if (style.stroke) return owner + " carries a text stroke, which this emitter does not write";
            if (style.shadow || !style.shadows.empty()) {
                return owner + " carries a text shadow, which this emitter does not write";
            }
            if (style.backdrop) return owner + " carries a backdrop blur, which this emitter does not write";
            if (style.background) return owner + " carries a text background, which this emitter does not write";
            return {};
        }

        /// Everything a track may carry that the lowering bakes away instead of
        /// publishing: a baked track is linear samples with no per-key timing and
        /// no driver, so one of these showing up means the plan came from
        /// somewhere this writer has not been taught.
        [[nodiscard]] std::string trackProblem(const std::string& owner, const AnimationTrackPlan& track) {

            if (!isKnownEasing(track.easing)) {
                return owner + " declares easing '" + track.easing + "', which the plan contract does not define";
            }
            if (track.bezier_curve) return owner + " carries a track-level bezier curve";
            if (track.spring) return owner + " carries a track spring";
            if (track.loop_mode != "none") return owner + " carries loop mode '" + track.loop_mode + "'";
            if (track.wiggle || track.oscillate || track.random_hold || track.posterize_time ||
                track.data_feed || track.audio_react) {
                return owner + " carries an animation driver, which the lowering bakes into samples";
            }
            for (const AnimationKeyframePlan& key : track.keyframes) {
                if (key.easing || key.interpolation || key.easing_xy || key.bezier_curve || key.spring ||
                    key.roving || key.temporal_in_dx || key.temporal_in_dy ||
                    key.temporal_out_dx || key.temporal_out_dy) {
                    return owner + " carries per-key timing overrides, which the lowering bakes into samples";
                }
            }
            return {};
        }

        [[nodiscard]] std::string animationProblem(const std::string& owner, const AnimationPlan& animation) {

            for (const AnimationTrackPlan& track : animation.tracks) {
                const std::string problem = trackProblem(owner + " track '" + track.property + "'", track);
                if (!problem.empty()) return problem;
            }
            return {};
        }

        [[nodiscard]] std::string layerProblem(const LayerPlan& layer) {

            if (layer.id.empty()) return "a layer has no id";

            const std::string owner = layerOwner(layer);

            switch (layer.type) {
                case LayerType::Color:
                case LayerType::Image:
                case LayerType::Video:
                case LayerType::Text:
                    break;
                default:
                    return owner + " is a '" + layerTypeName(layer.type) +
                           "' layer, which this emitter does not write";
            }

            if (layer.type == LayerType::Image && layer.asset.empty()) {
                return owner + " is an image layer with no asset";
            }
            if (layer.type == LayerType::Video && layer.source.empty()) {
                return owner + " is a video layer with no source";
            }
            if (layer.type == LayerType::Text && !layer.style) {
                return owner + " is a text layer with no style, which the plan contract requires";
            }

            if (layer.parent) return owner + " is parented; the plan contract already flattens hierarchy";
            if (layer.track_matte) return owner + " carries a track matte";
            if (layer.time_remap) return owner + " carries a time remap";
            if (!layer.text_spans.empty()) return owner + " carries text span overrides";
            if (!layer.font.empty()) {
                return owner + " carries a normalized font path, which the plan derives from style.font";
            }
            if (layer.motion_blur) return owner + " carries analytic motion blur";
            if (layer.shape) return owner + " carries shape content";
            if (layer.inline_mesh) return owner + " carries an inline mesh";
            if (layer.particle_emitter) return owner + " carries a particle emitter";
            if (layer.light) return owner + " carries a light layer";
            if (layer.path_mask || !layer.masks.empty()) return owner + " carries a mask";
            if (!layer.effects.empty() || !layer.effect_param_tracks.empty()) return owner + " carries an effect";
            if (layer.flow_layout || layer.grid_layout) return owner + " carries a layout group";
            if (layer.follow_path) return owner + " carries a follow-path driver";
            // The plan contract has no layer-level auto-orient: `auto_orient`
            // exists only inside a follow-path driver, which this emitter
            // already refuses above. Writing it on the layer itself made every
            // emitted plan fail the consumer's schema (additionalProperties:
            // false), so a Motion scene could be lowered and never rendered.
            if (layer.auto_orient) return owner + " carries an auto-orient flag, which the plan expresses only inside a follow-path driver";
            if (layer.skew_angle_deg != 0.f || layer.skew_axis_deg != 0.f) return owner + " carries a skew";
            if (layer.transition_in || layer.transition_out) return owner + " carries a transition";
            if (layer.fill_gradient_animation) return owner + " carries a gradient animation";
            if (!layer.text_animators.empty()) return owner + " carries text animators";
            if (layer.blend_mode) return owner + " carries a blend mode, which this emitter does not write";
            if (layer.safe_insets) return owner + " carries safe insets";
            if (layer.size_dimensions == 3) {
                return owner + " has a three-component size, which the plan contract cannot express";
            }
            if (layer.position_dimensions != 0 && layer.position_dimensions < 2) {
                return owner + " has a single-component position, which the plan contract cannot express";
            }
            if (layer.scale_dimensions == 1) {
                return owner + " has a single-component scale, which the plan contract cannot express";
            }

            const std::string style = styleProblem(layer);
            if (!style.empty()) return style;

            if (layer.animation) {
                const std::string animation = animationProblem(owner + " animation", *layer.animation);
                if (!animation.empty()) return animation;
            }
            return {};
        }

        /// The budget fields a plan can carry. `max_temporal_pixels` has a schema
        /// field; the rest are resolution ceilings with no plan representation, so
        /// a caller that lowered them has nowhere to put them.
        [[nodiscard]] std::string budgetProblem(const RenderPlan& plan) {

            const RenderBudget defaults{};
            const RenderBudget& budget = plan.budget;
            const bool lowered = budget.max_width != defaults.max_width ||
                                 budget.max_height != defaults.max_height ||
                                 budget.max_total_pixels != defaults.max_total_pixels ||
                                 budget.max_frames != defaults.max_frames ||
                                 budget.max_layers != defaults.max_layers ||
                                 budget.max_text_bytes != defaults.max_text_bytes ||
                                 budget.max_asset_reference_bytes != defaults.max_asset_reference_bytes ||
                                 budget.max_estimated_output_bytes != defaults.max_estimated_output_bytes ||
                                 budget.max_peak_memory_bytes != defaults.max_peak_memory_bytes;
            if (lowered) {
                return "the plan carries a render budget beyond max_temporal_pixels, which the plan contract "
                       "cannot express";
            }
            return {};
        }

        [[nodiscard]] std::string planProblem(const RenderPlan& plan) {

            if (plan.job_id.empty()) return "the plan has no job id";
            if (plan.canvas.width <= 0 || plan.canvas.height <= 0) return "the plan has an empty canvas";
            if (plan.canvas.duration.integral() <= 0) return "the plan has no duration";
            if (plan.output.path.empty()) return "the plan has no output path";
            if (plan.schema != chronon3d::render_plan::kRenderPlanSchemaV2 &&
                plan.schema != chronon3d::render_plan::kRenderPlanSchemaV3) {
                return "the plan declares schema '" + plan.schema + "', which this emitter does not write";
            }
            if (plan.content_fingerprint != 0) {
                return "the plan carries a content fingerprint, which the plan contract cannot express";
            }
            if (!plan.warnings.empty()) {
                return "the plan carries warnings, which the plan contract cannot express";
            }
            if (plan.temporal_motion_blur) return "the plan carries scene motion blur";
            if (plan.camera_shake) return "the plan carries camera shake";
            if (plan.lighting_rig) return "the plan carries a lighting rig";
            if (plan.audio) return "the plan carries an audio mix";
            if (plan.prepared_overlay_package) return "the plan carries a prepared overlay package";

            const std::string budget = budgetProblem(plan);
            if (!budget.empty()) return budget;

            if (plan.camera_animation) {
                for (const CameraTrackPlan& track : *plan.camera_animation) {
                    if (!isKnownEasing(track.easing)) {
                        return "camera track '" + std::string(cameraPropertyName(track.property)) +
                               "' declares easing '" + track.easing +
                               "', which the plan contract does not define";
                    }
                    if (track.bezier_curve) return "a camera track carries a bezier curve";
                    for (const AnimationKeyframePlan& key : track.keyframes) {
                        if (key.easing || key.interpolation || key.easing_xy || key.bezier_curve || key.spring ||
                            key.roving || key.temporal_in_dx || key.temporal_in_dy ||
                            key.temporal_out_dx || key.temporal_out_dy) {
                            return "a camera track carries per-key timing overrides";
                        }
                    }
                }
            }

            for (const LayerPlan& layer : plan.layers) {
                const std::string problem = layerProblem(layer);
                if (!problem.empty()) return problem;
            }

            if (plan.output.codec != chronon3d::render_plan::VideoCodec::Auto ||
                plan.output.rate_control != chronon3d::render_plan::RateControlMode::Crf ||
                plan.output.bitrate != 0 || plan.output.crf != 0 || plan.output.qp != -1 ||
                !plan.output.profile_id.empty()) {
                return "the plan carries encoder settings, which belong to the render invocation";
            }
            return {};
        }

        // ---------------------------------------------------------------- writes

        void writeFloats(detail::Writer& writer, const float* values, std::size_t count) {

            writer.beginArray();
            for (std::size_t i = 0; i < count; ++i) {
                writer.item();
                writer.number(values[i]);
            }
            writer.endArray();
        }

        template<std::size_t N>
        void writeFloats(detail::Writer& writer, const std::array<float, N>& values, std::size_t count) {

            writeFloats(writer, values.data(), count);
        }

        void writeKeyframes(detail::Writer& writer, const std::vector<AnimationKeyframePlan>& keyframes,
                            bool scalar) {

            writer.beginArray();
            for (const AnimationKeyframePlan& key : keyframes) {
                writer.item();
                writer.beginObject();
                writer.key("frame");
                writer.integer(key.frame.integral());
                writer.key("value");
                if (scalar) {
                    // A camera channel is one number; a layer channel is the
                    // property's own arity. An empty value is the channel's zero,
                    // which is what the decoder would have produced for it.
                    writer.number(key.value.empty() ? 0.f : key.value.front());
                } else {
                    writeFloats(writer, key.value.data(), key.value.size());
                }
                writer.endObject();
            }
            writer.endArray();
        }

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

        void writeStyle(detail::Writer& writer, const LayerStylePlan& style) {

            writer.beginObject();
            writer.key("font");
            writer.text(style.font);
            if (style.font_size) {
                writer.key("font_size");
                writer.number(*style.font_size);
            }
            writer.key("fill");
            writer.text(style.fill);
            if (style.glow) {
                writer.key("glow");
                writer.beginObject();
                writer.key("radius");
                writer.number(style.glow->radius);
                writer.key("intensity");
                writer.number(style.glow->intensity);
                writer.key("color");
                writer.text(style.glow->color);
                writer.endObject();
            }
            writer.endObject();
        }

        void writeLayer(detail::Writer& writer, const LayerPlan& layer) {

            writer.beginObject();

            writer.key("id");
            writer.text(layer.id);
            writer.key("type");
            writer.token(layerTypeName(layer.type));

            if (!layer.asset.empty()) {
                writer.key("asset");
                writer.text(layer.asset);
            }
            if (!layer.source.empty()) {
                writer.key("source");
                writer.text(layer.source);
            }

            writer.key("color");
            writeFloats(writer, layer.color, layer.color.size());

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
            if (layer.radius) {
                writer.key("radius");
                writer.number(*layer.radius);
            }
            if (layer.fit) {
                writer.key("fit");
                writer.token(fitName(*layer.fit));
            }

            if (layer.type == LayerType::Text) {
                writer.key("text");
                writer.text(layer.text);
                writer.key("style");
                writeStyle(writer, *layer.style);
            }
            if (layer.animation) {
                writer.key("animation");
                writeAnimation(writer, *layer.animation);
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
            writer.endObject();
        }

        void writeCameraAnimation(detail::Writer& writer, const std::vector<CameraTrackPlan>& tracks) {

            writer.beginObject();
            writer.key("tracks");
            writer.beginArray();
            for (const CameraTrackPlan& track : tracks) {
                writer.item();
                writer.beginObject();
                writer.key("property");
                writer.token(cameraPropertyName(track.property));
                writer.key("easing");
                writer.text(track.easing);
                writer.key("keyframes");
                writeKeyframes(writer, track.keyframes, true);
                writer.endObject();
            }
            writer.endArray();
            writer.endObject();
        }

    }// namespace

    std::string renderPlanEmissionProblem(const chronon3d::render_plan::RenderPlan& plan) {

        return planProblem(plan);
    }

    std::string renderPlanToJson(const chronon3d::render_plan::RenderPlan& plan,
                                 const RenderPlanEmissionOptions& options) {

        const std::string problem = planProblem(plan);
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

        if (plan.camera) {
            writer.key("camera");
            writeCamera(writer, *plan.camera);
        }
        if (plan.camera_animation) {
            writer.key("camera_animation");
            writeCameraAnimation(writer, *plan.camera_animation);
        }

        writer.key("layers");
        writer.beginArray();
        for (const LayerPlan& layer : plan.layers) {
            writer.item();
            writeLayer(writer, layer);
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
