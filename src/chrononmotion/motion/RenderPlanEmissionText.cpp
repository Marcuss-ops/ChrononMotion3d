// Text payload serialization of the emitter.

#include "RenderPlanEmissionInternal.hpp"
#include "JsonWriter.hpp"

#include <chronon3d/render_plan/color_utils.hpp>
#include <chronon3d/effects/effect_param_animation.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace chrononmotion::motion {
namespace detail {
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
                    // RenderPlan v3 text selector ease window (mirror of
                    // GlyphSelectorSpec semantics); omitted on defaults so the
                    // emitted JSON stays byte-identical for unmodified selectors.
                    if (selector.ease_low != 0.f) { writer.key("ease_low"); writer.number(selector.ease_low); }
                    if (selector.ease_high != 100.f) { writer.key("ease_high"); writer.number(selector.ease_high); }
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

    } // namespace
}
