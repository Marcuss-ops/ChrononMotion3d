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
        using chronon3d::render_plan::CameraPropertyPlan;
        using chronon3d::render_plan::CameraTrackPlan;
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



        void writeTextSpans(detail::Writer& writer,
                            const std::vector<chronon3d::TextSpanOverride>& spans) {
            writer.beginArray();
            for (const auto& span : spans) {
                writer.item();
                writer.beginObject();
                writer.key("start"); writer.unsignedInteger(span.byte_start);
                writer.key("end"); writer.unsignedInteger(span.byte_end);
                if (span.semantic_id) { writer.key("semantic_id"); writer.text(*span.semantic_id); }
                const bool hasStyle = span.color || span.font_size_multiplier || span.tracking ||
                                      span.stroke || span.highlight;
                if (hasStyle) { writer.key("style"); writer.beginObject(); }
                if (span.color) {
                    writer.key("color");
                    writer.text(*textSpanColorHex(*span.color));
                }
                if (span.font_size_multiplier) { writer.key("font_size_mult"); writer.number(*span.font_size_multiplier); }
                if (span.tracking) { writer.key("tracking"); writer.number(*span.tracking); }
                if (span.stroke) {
                    writer.key("stroke"); writer.beginObject();
                    if (span.stroke->color) {
                        writer.key("color"); writer.text(*textSpanColorHex(*span.stroke->color));
                    }
                    writer.key("width_em"); writer.number(span.stroke->width_em);
                    writer.endObject();
                }
                if (span.highlight) {
                    writer.key("highlight"); writer.beginObject();
                    if (span.highlight->background) {
                        writer.key("background"); writer.text(*textSpanColorHex(*span.highlight->background));
                    }
                    if (span.highlight->padding_em) { writer.key("padding_em"); writer.number(*span.highlight->padding_em); }
                    if (span.highlight->radius_em) { writer.key("radius_em"); writer.number(*span.highlight->radius_em); }
                    writer.endObject();
                }
                if (hasStyle) writer.endObject();
                writer.endObject();
            }
            writer.endArray();
        }

        void writeTextSelectorTrack(detail::Writer& writer, const AnimationTrackPlan& track) {
            writer.beginObject();
            writer.key("easing");
            writer.text(track.easing);
            writer.key("keyframes");
            writeKeyframes(writer, track.keyframes, true);
            writer.endObject();
        }

        void writePathCommands(detail::Writer& writer, const std::vector<ShapePathCommandPlan>& commands);
        void writeField(detail::Writer& writer, const chronon3d::graphics::Field2D& field);
        void writeGradientDefinition(detail::Writer& writer,
                                     const chronon3d::graphics::GradientDefinition& gradient);

        void writeMaskTrack(detail::Writer& writer, const AnimationTrackPlan& track) {
            writer.beginObject();
            writer.key("easing"); writer.text(track.easing);
            writer.key("keyframes"); writeKeyframes(writer, track.keyframes, true);
            writer.endObject();
        }

        template<class Tracks>
        void writeEffectParameterTracks(detail::Writer& writer, const Tracks& tracks) {
            writer.beginArray();
            for (const auto& item : tracks) {
                writer.item(); writer.beginObject();
                writer.key("effect_id"); writer.text(item.effect_id);
                writer.key("param"); writer.text(item.param);
                writer.key("easing"); writer.text(item.track.easing);
                writer.key("keyframes"); writeKeyframes(writer, item.track.keyframes, true);
                writer.endObject();
            }
            writer.endArray();
        }

        void writeMask(detail::Writer& writer, const LayerPlan::PathMaskPlan& mask,
                       bool legacyPathMask) {
            writer.beginObject();
            writer.key("type"); writer.text(mask.type);
            if (!legacyPathMask) { writer.key("mode"); writer.text(mask.mode); }
            if (mask.type == "text") { writer.key("source"); writer.text(mask.text_source); }
            if (!mask.path.empty()) { writer.key("path"); writePathCommands(writer, mask.path); }
            if (!mask.morph_target.empty()) {
                writer.key("target_path"); writePathCommands(writer, mask.morph_target);
                if (mask.morph_animation) { writer.key("animation"); writeMaskTrack(writer, *mask.morph_animation); }
            }
            if (mask.field) { writer.key("field"); writeField(writer, *mask.field); }
            if (mask.type != "path" && mask.type != "text") {
                const std::array<float, 2> position{mask.position[0], mask.position[1]};
                writer.key("position"); writeFloats(writer, position, position.size());
                const std::array<float, 2> size{mask.size[0], mask.size[1]};
                writer.key("size"); writeFloats(writer, size, size.size());
            }
            if (mask.type == "rounded_rect") { writer.key("radius"); writer.number(mask.radius); }
            if (mask.feather != 0.f) { writer.key("feather"); writer.number(mask.feather); }
            if (mask.expansion != 0.f) { writer.key("expansion"); writer.number(mask.expansion); }
            if (mask.opacity != 1.f) { writer.key("opacity"); writer.number(mask.opacity); }
            if (mask.feather_track) { writer.key("feather_track"); writeMaskTrack(writer, *mask.feather_track); }
            if (mask.expansion_track) { writer.key("expansion_track"); writeMaskTrack(writer, *mask.expansion_track); }
            if (mask.opacity_track) { writer.key("opacity_track"); writeMaskTrack(writer, *mask.opacity_track); }
            if (mask.inverted) { writer.key("inverted"); writer.boolean(true); }
            writer.endObject();
        }

        void writeTrackMatte(detail::Writer& writer,
                             const chronon3d::render_plan::TrackMattePlan& matte) {
            const char* type = "alpha";
            switch (matte.type) {
                case chronon3d::render_plan::TrackMatteTypePlan::Alpha: type = "alpha"; break;
                case chronon3d::render_plan::TrackMatteTypePlan::AlphaInverted: type = "alpha_inverted"; break;
                case chronon3d::render_plan::TrackMatteTypePlan::Luma: type = "luma"; break;
                case chronon3d::render_plan::TrackMatteTypePlan::LumaInverted: type = "luma_inverted"; break;
            }
            writer.beginObject(); writer.key("source"); writer.text(matte.source);
            writer.key("type"); writer.token(type); writer.endObject();
        }

        void writeLayerMasks(detail::Writer& writer, const LayerPlan& layer) {
            if (layer.path_mask) {
                writer.key("mask"); writeMask(writer, *layer.path_mask, true);
            }
            if (!layer.masks.empty()) {
                writer.key("masks"); writer.beginArray();
                for (const auto& mask : layer.masks) { writer.item(); writeMask(writer, mask, false); }
                writer.endArray();
            }
        }

        void writeTextAnimators(detail::Writer& writer,
                                const std::vector<chronon3d::render_plan::TextAnimatorPlan>& animators) {
            writer.beginArray();
            for (const auto& animator : animators) {
                writer.item();
                writer.beginObject();
                if (!animator.id.empty()) {
                    writer.key("id");
                    writer.text(animator.id);
                }
                writer.key("selectors");
                writer.beginArray();
                for (const auto& selector : animator.selectors) {
                    writer.item();
                    writer.beginObject();
                    if (!selector.id.empty()) { writer.key("id"); writer.text(selector.id); }
                    if (selector.semantic_id) { writer.key("semantic_id"); writer.text(*selector.semantic_id); }
                    writer.key("unit"); writer.text(selector.unit);
                    writer.key("shape"); writer.text(selector.shape);
                    writer.key("order"); writer.text(selector.order);
                    writer.key("combine"); writer.text(selector.combine);
                    if (!selector.start.keyframes.empty()) {
                        writer.key("start"); writeTextSelectorTrack(writer, selector.start);
                    }
                    if (!selector.end.keyframes.empty()) {
                        writer.key("end"); writeTextSelectorTrack(writer, selector.end);
                    }
                    if (!selector.offset.keyframes.empty()) {
                        writer.key("offset"); writeTextSelectorTrack(writer, selector.offset);
                    }
                    if (!selector.amount.keyframes.empty()) {
                        writer.key("amount"); writeTextSelectorTrack(writer, selector.amount);
                    }
                    writer.key("exclude_spaces"); writer.boolean(selector.exclude_spaces);
                    writer.key("randomize_order"); writer.boolean(selector.randomize_order);
                    if (selector.random_seed != 0) { writer.key("random_seed"); writer.unsignedInteger(selector.random_seed); }
                    if (selector.shape == "wiggly") {
                        writer.key("wiggly_frequency"); writer.number(selector.wiggly_frequency);
                        writer.key("wiggly_amplitude"); writer.number(selector.wiggly_amplitude);
                        if (selector.wiggly_seed != 0) { writer.key("wiggly_seed"); writer.unsignedInteger(selector.wiggly_seed); }
                    }
                    if (selector.shape == "noise") {
                        writer.key("noise_frequency"); writer.number(selector.noise_frequency);
                        writer.key("noise_amplitude"); writer.number(selector.noise_amplitude);
                        if (selector.noise_seed != 0) { writer.key("noise_seed"); writer.unsignedInteger(selector.noise_seed); }
                    }
                    if (selector.order == "index") { writer.key("index_step"); writer.unsignedInteger(selector.index_step); }
                    writer.endObject();
                }
                writer.endArray();
                writer.key("properties");
                writer.beginArray();
                for (const auto& track : animator.properties) {
                    writer.item();
                    writer.beginObject();
                    writer.key("property"); writer.text(track.property);
                    writer.key("easing"); writer.text(track.easing);
                    writer.key("keyframes");
                    writeKeyframes(writer, track.keyframes, false);
                    writer.endObject();
                }
                writer.endArray();
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

        void writeMediaStyle(detail::Writer& writer, const LayerStylePlan& style) {
            writer.beginObject();
            if (style.background) {
                const auto& background = *style.background;
                writer.key("background");
                writer.beginObject();
                writer.key("color"); writer.text(background.color);
                if (background.opacity) { writer.key("opacity"); writer.number(*background.opacity); }
                if (background.radius) { writer.key("radius"); writer.number(*background.radius); }
                writer.key("padding");
                writeFloats(writer, background.padding, background.padding_dimensions);
                writer.endObject();
            }
            writer.endObject();
        }

        void writeEffects(detail::Writer& writer, const std::vector<LayerPlan::EffectPlan>& effects) {
            writer.beginArray();
            for (const auto& effect : effects) {
                writer.item();
                writer.beginObject();
                switch (effect.kind) {
                    case LayerPlan::EffectKindPlan::Catalog:
                        writer.key("type"); writer.token("catalog");
                        writer.key("effect_id"); writer.text(effect.canonical_effect_id);
                        writer.key("params"); writer.beginArray();
                        for (const auto& param : effect.canonical_params) {
                            writer.item(); writer.beginObject();
                            writer.key("param"); writer.text(param.param);
                            writer.key("value"); writer.number(param.value);
                            writer.endObject();
                        }
                        writer.endArray();
                        break;
                    case LayerPlan::EffectKindPlan::Noise:
                        writer.key("type"); writer.token("noise");
                        writer.key("amount"); writer.number(effect.amount);
                        writer.key("seed"); writer.unsignedInteger(effect.seed);
                        if (effect.animated) { writer.key("animated"); writer.boolean(true); }
                        if (effect.noise_size != 1.f) { writer.key("size"); writer.number(effect.noise_size); }
                        if (effect.grain_shadow_response != 1.f) { writer.key("shadow_response"); writer.number(effect.grain_shadow_response); }
                        if (effect.grain_midtone_response != 1.f) { writer.key("midtone_response"); writer.number(effect.grain_midtone_response); }
                        if (effect.grain_highlight_response != 1.f) { writer.key("highlight_response"); writer.number(effect.grain_highlight_response); }
                        if (effect.rgb_noise) { writer.key("color_mode"); writer.token("rgb"); }
                        break;
                    case LayerPlan::EffectKindPlan::Vignette:
                        writer.key("type"); writer.token("vignette");
                        writer.key("amount"); writer.number(effect.amount);
                        writer.key("radius"); writer.number(effect.radius);
                        writer.key("softness"); writer.number(effect.softness);
                        writer.key("color"); writeFloats(writer, effect.effect_color, effect.effect_color.size());
                        break;
                    case LayerPlan::EffectKindPlan::Glow:
                        writer.key("type"); writer.token("glow");
                        writer.key("radius"); writer.number(effect.radius);
                        writer.key("intensity"); writer.number(effect.glow_intensity);
                        writer.key("color"); writeFloats(writer, effect.effect_color, effect.effect_color.size());
                        break;
                    case LayerPlan::EffectKindPlan::Bloom:
                        writer.key("type"); writer.token("bloom");
                        writer.key("threshold"); writer.number(effect.bloom_threshold);
                        writer.key("radius"); writer.number(effect.radius);
                        writer.key("intensity"); writer.number(effect.bloom_intensity);
                        break;
                    default:
                        throw std::invalid_argument("writeEffects: effect was not validated");
                }
                writer.endObject();
            }
            writer.endArray();
        }

        void writeInlineMesh(detail::Writer& writer, const InlineMeshPlan& mesh) {

            writer.beginObject();
            writer.key("vertices");
            writer.beginArray();
            for (const auto& vertex : mesh.vertices) {
                writer.item();
                writer.beginObject();
                writer.key("position");
                writeFloats(writer, vertex.position, vertex.position.size());
                writer.key("normal");
                writeFloats(writer, vertex.normal, vertex.normal.size());
                writer.key("uv");
                writeFloats(writer, vertex.uv, vertex.uv.size());
                writer.endObject();
            }
            writer.endArray();
            writer.key("indices");
            writer.beginArray();
            for (const std::uint32_t index : mesh.indices) {
                writer.item();
                writer.unsignedInteger(index);
            }
            writer.endArray();
            if (!mesh.animation.empty()) {
                writer.key("animation");
                writer.beginArray();
                for (const auto& sample : mesh.animation) {
                    writer.item();
                    writer.beginObject();
                    writer.key("frame");
                    writer.integer(sample.frame);
                    writer.key("positions");
                    writer.beginArray();
                    for (const auto& position : sample.positions) {
                        writer.item();
                        writeFloats(writer, position, position.size());
                    }
                    writer.endArray();
                    writer.endObject();
                }
                writer.endArray();
            }
            if (mesh.material) {
                const auto& material = *mesh.material;
                writer.key("material");
                writer.beginObject();
                writer.key("diffuse");
                writer.number(material.diffuse);
                writer.key("specular");
                writer.number(material.specular);
                writer.key("shininess");
                writer.number(material.shininess);
                writer.key("rim_intensity");
                writer.number(material.rim_intensity);
                if (material.base_color) {
                    writer.key("base_color");
                    writeFloats(writer, *material.base_color, material.base_color->size());
                }
                writer.endObject();
            }
            writer.endObject();
        }

        [[nodiscard]] const char* pathCommandTypeName(ShapePathCommandTypePlan type) {
            switch (type) {
                case ShapePathCommandTypePlan::MoveTo: return "move_to";
                case ShapePathCommandTypePlan::LineTo: return "line_to";
                case ShapePathCommandTypePlan::QuadraticTo: return "quadratic_to";
                case ShapePathCommandTypePlan::CubicTo: return "cubic_to";
                case ShapePathCommandTypePlan::Close: return "close";
            }
            return "close";
        }

        void writePathCommands(detail::Writer& writer, const std::vector<ShapePathCommandPlan>& commands) {
            writer.beginArray();
            for (const auto& command : commands) {
                writer.item();
                writer.beginObject();
                writer.key("type");
                writer.token(pathCommandTypeName(command.type));
                if (command.type != ShapePathCommandTypePlan::Close) {
                    writer.key("point");
                    writeFloats(writer, command.point, command.point.size());
                }
                if (command.type == ShapePathCommandTypePlan::QuadraticTo ||
                    command.type == ShapePathCommandTypePlan::CubicTo) {
                    writer.key("control1");
                    writeFloats(writer, command.control1, command.control1.size());
                }
                if (command.type == ShapePathCommandTypePlan::CubicTo) {
                    writer.key("control2");
                    writeFloats(writer, command.control2, command.control2.size());
                }
                writer.endObject();
            }
            writer.endArray();
        }

        void writeStroke(detail::Writer& writer, const chronon3d::render_plan::StrokeStyle& stroke) {
            writer.beginObject();
            writer.key("color");
            if (stroke.solid_color) writeFloats(writer, *stroke.solid_color, stroke.solid_color->size());
            else writer.text(stroke.color);
            if (stroke.width) { writer.key("width"); writer.number(*stroke.width); }
            if (stroke.trim_start) { writer.key("trim_start"); writer.number(*stroke.trim_start); }
            if (stroke.trim_end) { writer.key("trim_end"); writer.number(*stroke.trim_end); }
            if (!stroke.dash_array.empty()) {
                writer.key("dash_array");
                writeFloats(writer, stroke.dash_array.data(), stroke.dash_array.size());
            }
            if (stroke.dash_offset != 0.f) { writer.key("dash_offset"); writer.number(stroke.dash_offset); }
            if (stroke.cap != "butt") { writer.key("cap"); writer.text(stroke.cap); }
            if (stroke.join != "miter") { writer.key("join"); writer.text(stroke.join); }
            if (stroke.taper_start != 1.f) { writer.key("taper_start"); writer.number(stroke.taper_start); }
            if (stroke.taper_end != 1.f) { writer.key("taper_end"); writer.number(stroke.taper_end); }
            writer.endObject();
        }

        void writeTrimOperator(detail::Writer& writer, const chronon3d::render_plan::PathOperatorPlan& op) {
            writer.beginObject();
            writer.key("kind");
            writer.token("trim");
            writer.key("params");
            writer.beginObject();
            writer.key("start"); writer.number(op.trim_start);
            writer.key("end"); writer.number(op.trim_end);
            if (op.trim_offset != 0.f) { writer.key("offset"); writer.number(op.trim_offset); }
            if (op.trim_animation) {
                writer.key("animation");
                writer.beginObject();
                writer.key("easing"); writer.text(op.trim_animation->easing);
                writer.key("keyframes"); writeKeyframes(writer, op.trim_animation->keyframes, false);
                writer.endObject();
            }
            writer.endObject();
            writer.endObject();
        }

        [[nodiscard]] const char* fieldGeneratorName(chronon3d::graphics::Field2DGenerator generator) {
            using chronon3d::graphics::Field2DGenerator;
            switch (generator) {
                case Field2DGenerator::Perlin: return "perlin";
                case Field2DGenerator::Simplex: return "simplex";
                case Field2DGenerator::Fractal: return "fractal";
                case Field2DGenerator::Cellular: return "cellular";
                case Field2DGenerator::Grid: return "grid";
                case Field2DGenerator::Checker: return "checker";
                case Field2DGenerator::Dots: return "dots";
                case Field2DGenerator::Stripes: return "stripes";
                case Field2DGenerator::Rings: return "rings";
                case Field2DGenerator::Linear: return "linear";
                case Field2DGenerator::Radial: return "radial";
                case Field2DGenerator::Conic: return "conic";
                case Field2DGenerator::Burst: return "burst";
                // Chronon3D's in-memory field vocabulary is wider than the
                // RenderPlan contract. Reject unsupported values explicitly
                // instead of silently serializing them as Perlin.
                case Field2DGenerator::Value:
                case Field2DGenerator::Ridged:
                    throw std::invalid_argument("writeField: field generator is not expressible in RenderPlan");
            }
            return "perlin";
        }

        void writeField(detail::Writer& writer, const chronon3d::graphics::Field2D& field) {
            using chronon3d::graphics::Field2DOperatorKind;
            writer.beginObject();
            writer.key("generator"); writer.token(fieldGeneratorName(field.generator));
            writer.key("seed"); writer.unsignedInteger(field.seed);
            writer.key("frequency"); writer.number(field.frequency);
            if (field.generator == chronon3d::graphics::Field2DGenerator::Fractal ||
                field.generator == chronon3d::graphics::Field2DGenerator::Burst) {
                writer.key("octaves"); writer.unsignedInteger(field.octaves);
            }
            if (field.generator == chronon3d::graphics::Field2DGenerator::Fractal) {
                writer.key("persistence"); writer.number(field.persistence);
                writer.key("lacunarity"); writer.number(field.lacunarity);
            }
            if (!field.operators.empty()) {
                writer.key("operators"); writer.beginArray();
                for (const auto& op : field.operators) {
                    writer.item(); writer.beginObject();
                    switch (op.kind) {
                        case Field2DOperatorKind::Remap:
                            writer.key("kind"); writer.token("remap");
                            writer.key("from"); writer.number(op.from);
                            writer.key("to"); writer.number(op.to);
                            break;
                        case Field2DOperatorKind::Threshold:
                            writer.key("kind"); writer.token("threshold");
                            writer.key("edge"); writer.number(op.edge);
                            break;
                        case Field2DOperatorKind::Smoothstep:
                            writer.key("kind"); writer.token("smoothstep");
                            writer.key("edge"); writer.number(op.edge);
                            writer.key("softness"); writer.number(op.softness);
                            break;
                        case Field2DOperatorKind::Invert:
                            writer.key("kind"); writer.token("invert");
                            break;
                        case Field2DOperatorKind::DomainWarp:
                        case Field2DOperatorKind::Warp:
                            writer.key("kind"); writer.token("warp");
                            writer.key("amount"); writer.number(op.amount);
                            break;
                        default:
                            throw std::invalid_argument("writeField: field operator is not expressible in RenderPlan");
                    }
                    writer.endObject();
                }
                writer.endArray();
            }
            writer.endObject();
        }

        void writeGradientDefinition(detail::Writer& writer,
                                     const chronon3d::graphics::GradientDefinition& gradient) {
            writer.beginObject();
            writer.key("type");
            writer.token(gradient.type == chronon3d::graphics::GradientType::Linear ? "linear" :
                         gradient.type == chronon3d::graphics::GradientType::Radial ? "radial" : "conic");
            writer.key("spread");
            writer.token(gradient.spread == chronon3d::graphics::GradientSpread::Repeat ? "repeat" :
                         gradient.spread == chronon3d::graphics::GradientSpread::Reflect ? "reflect" : "pad");
            const std::array<float, 2> start{gradient.start.x, gradient.start.y};
            const std::array<float, 2> end{gradient.end.x, gradient.end.y};
            const std::array<float, 2> center{gradient.center.x, gradient.center.y};
            writer.key("start"); writeFloats(writer, start.data(), start.size());
            writer.key("end"); writeFloats(writer, end.data(), end.size());
            writer.key("center"); writeFloats(writer, center.data(), center.size());
            writer.key("radius"); writer.number(gradient.radius);
            writer.key("angle"); writer.number(gradient.angle);
            writer.key("color_stops"); writer.beginArray();
            for (const auto& stop : gradient.color_stops) {
                writer.item(); writer.beginObject();
                writer.key("position"); writer.number(stop.position);
                writer.key("color");
                const std::array<float, 4> color{stop.color.r, stop.color.g, stop.color.b, stop.color.a};
                writeFloats(writer, color.data(), color.size());
                writer.endObject();
            }
            writer.endArray();
            if (!gradient.opacity_stops.empty()) {
                writer.key("opacity_stops"); writer.beginArray();
                for (const auto& stop : gradient.opacity_stops) {
                    writer.item(); writer.beginObject();
                    writer.key("position"); writer.number(stop.position);
                    writer.key("opacity"); writer.number(stop.opacity);
                    writer.endObject();
                }
                writer.endArray();
            }
            writer.endObject();
        }

        void writeShape(detail::Writer& writer, const ShapeContentPlan& shape) {
            writer.beginObject();
            writer.key("type"); writer.token(shapeTypeName(shape.type));
            if (shape.svg_asset) { writer.key("asset"); writer.text(*shape.svg_asset); }
            if (shape.lottie_asset) { writer.key("lottie"); writer.text(*shape.lottie_asset); }
            if (shape.type == ShapeContentTypePlan::Path) {
                writer.key("fill_rule");
                writer.token(shape.fill_rule == chronon3d::render_plan::ShapeFillRulePlan::EvenOdd ? "even_odd" : "non_zero");
            }
            if (shape.type == ShapeContentTypePlan::RoundedRect) {
                if (shape.corner_radius) {
                    writer.key("corner_radius");
                    writeFloats(writer, *shape.corner_radius, shape.corner_radius->size());
                } else if (shape.radius != 0.f) {
                    writer.key("radius"); writer.number(shape.radius);
                }
            }
            if (shape.gradient) {
                const auto& gradient = *shape.gradient;
                writer.key("fill");
                writer.beginObject();
                writer.key("type");
                writer.token(gradient.type == chronon3d::graphics::GradientType::Linear ? "linear" :
                             gradient.type == chronon3d::graphics::GradientType::Radial ? "radial" : "conic");
                writer.key("spread");
                writer.token(gradient.spread == chronon3d::graphics::GradientSpread::Repeat ? "repeat" :
                             gradient.spread == chronon3d::graphics::GradientSpread::Reflect ? "reflect" : "pad");
                const std::array<float, 2> start{gradient.start.x, gradient.start.y};
                const std::array<float, 2> end{gradient.end.x, gradient.end.y};
                const std::array<float, 2> center{gradient.center.x, gradient.center.y};
                writer.key("start"); writeFloats(writer, start.data(), start.size());
                writer.key("end"); writeFloats(writer, end.data(), end.size());
                writer.key("center"); writeFloats(writer, center.data(), center.size());
                writer.key("radius"); writer.number(gradient.radius);
                writer.key("angle"); writer.number(gradient.angle);
                writer.key("color_stops"); writer.beginArray();
                for (const auto& stop : gradient.color_stops) {
                    writer.item(); writer.beginObject();
                    writer.key("position"); writer.number(stop.position);
                    writer.key("color");
                    const std::array<float, 4> color{stop.color.r, stop.color.g, stop.color.b, stop.color.a};
                    writeFloats(writer, color.data(), color.size());
                    writer.endObject();
                }
                writer.endArray();
                if (!gradient.opacity_stops.empty()) {
                    writer.key("opacity_stops"); writer.beginArray();
                    for (const auto& stop : gradient.opacity_stops) {
                        writer.item(); writer.beginObject();
                        writer.key("position"); writer.number(stop.position);
                        writer.key("opacity"); writer.number(stop.opacity);
                        writer.endObject();
                    }
                    writer.endArray();
                }
                writer.endObject();
            } else if (shape.fill_color) {
                writer.key("fill"); writeFloats(writer, *shape.fill_color, shape.fill_color->size());
            }
            if (shape.field) {
                writer.key("fill"); writer.beginObject();
                writer.key("type"); writer.token("field");
                writer.key("field"); writeField(writer, *shape.field);
                if (shape.field_ramp) {
                    writer.key("field_ramp"); writeGradientDefinition(writer, *shape.field_ramp);
                }
                if (shape.field_render_scale != 1) {
                    writer.key("field_render_scale"); writer.unsignedInteger(shape.field_render_scale);
                }
                if (shape.field_drift) {
                    writer.key("field_drift");
                    const std::array<float, 2> drift{shape.field_drift->x, shape.field_drift->y};
                    writeFloats(writer, drift.data(), drift.size());
                }
                writer.endObject();
            }
            if (shape.gradient_mesh) {
                writer.key("fill"); writer.beginObject();
                writer.key("type"); writer.token("mesh");
                writer.key("mesh"); writer.beginObject();
                writer.key("nodes"); writer.beginArray();
                for (const auto& node : shape.gradient_mesh->nodes) {
                    writer.item(); writer.beginObject();
                    writer.key("position");
                    const std::array<float, 2> position{node.position.x, node.position.y};
                    writeFloats(writer, position.data(), position.size());
                    writer.key("color");
                    const std::array<float, 4> color{node.color.r, node.color.g, node.color.b, node.color.a};
                    writeFloats(writer, color.data(), color.size());
                    writer.key("weight"); writer.number(node.weight);
                    writer.endObject();
                }
                writer.endArray(); writer.endObject(); writer.endObject();
            }
            if (shape.type == ShapeContentTypePlan::Path || shape.type == ShapeContentTypePlan::Line) {
                if (!shape.path.empty()) { writer.key("path"); writePathCommands(writer, shape.path); }
            }
            if (shape.stroke) { writer.key("stroke"); writeStroke(writer, *shape.stroke); }
            if (shape.type == ShapeContentTypePlan::Star || shape.type == ShapeContentTypePlan::Polygon) {
                writer.key("points"); writer.integer(shape.points.value_or(shape.type == ShapeContentTypePlan::Star ? 5 : 6));
                if (shape.type == ShapeContentTypePlan::Star) {
                    writer.key("inner_radius_ratio"); writer.number(shape.inner_radius_ratio);
                }
                writer.key("rotation_degrees"); writer.number(shape.shape_rotation_deg);
            }
            if (shape.type == ShapeContentTypePlan::Arc) {
                writer.key("start_degrees"); writer.number(shape.arc_start_deg);
                writer.key("sweep_degrees"); writer.number(shape.arc_sweep_deg);
            }
            if (shape.type == ShapeContentTypePlan::Grid || shape.type == ShapeContentTypePlan::DotGrid) {
                writer.key("spacing"); writer.number(shape.grid_spacing);
            }
            if (shape.type == ShapeContentTypePlan::DotGrid) {
                writer.key("dot_radius"); writer.number(shape.dot_radius);
            }
            if (shape.type == ShapeContentTypePlan::DeviceFrame) {
                writer.key("device");
                writer.token(shape.device_frame_kind == chronon3d::render_plan::DeviceFrameKindPlan::Phone ? "phone" : "browser");
                if (!shape.device_frame_chrome) { writer.key("chrome"); writer.boolean(false); }
            }
            if (!shape.operators.empty()) {
                writer.key("operators");
                writer.beginArray();
                for (const auto& op : shape.operators) {
                    writer.item();
                    writeTrimOperator(writer, op);
                }
                writer.endArray();
            }
            writer.endObject();
        }

        void writeParticleEmitter(detail::Writer& writer,
                                  const chronon3d::ParticleEmitterParams& particles,
                                  const std::string& schema) {
            writer.beginObject();
            writer.key("count"); writer.unsignedInteger(particles.count);
            writer.key("seed"); writer.unsignedInteger(particles.seed);
            writer.key("emitter_size");
            const std::array<float, 2> emitterSize{particles.emitter_size.x, particles.emitter_size.y};
            writeFloats(writer, emitterSize, emitterSize.size());
            writer.key("velocity_min");
            const std::array<float, 2> velocityMin{particles.velocity_min.x, particles.velocity_min.y};
            writeFloats(writer, velocityMin, velocityMin.size());
            writer.key("velocity_max");
            const std::array<float, 2> velocityMax{particles.velocity_max.x, particles.velocity_max.y};
            writeFloats(writer, velocityMax, velocityMax.size());
            writer.key("lifetime");
            writer.beginArray(); writer.item(); writer.number(particles.lifetime_min);
            writer.item(); writer.number(particles.lifetime_max); writer.endArray();
            writer.key("size");
            writer.beginArray(); writer.item(); writer.number(particles.size_min);
            writer.item(); writer.number(particles.size_max); writer.endArray();
            if (particles.gravity != 0.f) { writer.key("gravity"); writer.number(particles.gravity); }
            if (particles.turbulence != 0.f) { writer.key("turbulence"); writer.number(particles.turbulence); }
            if (particles.blend_mode != chronon3d::BlendMode::Normal) {
                writer.key("blend_mode"); writer.token(blendModeName(particles.blend_mode));
            }
            if (!particles.color_ramp.color_stops.empty()) {
                writer.key("color_ramp");
                writer.beginArray();
                for (const auto& stop : particles.color_ramp.color_stops) {
                    writer.item();
                    writer.beginObject();
                    writer.key("position"); writer.number(stop.position);
                    writer.key("color");
                    const std::array<float, 4> color{stop.color.r, stop.color.g, stop.color.b, stop.color.a};
                    writeFloats(writer, color, color.size());
                    writer.endObject();
                }
                writer.endArray();
            }
            if (schema == chronon3d::render_plan::kRenderPlanSchemaV3) {
                const char* shape = "box";
                switch (particles.shape) {
                    case chronon3d::EmitterShape::Box: shape = "box"; break;
                    case chronon3d::EmitterShape::Ellipse: shape = "ellipse"; break;
                    case chronon3d::EmitterShape::Edge: shape = "edge"; break;
                    case chronon3d::EmitterShape::Path: shape = "path"; break;
                }
                if (particles.shape != chronon3d::EmitterShape::Box) { writer.key("shape"); writer.token(shape); }
                if (!particles.path_id.empty()) { writer.key("path_id"); writer.text(particles.path_id); }
                if (!particles.path_points.empty()) {
                    writer.key("path_points");
                    writer.beginArray();
                    for (const auto point : particles.path_points) {
                        writer.item();
                        const std::array<float, 2> value{point.x, point.y};
                        writeFloats(writer, value, value.size());
                    }
                    writer.endArray();
                }
                if (!particles.sprite_asset.empty()) { writer.key("sprite_asset"); writer.text(particles.sprite_asset); }
                if (particles.sprite_scale_min != 1.f || particles.sprite_scale_max != 1.f) {
                    writer.key("sprite_scale");
                    const std::array<float, 2> range{particles.sprite_scale_min, particles.sprite_scale_max};
                    writeFloats(writer, range, range.size());
                }
                if (particles.rotation_min != 0.f || particles.rotation_max != 0.f) {
                    writer.key("rotation");
                    const std::array<float, 2> range{particles.rotation_min, particles.rotation_max};
                    writeFloats(writer, range, range.size());
                }
                if (particles.angular_velocity_min != 0.f || particles.angular_velocity_max != 0.f) {
                    writer.key("angular_velocity");
                    const std::array<float, 2> range{particles.angular_velocity_min, particles.angular_velocity_max};
                    writeFloats(writer, range, range.size());
                }
            }
            writer.endObject();
        }

        void writeLight(detail::Writer& writer, const chronon3d::render_plan::LightLayerPlan& light) {
            writer.beginObject();
            writer.key("radius"); writer.number(light.radius);
            writer.key("intensity"); writer.number(light.intensity);
            const chronon3d::Color color{light.color[0], light.color[1], light.color[2], light.color[3]};
            const auto hex = textSpanColorHex(color);
            writer.key("color"); writer.text(hex ? hex->substr(0, 7) : "#FFFFFF");
            const char* kind = light.kind == chronon3d::render_plan::LightKindPlan::Point ? "point"
                : light.kind == chronon3d::render_plan::LightKindPlan::Spot ? "spot"
                : light.kind == chronon3d::render_plan::LightKindPlan::Directional ? "directional" : "radial";
            writer.key("kind"); writer.token(kind);
            if (light.kind != chronon3d::render_plan::LightKindPlan::Radial) {
                writer.key("direction");
                writeFloats(writer, light.direction, light.direction.size());
            }
            if (light.range != 0.f) { writer.key("range"); writer.number(light.range); }
            if (light.decay != 1.f) { writer.key("decay"); writer.number(light.decay); }
            if (light.angle != 0.78539816339744830962f) { writer.key("angle"); writer.number(light.angle); }
            if (light.penumbra != 0.f) { writer.key("penumbra"); writer.number(light.penumbra); }
            if (light.cast_shadow) { writer.key("cast_shadow"); writer.boolean(true); }
            if (light.shadow_radius != 1.f) { writer.key("shadow_radius"); writer.number(light.shadow_radius); }
            if (light.shadow_bias != 0.f) { writer.key("shadow_bias"); writer.number(light.shadow_bias); }
            writer.endObject();
        }

        void writeLayer(detail::Writer& writer, const LayerPlan& layer, const std::string& schema) {

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
                if (track.bezier_curve) {
                    writer.key("bezier_curve");
                    writeFloats(writer, track.bezier_curve->data(), track.bezier_curve->size());
                }
                writer.key("keyframes");
                writeKeyframes(writer, track.keyframes, true);
                writer.endObject();
            }
            writer.endArray();
            writer.endObject();
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
        if (plan.camera_animation) {
            writer.key("camera_animation");
            writeCameraAnimation(writer, *plan.camera_animation);
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
