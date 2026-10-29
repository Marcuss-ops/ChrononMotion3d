// Per-layer write contract of the emitter.

#include "RenderPlanEmissionInternal.hpp"
#include "chrononmotion/math/Finite.hpp"

#include <chronon3d/render_plan/color_utils.hpp>
#include <chronon3d/effects/effect_param_animation.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cmath>
#include <limits>
#include <cstddef>
#include <initializer_list>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace chrononmotion::motion::detail {
using chronon3d::render_plan::AnimationKeyframePlan;
using chronon3d::render_plan::AnimationPlan;
using chronon3d::render_plan::AnimationTrackPlan;
using chronon3d::render_plan::CameraPlan;
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

        [[nodiscard]] std::string layerOwner(const LayerPlan& layer) {

            return layer.id.empty() ? std::string("a layer") : "layer '" + layer.id + "'";
        }

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
            if (style.background) {
                if (layer.type != LayerType::Image && layer.type != LayerType::Video)
                    return owner + " carries a background plate on an unsupported layer type";
                const auto& background = *style.background;
                if (!chronon3d::render_plan::parse_hex_color(background.color))
                    return owner + " has a media frame color that is not representable as #RRGGBB";
                if (background.opacity && (!std::isfinite(*background.opacity) ||
                    *background.opacity < 0.f || *background.opacity > 1.f))
                    return owner + " has media frame opacity outside [0, 1]";
                if (background.radius && (!std::isfinite(*background.radius) ||
                    *background.radius < 0.f || *background.radius > 512.f))
                    return owner + " has media frame radius outside [0, 512]";
                if (background.padding_dimensions != 2)
                    return owner + " media frame requires two-dimensional padding";
                for (float padding : background.padding)
                    if (!std::isfinite(padding) || padding < 0.f || padding > 4096.f)
                        return owner + " has media frame padding outside [0, 4096]";
            }
            return {};
        }

        [[nodiscard]] std::string layerProblem(const LayerPlan& layer, const std::string& schema) {

            if (layer.id.empty()) return "a layer has no id";

            const std::string owner = layerOwner(layer);

            switch (layer.type) {
                case LayerType::Color:
                case LayerType::Image:
                case LayerType::Video:
                case LayerType::Text:
                case LayerType::Shape:
                case LayerType::Particles:
                case LayerType::Light:
                case LayerType::Mesh:
                    break;
                default:
                    return owner + " is a '" + layerTypeName(layer.type) +
                           "' layer, which this emitter does not write";
            }

            if (layer.text_spans.size() > 65'536 || layer.text_animators.size() > 65'536)
                return owner + " exceeds the RenderPlan v3 text span or animator count limit";

            if (layer.type == LayerType::Image && layer.asset.empty()) {
                return owner + " is an image layer with no asset";
            }
            if (layer.type == LayerType::Video && layer.source.empty()) {
                return owner + " is a video layer with no source";
            }
            if (layer.type == LayerType::Text && !layer.style) {
                return owner + " is a text layer with no style, which the plan contract requires";
            }
            if (layer.type == LayerType::Shape && !layer.shape)
                return owner + " is a shape layer with no shape payload";
            if (layer.type == LayerType::Particles && !layer.particle_emitter)
                return owner + " is a particles layer with no particle emitter";
            if (layer.type == LayerType::Light && !layer.light)
                return owner + " is a light layer with no light payload";
            if (layer.type == LayerType::Mesh && !layer.source.empty()) {
                return owner + " is a mesh layer with a source reference; the plan requires inline geometry";
            }
            if (layer.type == LayerType::Mesh && !layer.asset.empty() && !layer.inline_mesh) {
                return owner + " is a mesh layer with a texture asset but no inline geometry";
            }
            if (layer.type == LayerType::Mesh && layer.animation && !layer.animation->tracks.empty()) {
                return owner + " carries transform tracks; use V3 inline_mesh baked geometry samples";
            }

            if (layer.parent) return owner + " is parented; the plan contract already flattens hierarchy";
            if (layer.track_matte && (layer.track_matte->source.empty() || layer.track_matte->source.size() > 256))
                return owner + " carries a track matte with an invalid source layer id";
            if (layer.track_matte && schema != chronon3d::render_plan::kRenderPlanSchemaV3)
                return owner + " carries a track matte, which requires RenderPlan v3";
            if (layer.track_matte && layer.track_matte->type != chronon3d::render_plan::TrackMatteTypePlan::Alpha &&
                layer.track_matte->type != chronon3d::render_plan::TrackMatteTypePlan::AlphaInverted &&
                layer.track_matte->type != chronon3d::render_plan::TrackMatteTypePlan::Luma &&
                layer.track_matte->type != chronon3d::render_plan::TrackMatteTypePlan::LumaInverted)
                return owner + " carries an unknown track-matte type";
            if (layer.time_remap) {
                if (schema != chronon3d::render_plan::kRenderPlanSchemaV3)
                    return owner + " carries a time remap, which requires RenderPlan v3";
                if (layer.type != LayerType::Video)
                    return owner + " carries a time remap, which is supported only for video layers";
                const auto& remap = *layer.time_remap;
                if (remap.speed && (!std::isfinite(*remap.speed) || *remap.speed <= 0.f || *remap.speed > 1000.f))
                    return owner + " carries an invalid time-remap speed";
                if (remap.freeze_frame && (remap.freeze_frame->integral() < 0 ||
                                           remap.freeze_frame->integral() > 1'000'000))
                    return owner + " carries an out-of-range time-remap freeze frame";
                if (remap.freeze_frame && remap.animation)
                    return owner + " carries both a time-remap freeze frame and animation";
                if (remap.animation) {
                    const AnimationTrackPlan& track = *remap.animation;
                    const std::string problem = trackProblem(owner + " time-remap animation", track);
                    if (!problem.empty()) return problem;
                    if (track.keyframes.empty()) return owner + " carries an empty time-remap animation";
                    for (const AnimationKeyframePlan& key : track.keyframes) {
                        if (key.frame.integral() < 0 || key.frame.integral() > 1'000'000 ||
                            key.value.size() != 1 || !std::isfinite(key.value.front()) ||
                            key.value.front() < 0.f || key.value.front() > 1'000'000.f)
                            return owner + " carries an invalid time-remap keyframe";
                    }
                }
            }
            if (!layer.effect_param_tracks.empty() && schema != chronon3d::render_plan::kRenderPlanSchemaV3)
                return owner + " carries effect parameter tracks, which require RenderPlan v3";
            if ((layer.path_mask || !layer.masks.empty() || layer.transition_in || layer.transition_out) &&
                schema != chronon3d::render_plan::kRenderPlanSchemaV3)
                return owner + " carries masks or transitions, which require RenderPlan v3";
            if (!layer.text_spans.empty() && schema != chronon3d::render_plan::kRenderPlanSchemaV3)
                return owner + " carries text spans, which require RenderPlan v3";
            std::size_t previousTextSpanEnd = 0;
            std::vector<std::string> semanticIds;
            const auto isUtf8Boundary = [&](std::size_t offset) {
                return offset == 0 || offset == layer.text.size() ||
                    (offset < layer.text.size() &&
                     (static_cast<unsigned char>(layer.text[offset]) & 0xc0U) != 0x80U);
            };
            for (const auto& span : layer.text_spans) {
                if (layer.type != LayerType::Text)
                    return owner + " carries text spans but is not a text layer";
                if (span.byte_start < previousTextSpanEnd || span.byte_start >= span.byte_end ||
                    span.byte_end > layer.text.size() || !isUtf8Boundary(span.byte_start) ||
                    !isUtf8Boundary(span.byte_end))
                    return owner + " carries overlapping, unsorted, or out-of-range UTF-8 text spans";
                previousTextSpanEnd = span.byte_end;
                if (span.font || span.font_size || span.baseline_shift)
                    return owner + " carries a text span font, absolute size, or baseline override that V3 does not serialize";
                if (span.tracking && (!std::isfinite(*span.tracking) ||
                                      *span.tracking < -1000.f || *span.tracking > 1000.f))
                    return owner + " carries text span tracking outside [-1000, 1000]";
            if (span.highlight && !span.highlight->background &&
                !span.highlight->padding_em && !span.highlight->radius_em)
                return owner + " carries an empty text highlight style";
            if (span.font_size_multiplier && (!std::isfinite(*span.font_size_multiplier) ||
                    *span.font_size_multiplier <= 0.f || *span.font_size_multiplier > 100.f))
                    return owner + " carries a text span font-size multiplier outside (0, 100]";
                if (span.semantic_id && (span.semantic_id->empty() || span.semantic_id->size() > 256))
                    return owner + " carries a text span semantic_id outside the V3 bounds";
                if (span.semantic_id) {
                    if (std::find(semanticIds.begin(), semanticIds.end(), *span.semantic_id) != semanticIds.end())
                        return owner + " carries duplicate text span semantic_id '" + *span.semantic_id + "'";
                    semanticIds.push_back(*span.semantic_id);
                }
                const auto validColor = [](const std::optional<chronon3d::Color>& color) {
                    return !color || (std::isfinite(color->r) && std::isfinite(color->g) &&
                        std::isfinite(color->b) && std::isfinite(color->a) &&
                        color->r >= 0.f && color->r <= 1.f && color->g >= 0.f && color->g <= 1.f &&
                        color->b >= 0.f && color->b <= 1.f && color->a >= 0.f && color->a <= 1.f);
                };
                const auto representableColor = [](const std::optional<chronon3d::Color>& color) {
                    return !color || textSpanColorHex(*color).has_value();
                };
                if (!validColor(span.color) || !representableColor(span.color))
                    return owner + " carries a non-finite, out-of-range, or non-serializable text span color";
                if (span.stroke) {
                    if (!validColor(span.stroke->color) || !representableColor(span.stroke->color))
                        return owner + " carries an invalid or non-serializable text span stroke color";
                    if (!std::isfinite(span.stroke->width_em) || span.stroke->width_em < 0.f || span.stroke->width_em > 100.f)
                        return owner + " carries a text span stroke width outside [0, 100]";
                    if (span.stroke->dash_period_em && *span.stroke->dash_period_em != 0.f)
                        return owner + " carries a text span dash pattern that V3 does not serialize";
                }
                if (span.highlight) {
                    if (!span.highlight->background && !span.highlight->padding_em && !span.highlight->radius_em)
                        return owner + " carries an empty text highlight style";
                    if (!validColor(span.highlight->background) || !representableColor(span.highlight->background))
                        return owner + " carries an invalid or non-serializable text highlight color";
                    for (const auto value : {span.highlight->padding_em, span.highlight->radius_em})
                        if (value && (!std::isfinite(*value) || *value < 0.f || *value > 100.f))
                            return owner + " carries text highlight geometry outside [0, 100] em";
                }
            }
            if (!layer.font.empty()) {
                return owner + " carries a normalized font path, which the plan derives from style.font";
            }
            if (layer.motion_blur) return owner + " carries analytic motion blur";
            if (layer.shape) {
                const ShapeContentPlan& shape = *layer.shape;
                if (layer.type != LayerType::Shape) return owner + " carries shape content but is not a shape layer";
                if (std::string_view(shapeTypeName(shape.type)) == "unknown")
                    return owner + " carries an unknown shape kind";
                if (layer.size_dimensions != 2 || !std::isfinite(layer.size[0]) || !std::isfinite(layer.size[1]) ||
                    layer.size[0] <= 0.f || layer.size[1] <= 0.f)
                    return owner + " shape requires a positive two-dimensional size";
                if (!shape.imported_subpaths.empty())
                    return owner + " carries imported subpaths that the current emitter does not write";
                if (shape.field) {
                    const auto& field = *shape.field;
                    if (!std::isfinite(field.frequency) || field.frequency <= 0.f || field.frequency > 4096.f ||
                        field.octaves < 1 || field.octaves > 16 ||
                        !std::isfinite(field.persistence) || field.persistence < 0.f || field.persistence > 1.f ||
                        !std::isfinite(field.lacunarity) || field.lacunarity <= 0.f || field.lacunarity > 8.f ||
                        field.operators.size() > 8 || shape.gradient || shape.gradient_mesh ||
                        shape.fill_color || shape.type == ShapeContentTypePlan::Line ||
                        shape.type == ShapeContentTypePlan::Arc || shape.type == ShapeContentTypePlan::Grid ||
                        shape.type == ShapeContentTypePlan::DotGrid || shape.type == ShapeContentTypePlan::DeviceFrame)
                        return owner + " carries a field with unsupported geometry, fill conflict, or invalid settings";
                    const auto& transform = field.transform;
                    if (transform.translation.x != 0.f || transform.translation.y != 0.f ||
                        transform.scale.x != 1.f || transform.scale.y != 1.f ||
                        transform.rotation_radians != 0.f || transform.shear.x != 0.f ||
                        transform.shear.y != 0.f || transform.pivot.x != 0.5f || transform.pivot.y != 0.5f)
                        return owner + " carries Field2D transform values absent from the RenderPlan field contract";
                    bool scalarOperatorSeen = false;
                    bool warpSeen = false;
                    for (const auto& op : field.operators) {
                        using chronon3d::graphics::Field2DOperatorKind;
                        if (op.kind == Field2DOperatorKind::Warp || op.kind == Field2DOperatorKind::DomainWarp) {
                            if (scalarOperatorSeen || warpSeen || !std::isfinite(op.amount) ||
                                op.amount < 0.f || op.amount > 1.f)
                                return owner + " carries an invalid Field2D warp operator";
                            warpSeen = true;
                        } else {
                            scalarOperatorSeen = true;
                            if ((op.kind == Field2DOperatorKind::Remap &&
                                 (!std::isfinite(op.from) || !std::isfinite(op.to) || op.from < 0.f || op.from > 1.f ||
                                  op.to < 0.f || op.to > 1.f || std::abs(op.to - op.from) < 1e-7f)) ||
                                (op.kind == Field2DOperatorKind::Threshold &&
                                 (!std::isfinite(op.edge) || op.edge < 0.f || op.edge > 1.f)) ||
                                (op.kind == Field2DOperatorKind::Smoothstep &&
                                 (!std::isfinite(op.edge) || op.edge < 0.f || op.edge > 1.f ||
                                  !std::isfinite(op.softness) || op.softness <= 0.f || op.softness > 1.f)) ||
                                op.kind == Field2DOperatorKind::Translate ||
                                op.kind == Field2DOperatorKind::Scale || op.kind == Field2DOperatorKind::Rotate ||
                                op.kind == Field2DOperatorKind::Repeat || op.kind == Field2DOperatorKind::Add ||
                                op.kind == Field2DOperatorKind::Subtract || op.kind == Field2DOperatorKind::Multiply ||
                                op.kind == Field2DOperatorKind::Min || op.kind == Field2DOperatorKind::Max ||
                                op.kind == Field2DOperatorKind::Abs || op.kind == Field2DOperatorKind::Power ||
                                op.kind == Field2DOperatorKind::Clamp)
                                return owner + " carries a Field2D operator absent from the RenderPlan contract";
                        }
                    }
                    if (shape.field_ramp) {
                        const auto& ramp = *shape.field_ramp;
                        if (ramp.color_stops.size() < 2 || ramp.color_stops.size() > 256)
                            return owner + " carries a field ramp outside [2, 256] stops";
                        for (const auto& stop : ramp.color_stops)
                            if (!std::isfinite(stop.position) || stop.position < 0.f || stop.position > 1.f ||
                                !std::isfinite(stop.color.r) || stop.color.r < 0.f || stop.color.r > 1.f ||
                                !std::isfinite(stop.color.g) || stop.color.g < 0.f || stop.color.g > 1.f ||
                                !std::isfinite(stop.color.b) || stop.color.b < 0.f || stop.color.b > 1.f ||
                                !std::isfinite(stop.color.a) || stop.color.a < 0.f || stop.color.a > 1.f)
                                return owner + " carries invalid field-ramp stops";
                    }
                    if (shape.field_render_scale != 1 && shape.field_render_scale != 2 && shape.field_render_scale != 4)
                        return owner + " carries a field render scale outside {1, 2, 4}";
                    if (shape.field_drift && (!std::isfinite(shape.field_drift->x) ||
                        !std::isfinite(shape.field_drift->y) || std::abs(shape.field_drift->x) > 100.f ||
                        std::abs(shape.field_drift->y) > 100.f))
                        return owner + " carries field drift outside [-100, 100]";
                } else if (shape.field_ramp || shape.field_render_scale != 1 || shape.field_drift) {
                    return owner + " carries field options without a Field2D source";
                }
                if (shape.gradient_mesh) {
                    if (shape.gradient || shape.field || shape.fill_color ||
                        shape.gradient_mesh->nodes.empty() || shape.gradient_mesh->nodes.size() > 256 ||
                        shape.type == ShapeContentTypePlan::Line || shape.type == ShapeContentTypePlan::Arc ||
                        shape.type == ShapeContentTypePlan::Grid || shape.type == ShapeContentTypePlan::DeviceFrame)
                        return owner + " carries a mesh fill with unsupported geometry, conflict, or node count";
                    for (const auto& node : shape.gradient_mesh->nodes)
                        if (!std::isfinite(node.position.x) || node.position.x < 0.f || node.position.x > 1.f ||
                            !std::isfinite(node.position.y) || node.position.y < 0.f || node.position.y > 1.f ||
                            !std::isfinite(node.color.r) || node.color.r < 0.f || node.color.r > 1.f ||
                            !std::isfinite(node.color.g) || node.color.g < 0.f || node.color.g > 1.f ||
                            !std::isfinite(node.color.b) || node.color.b < 0.f || node.color.b > 1.f ||
                            !std::isfinite(node.color.a) || node.color.a < 0.f || node.color.a > 1.f ||
                            !std::isfinite(node.weight) || node.weight < 0.0001f || node.weight > 1000.f)
                            return owner + " carries invalid gradient-mesh nodes";
                }
                if (shape.gradient) {
                    const auto& gradient = *shape.gradient;
                    if (gradient.color_stops.size() < 2 ||
                        (gradient.type != chronon3d::graphics::GradientType::Linear &&
                         gradient.type != chronon3d::graphics::GradientType::Radial &&
                         gradient.type != chronon3d::graphics::GradientType::Conic) ||
                        (gradient.spread != chronon3d::graphics::GradientSpread::Pad &&
                         gradient.spread != chronon3d::graphics::GradientSpread::Repeat &&
                         gradient.spread != chronon3d::graphics::GradientSpread::Reflect) ||
                        !std::isfinite(gradient.start.x) || !std::isfinite(gradient.start.y) ||
                        !std::isfinite(gradient.end.x) || !std::isfinite(gradient.end.y) ||
                        !std::isfinite(gradient.center.x) || !std::isfinite(gradient.center.y) ||
                        !std::isfinite(gradient.radius) || gradient.radius <= 0.f ||
                        !std::isfinite(gradient.angle))
                        return owner + " carries an invalid gradient definition";
                    for (const auto& stop : gradient.color_stops) {
                        const auto unit = [](float value) {
                            return std::isfinite(value) && value >= 0.f && value <= 1.f;
                        };
                        if (!std::isfinite(stop.position) || stop.position < 0.f || stop.position > 1.f ||
                            !unit(stop.color.r) || !unit(stop.color.g) ||
                            !unit(stop.color.b) || !unit(stop.color.a))
                            return owner + " carries an invalid gradient color stop";
                    }
                    for (const auto& stop : gradient.opacity_stops)
                        if (!std::isfinite(stop.position) || stop.position < 0.f || stop.position > 1.f ||
                            !std::isfinite(stop.opacity) || stop.opacity < 0.f || stop.opacity > 1.f)
                            return owner + " carries an invalid gradient opacity stop";
                }
                if ((shape.svg_asset || shape.lottie_asset) && shape.type != ShapeContentTypePlan::Path)
                    return owner + " carries an SVG/Lottie source on a non-path shape";
                if (shape.svg_asset && shape.lottie_asset)
                    return owner + " carries both SVG and Lottie sources";
                const bool pathKind = shape.type == ShapeContentTypePlan::Path || shape.type == ShapeContentTypePlan::Line;
                if (shape.type != ShapeContentTypePlan::Path &&
                    shape.fill_rule != chronon3d::render_plan::ShapeFillRulePlan::EvenOdd)
                    return owner + " carries a path fill rule on a non-path shape";
                if (pathKind && !shape.svg_asset && !shape.lottie_asset &&
                    (shape.path.empty() || shape.path.size() > 65'536))
                    return owner + " path command count must be in [1, 65536]";
                if (!pathKind && !shape.path.empty())
                    return owner + " carries explicit path commands on a shape generated procedurally by Chronon3D";
                if (shape.type == ShapeContentTypePlan::Line && !shape.stroke)
                    return owner + " is a line shape with no stroke";
                if (shape.type == ShapeContentTypePlan::RoundedRect) {
                    const float maximum = std::min(layer.size[0], layer.size[1]) * 0.5f;
                    if (shape.radius < 0.f || !std::isfinite(shape.radius) || shape.radius > maximum)
                        return owner + " has a rounded-rectangle radius outside its size";
                    if (shape.corner_radius && (shape.radius != 0.f ||
                        std::any_of(shape.corner_radius->begin(), shape.corner_radius->end(), [&](float value) {
                            return !std::isfinite(value) || value < 0.f || value > maximum;
                        })))
                        return owner + " has invalid or conflicting per-corner radii";
                } else if (shape.corner_radius || shape.radius != 0.f) {
                    return owner + " carries corner radii on a non-rounded-rectangle shape";
                }
                const bool pointShape = shape.type == ShapeContentTypePlan::Star ||
                                        shape.type == ShapeContentTypePlan::Polygon;
                if (pointShape) {
                    const int count = shape.points.value_or(shape.type == ShapeContentTypePlan::Star ? 5 : 6);
                    if (count < 3 || count > 64 || !std::isfinite(shape.inner_radius_ratio) ||
                        shape.inner_radius_ratio < 0.05f || shape.inner_radius_ratio > 0.95f ||
                        !std::isfinite(shape.shape_rotation_deg))
                        return owner + " has invalid star/polygon parameters";
                } else if (shape.points || shape.shape_rotation_deg != 0.f || shape.inner_radius_ratio != 0.4f) {
                    return owner + " carries star/polygon parameters on another shape type";
                }
                if (shape.type == ShapeContentTypePlan::Arrow) {
                    if (shape.arrow_head_length || shape.arrow_head_width || shape.arrow_shaft_width != 4.f ||
                        shape.arrow_direction != "right")
                        return owner + " carries arrow width/direction values absent from the RenderPlan schema";
                } else if (shape.arrow_head_length || shape.arrow_head_width || shape.arrow_shaft_width != 4.f ||
                           shape.arrow_direction != "right") {
                    return owner + " carries arrow parameters on another shape type";
                }
                if (shape.type == ShapeContentTypePlan::Arc) {
                    if (!std::isfinite(shape.arc_start_deg) || shape.arc_start_deg < 0.f || shape.arc_start_deg >= 360.f ||
                        !std::isfinite(shape.arc_sweep_deg) || shape.arc_sweep_deg < 1.f || shape.arc_sweep_deg > 359.9f ||
                        shape.arc_thickness != 8.f || shape.fill_color || !shape.stroke)
                        return owner + " has arc values not expressible by the current RenderPlan schema";
                } else if (shape.arc_start_deg != 0.f || shape.arc_sweep_deg != 270.f || shape.arc_thickness != 8.f) {
                    return owner + " carries arc parameters on another shape type";
                }
                if (shape.type == ShapeContentTypePlan::Grid || shape.type == ShapeContentTypePlan::DotGrid) {
                    if (!std::isfinite(shape.grid_spacing) || shape.grid_spacing < 4.f || shape.grid_spacing > 512.f)
                        return owner + " has grid spacing outside [4, 512]";
                    if (shape.type == ShapeContentTypePlan::Grid &&
                        (shape.grid_line_width != 1.f || shape.fill_color || !shape.stroke))
                        return owner + " has grid values not expressible by the current RenderPlan schema";
                    if (shape.type == ShapeContentTypePlan::DotGrid &&
                        (!std::isfinite(shape.dot_radius) || shape.dot_radius < 0.5f || shape.dot_radius > 64.f || !shape.fill_color))
                        return owner + " has invalid dot-grid radius or no fill";
                } else if (shape.grid_spacing != 24.f || shape.grid_line_width != 1.f || shape.dot_radius != 3.f) {
                    return owner + " carries grid parameters on another shape type";
                }
                if (shape.type == ShapeContentTypePlan::DeviceFrame) {
                    if (!shape.fill_color) return owner + " device frame has no screen fill";
                    if (shape.device_frame)
                        return owner + " carries nested device-frame styling absent from the RenderPlan schema";
                } else if (shape.device_frame || shape.device_frame_chrome != true ||
                           shape.device_frame_kind != chronon3d::render_plan::DeviceFrameKindPlan::Phone) {
                    return owner + " carries device-frame options on another shape type";
                }
                if ((shape.type == ShapeContentTypePlan::Arc || shape.type == ShapeContentTypePlan::Grid) && !shape.stroke)
                    return owner + " procedural arc/grid requires a stroke";
                for (const auto& command : shape.path) {
                    if ((command.type != ShapePathCommandTypePlan::Close && !math::isFinite(command.point)) ||
                        !math::isFinite(command.control1) || !math::isFinite(command.control2))
                        return owner + " has a path command with non-finite coordinates";
                }
                if (shape.stroke) {
                    const auto& stroke = *shape.stroke;
                    if (stroke.gradient) return owner + " carries a gradient stroke that the current emitter does not write";
                    if (stroke.color.empty() && !stroke.solid_color)
                        return owner + " has a stroke with no color";
                    if (stroke.width && (!std::isfinite(*stroke.width) || *stroke.width < 0.f || *stroke.width > 10'000.f))
                        return owner + " has a stroke width outside [0, 10000]";
                    if (stroke.trim_start && (!std::isfinite(*stroke.trim_start) || *stroke.trim_start < 0.f || *stroke.trim_start > 1.f))
                        return owner + " has stroke trim_start outside [0, 1]";
                    if (stroke.trim_end && (!std::isfinite(*stroke.trim_end) || *stroke.trim_end < 0.f || *stroke.trim_end > 1.f))
                        return owner + " has stroke trim_end outside [0, 1]";
                    if (stroke.cap != "butt" && stroke.cap != "round" && stroke.cap != "square")
                        return owner + " has an unsupported stroke cap";
                    if (stroke.join != "miter" && stroke.join != "round" && stroke.join != "bevel")
                        return owner + " has an unsupported stroke join";
                    for (const float value : stroke.dash_array)
                        if (!std::isfinite(value) || value < 0.f)
                            return owner + " has a negative or non-finite dash entry";
                    if (!std::isfinite(stroke.dash_offset) || !std::isfinite(stroke.taper_start) ||
                        stroke.taper_start < 0.f || stroke.taper_start > 1.f ||
                        !std::isfinite(stroke.taper_end) || stroke.taper_end < 0.f || stroke.taper_end > 1.f)
                        return owner + " has stroke parameters outside the supported range";
                    if (stroke.solid_color)
                        for (const float component : *stroke.solid_color)
                            if (!std::isfinite(component) || component < 0.f || component > 1.f)
                                return owner + " has stroke RGBA components outside [0, 1]";
                }
                if (shape.fill_color)
                    for (const float component : *shape.fill_color)
                        if (!std::isfinite(component) || component < 0.f || component > 1.f)
                            return owner + " has fill RGBA components outside [0, 1]";
                for (const auto& op : shape.operators) {
                    if (shape.type != ShapeContentTypePlan::Path)
                        return owner + " carries path operators on a non-path shape";
                    if (op.kind != PathOperatorKindPlan::Trim)
                        return owner + " carries a path operator the current emitter does not write";
                    if (!std::isfinite(op.trim_start) || op.trim_start < 0.f || op.trim_start > 1.f ||
                        !std::isfinite(op.trim_end) || op.trim_end < op.trim_start || op.trim_end > 1.f ||
                        !std::isfinite(op.trim_offset) || std::abs(op.trim_offset) > 1'000'000.f)
                        return owner + " has trim-path values outside the RenderPlan contract";
                    if (op.trim_animation) {
                        const auto& track = *op.trim_animation;
                        if (!isKnownEasing(track.easing) || track.keyframes.empty() || track.keyframes.size() > 65'536)
                            return owner + " has an invalid trim-path animation";
                        if (track.bezier_curve || track.spring || track.loop_mode != "none" || track.wiggle ||
                            track.oscillate || track.random_hold || track.posterize_time || track.data_feed ||
                            track.audio_react)
                            return owner + " has trim-path timing or drivers that the current emitter cannot preserve";
                        chronon3d::Frame previousFrame{-1};
                        for (const auto& key : track.keyframes) {
                            if (key.frame.integral() < 0 || key.frame.integral() > 1'000'000 ||
                                key.frame.integral() <= previousFrame.integral() ||
                                key.value.empty() || key.value.size() > 3)
                                return owner + " has a trim-path key outside the RenderPlan limits";
                            previousFrame = key.frame;
                            if (key.interpolation || key.easing || key.easing_xy || key.bezier_curve || key.spring ||
                                key.roving || key.temporal_in_dx || key.temporal_in_dy ||
                                key.temporal_out_dx || key.temporal_out_dy)
                                return owner + " has per-key trim-path timing the current emitter cannot preserve";
                            for (std::size_t component = 0; component < key.value.size(); ++component) {
                                const float value = key.value[component];
                                if (!std::isfinite(value) || (component < 2 && (value < 0.f || value > 1.f)))
                                    return owner + " has a non-finite or out-of-range trim-path key";
                            }
                        }
                    }
                }
            }
            if (layer.inline_mesh) {
                if (layer.type != LayerType::Mesh)
                    return owner + " carries inline mesh geometry but is not a mesh layer";
                const auto& mesh = *layer.inline_mesh;
                if (!mesh.metallic_roughness_texture_asset.empty()) {
                    const auto& path = mesh.metallic_roughness_texture_asset;
                    if (path.size() > 4096 || path.front() == '/' || path.find('\\') != std::string::npos ||
                        path.find(':') != std::string::npos)
                        return owner + " has an unsafe metallic-roughness texture path";
                    std::size_t start = 0;
                    while (start <= path.size()) {
                        const auto end = path.find('/', start);
                        const auto part = path.substr(start, end == std::string::npos ? end : end - start);
                        if (part.empty() || part == "." || part == "..")
                            return owner + " has an unsafe metallic-roughness texture path";
                        if (end == std::string::npos) break;
                        start = end + 1;
                    }
                }
                if (mesh.vertices.size() < 3 || mesh.vertices.size() > chronon3d::render_plan::kInlineMeshMaxVertices)
                    return owner + " has an inline mesh vertex count outside [3, 250000]";
                if (mesh.indices.size() < 3 || mesh.indices.size() > chronon3d::render_plan::kInlineMeshMaxIndices ||
                    mesh.indices.size() % 3 != 0)
                    return owner + " has an inline mesh index count outside the triangle contract";
                for (const auto& vertex : mesh.vertices) {
                    for (const float component : vertex.position)
                        if (!std::isfinite(component) || std::abs(component) > 1'000'000.f)
                            return owner + " has an inline mesh position outside the finite contract range";
                    for (const float component : vertex.normal)
                        if (!std::isfinite(component) || std::abs(component) > 1.f)
                            return owner + " has an inline mesh normal outside [-1, 1]";
                    for (const float component : vertex.uv)
                        if (!std::isfinite(component) || std::abs(component) > 1'000'000.f)
                            return owner + " has inline mesh UVs outside the finite contract range";
                }
                for (const std::uint32_t index : mesh.indices)
                    if (index >= mesh.vertices.size())
                        return owner + " has an inline mesh index outside its vertex array";
                if (!mesh.animation.empty()) {
                    if (schema != chronon3d::render_plan::kRenderPlanSchemaV3)
                        return owner + " carries animated inline geometry, which requires RenderPlan V3";
                    if (mesh.animation.size() > chronon3d::render_plan::kInlineMeshMaxAnimationSamples)
                        return owner + " exceeds the 4096 baked mesh animation sample limit";
                    std::int64_t previousFrame = -1;
                    std::uint64_t animationBytes = 0;
                    for (const auto& sample : mesh.animation) {
                        if (sample.frame < 0 || sample.frame > 1'000'000 ||
                            sample.frame <= previousFrame || sample.positions.size() != mesh.vertices.size() ||
                            (!sample.normals.empty() && sample.normals.size() != mesh.vertices.size()))
                            return owner + " has a mesh sample outside the timeline, unsorted, or with mismatched topology";
                        if (sample.normals.empty() != mesh.animation.front().normals.empty())
                            return owner + " has inconsistent animated normal arrays";
                        previousFrame = sample.frame;
                        constexpr std::uint64_t bytesPerPosition = sizeof(std::array<float, 3>);
                        if (sample.positions.size() >
                                chronon3d::render_plan::kInlineMeshMaxAggregateBytes / bytesPerPosition ||
                            animationBytes > chronon3d::render_plan::kInlineMeshMaxAggregateBytes -
                                sample.positions.size() * bytesPerPosition)
                            return owner + " exceeds the inline mesh animation byte budget";
                        animationBytes += sample.positions.size() * bytesPerPosition;
                        if (sample.normals.size() >
                                chronon3d::render_plan::kInlineMeshMaxAggregateBytes / bytesPerPosition ||
                            animationBytes > chronon3d::render_plan::kInlineMeshMaxAggregateBytes -
                                sample.normals.size() * bytesPerPosition)
                            return owner + " exceeds the inline mesh animation byte budget";
                        animationBytes += sample.normals.size() * bytesPerPosition;
                        for (const auto& position : sample.positions)
                            for (const float component : position)
                                if (!std::isfinite(component) || std::abs(component) > 1'000'000.f)
                                    return owner + " has animated mesh positions outside the finite contract range";
                        for (const auto& normal : sample.normals)
                            for (const float component : normal)
                                if (!std::isfinite(component) || std::abs(component) > 1.f)
                                    return owner + " has animated mesh normals outside [-1, 1]";
                    }
                }
                if (mesh.material) {
                    const auto& material = *mesh.material;
                    if (!std::isfinite(material.diffuse) || material.diffuse < 0.f || material.diffuse > 1.f ||
                        !std::isfinite(material.specular) || material.specular < 0.f || material.specular > 1.f ||
                        !std::isfinite(material.shininess) || material.shininess < 1.f || material.shininess > 1024.f ||
                        !std::isfinite(material.rim_intensity) || material.rim_intensity < 0.f || material.rim_intensity > 16.f)
                        return owner + " has inline mesh material values outside their supported ranges";
                    if (material.base_color)
                        for (const float component : *material.base_color)
                            if (!std::isfinite(component) || component < 0.f || component > 1.f)
                                return owner + " has an inline mesh base color outside [0, 1]";
                    if ((material.metallic && (!std::isfinite(*material.metallic) || *material.metallic < 0.f || *material.metallic > 1.f)) ||
                        (material.roughness && (!std::isfinite(*material.roughness) || *material.roughness < 0.f || *material.roughness > 1.f)))
                        return owner + " has inline mesh PBR values outside [0, 1]";
                    if (material.emissive)
                        for (const float component : *material.emissive)
                            if (!std::isfinite(component) || component < 0.f || component > 10000.f)
                                return owner + " has inline mesh emissive values outside [0, 10000]";
                    if (material.alpha_cutoff && (!std::isfinite(*material.alpha_cutoff) || *material.alpha_cutoff < 0.f || *material.alpha_cutoff > 1.f))
                        return owner + " has an inline mesh alpha cutoff outside [0, 1]";
                }
            } else if (layer.type == LayerType::Mesh) {
                return owner + " is a mesh layer with no inline geometry";
            }
            if (layer.particle_emitter) {
                if (layer.type != LayerType::Particles)
                    return owner + " carries a particle emitter but is not a particles layer";
                const auto& particles = *layer.particle_emitter;
                if (const std::string error = chronon3d::validate_particle_emitter(particles); !error.empty())
                    return owner + " has an invalid particle emitter: " + error;
                if (particles.shape != chronon3d::EmitterShape::Path &&
                    (!particles.path_id.empty() || !particles.path_points.empty()))
                    return owner + " carries a particle path on a non-path emitter";
                if (particles.color_ramp.opacity_stops.size() != 0)
                    return owner + " has separate particle opacity stops that the RenderPlan contract cannot carry";
                if (!particles.color_ramp.color_stops.empty()) {
                    if (particles.color_ramp.color_stops.size() < 2 || particles.color_ramp.color_stops.size() > 256)
                        return owner + " has a particle color ramp outside [2, 256] stops";
                    for (const auto& stop : particles.color_ramp.color_stops) {
                        const auto& color = stop.color;
                        if (!std::isfinite(stop.position) || stop.position < 0.f || stop.position > 1.f ||
                            !std::isfinite(color.r) || color.r < 0.f || color.r > 1.f ||
                            !std::isfinite(color.g) || color.g < 0.f || color.g > 1.f ||
                            !std::isfinite(color.b) || color.b < 0.f || color.b > 1.f ||
                            !std::isfinite(color.a) || color.a < 0.f || color.a > 1.f)
                            return owner + " has particle ramp color data outside [0, 1]";
                    }
                }
                if (schema == chronon3d::render_plan::kRenderPlanSchemaV2 &&
                    (particles.shape != chronon3d::EmitterShape::Box || !particles.path_id.empty() ||
                     !particles.path_points.empty() || !particles.sprite_asset.empty() ||
                     particles.sprite_scale_min != 1.f || particles.sprite_scale_max != 1.f ||
                     particles.rotation_min != 0.f || particles.rotation_max != 0.f ||
                     particles.angular_velocity_min != 0.f || particles.angular_velocity_max != 0.f))
                    return owner + " carries particle fields that require RenderPlan v3";
            }
            if (layer.light) {
                if (layer.type != LayerType::Light)
                    return owner + " carries light data but is not a light layer";
                const auto& light = *layer.light;
                if (!std::isfinite(light.radius) || light.radius <= 0.f ||
                    !std::isfinite(light.intensity) || light.intensity < 0.f || light.intensity > 16.f)
                    return owner + " has light values outside supported bounds";
                const chronon3d::Color color{light.color[0], light.color[1], light.color[2], light.color[3]};
                const auto hex = textSpanColorHex(color);
                if (!hex) return owner + " has light color components outside [0, 1]";
                if (std::abs(color.a - 1.f) > 1e-6f)
                    return owner + " has light alpha that the consumer's RGB color contract cannot represent";
                const auto decoded = chronon3d::render_plan::parse_hex_color(hex->substr(0, 7));
                if (!decoded || std::abs(decoded->r - color.r) > 1e-6f ||
                    std::abs(decoded->g - color.g) > 1e-6f || std::abs(decoded->b - color.b) > 1e-6f)
                    return owner + " has a light color that cannot be represented without quantization";
            }
            const auto maskProblem = [&](const LayerPlan::PathMaskPlan& mask) -> std::string {
                if (mask.type != "path" && mask.type != "rect" && mask.type != "rounded_rect" &&
                    mask.type != "ellipse" && mask.type != "field" && mask.type != "text")
                    return owner + " carries an unsupported mask type";
                if (mask.mode != "add" && mask.mode != "subtract" && mask.mode != "intersect" &&
                    mask.mode != "difference")
                    return owner + " carries an unsupported mask combination mode";
                if (!std::isfinite(mask.feather) || mask.feather < 0.f || mask.feather > 1'000'000.f ||
                    !std::isfinite(mask.expansion) || std::abs(mask.expansion) > 1'000'000.f ||
                    !std::isfinite(mask.opacity) || mask.opacity < 0.f || mask.opacity > 1.f ||
                    !std::isfinite(mask.radius) || mask.radius < 0.f || mask.radius > 500'000.f)
                    return owner + " carries mask parameters outside RenderPlan bounds";
                const auto checkTrack = [&](const std::optional<AnimationTrackPlan>& track, float low, float high) {
                    if (!track) return std::string{};
                    if (track->keyframes.empty() || track->keyframes.size() > 65'536 || track->easing != "linear")
                        return owner + " carries a mask parameter track outside the linear keyframe contract";
                    for (const auto& key : track->keyframes)
                        if (key.frame.integral() < 0 || key.frame.integral() > 1'000'000 ||
                            key.value.size() != 1 || !std::isfinite(key.value.front()) ||
                            key.value.front() < low || key.value.front() > high)
                            return owner + " carries mask parameter keyframes outside bounds";
                    return std::string{};
                };
                for (const auto& [track, low, high] : {
                    std::tuple{&mask.feather_track, 0.f, 1'000'000.f},
                    std::tuple{&mask.expansion_track, -1'000'000.f, 1'000'000.f},
                    std::tuple{&mask.opacity_track, 0.f, 1.f}}) {
                    const std::string problem = checkTrack(*track, low, high);
                    if (!problem.empty()) return problem;
                }
                if (mask.type == "path") {
                    if (mask.path.empty() || mask.path.size() > 65'536 || mask.path.front().type != ShapePathCommandTypePlan::MoveTo ||
                        mask.path.back().type != ShapePathCommandTypePlan::Close)
                        return owner + " carries a path mask without a bounded closed path";
                    if (!mask.morph_target.empty() && (mask.morph_target.size() != mask.path.size() || !mask.morph_animation))
                        return owner + " carries an incomplete path mask morph";
                    for (const auto& point : mask.morph_target)
                        for (float value : {point.point[0], point.point[1], point.control1[0], point.control1[1], point.control2[0], point.control2[1]})
                            if (!std::isfinite(value) || std::abs(value) > 1'000'000.f)
                                return owner + " carries non-finite or out-of-bounds mask morph coordinates";
                    if (mask.size[0] != 0.f || mask.size[1] != 0.f || mask.radius != 0.f || mask.field)
                        return owner + " carries geometric fields invalid for a path mask";
                    for (const auto& point : mask.path)
                        for (float value : {point.point[0], point.point[1], point.control1[0], point.control1[1], point.control2[0], point.control2[1]})
                            if (!std::isfinite(value) || std::abs(value) > 1'000'000.f)
                                return owner + " carries non-finite or out-of-bounds mask path coordinates";
                } else if (mask.type == "text") {
                    if (mask.text_source.empty() || mask.text_source.size() > 256 || !mask.path.empty() ||
                        !mask.morph_target.empty() || mask.field || mask.size[0] != 0.f || mask.size[1] != 0.f ||
                        mask.radius != 0.f || mask.position[0] != 0.f || mask.position[1] != 0.f)
                        return owner + " carries invalid fields on a text mask";
                } else {
                    if (!std::isfinite(mask.position[0]) || !std::isfinite(mask.position[1]) ||
                        std::abs(mask.position[0]) > 1'000'000.f || std::abs(mask.position[1]) > 1'000'000.f ||
                        !std::isfinite(mask.size[0]) || !std::isfinite(mask.size[1]) ||
                        mask.size[0] <= 0.f || mask.size[1] <= 0.f ||
                        mask.size[0] > 1'000'000.f || mask.size[1] > 1'000'000.f)
                        return owner + " carries mask geometry outside RenderPlan bounds";
                    if (mask.type == "rounded_rect" && mask.radius > std::min(mask.size[0], mask.size[1]) * 0.5f)
                        return owner + " has a rounded mask radius larger than half its smaller dimension";
                    if (mask.type == "ellipse" && mask.radius != 0.f)
                        return owner + " carries radius on an ellipse mask";
                    if (mask.type == "rect" && mask.radius != 0.f)
                        return owner + " carries radius on a rectangular mask";
                    if (mask.type == "field" && !mask.field)
                        return owner + " carries a field mask without a field definition";
                    if (mask.type != "field" && mask.field)
                        return owner + " carries a field definition on a non-field mask";
                }
                if (mask.morph_animation) {
                    const std::string problem = checkTrack(mask.morph_animation, 0.f, 1.f);
                    if (!problem.empty()) return problem;
                }
                return {};
            };
            if (layer.path_mask) {
                const std::string problem = maskProblem(*layer.path_mask);
                if (!problem.empty()) return problem;
            }
            for (const auto& mask : layer.masks) {
                const std::string problem = maskProblem(mask);
                if (!problem.empty()) return problem;
            }
            if (const std::string effect = effectProblem(layer); !effect.empty()) return effect;
            if (const std::string tracks = effectParameterProblem(layer); !tracks.empty()) return tracks;
            if (layer.flow_layout || layer.grid_layout) return owner + " carries a layout group";
            if (layer.follow_path) return owner + " carries a follow-path driver";
            // The plan contract has no layer-level auto-orient: `auto_orient`
            // exists only inside a follow-path driver, which this emitter
            // already refuses above. Writing it on the layer itself made every
            // emitted plan fail the consumer's schema (additionalProperties:
            // false), so a Motion scene could be lowered and never rendered.
            if (layer.auto_orient) return owner + " carries an auto-orient flag, which the plan expresses only inside a follow-path driver";
            if (layer.skew_angle_deg != 0.f || layer.skew_axis_deg != 0.f) return owner + " carries a skew";
            const auto transitionProblem = [&](const std::optional<chronon3d::render_plan::TransitionPlan>& transition) {
                if (!transition) return std::string{};
                static constexpr std::array<std::string_view, 11> kinds{
                    "none", "crossfade", "dip_to_black", "slide", "zoom", "wipe_linear", "smooth_wipe",
                    "circle_iris", "flash", "procedural_remotion", "remotion"};
                if (std::find(kinds.begin(), kinds.end(), transition->kind) == kinds.end() ||
                    transition->duration_frames.integral() < 1 || transition->duration_frames.integral() > 1'000'000 ||
                    transition->delay_frames.integral() < 0 || transition->delay_frames.integral() > 1'000'000 ||
                    transition->id.size() > 256 || !isKnownEasing(transition->easing))
                    return owner + " carries an invalid transition identity, timing, or easing";
                if ((transition->kind == "slide" && !oneOf(transition->direction, {"left", "right", "up", "down"})) ||
                    (transition->kind == "zoom" && !oneOf(transition->direction, {"in", "out"})))
                    return owner + " carries a transition with an invalid direction";
                const auto unit = [](float value) { return std::isfinite(value) && value >= 0.f && value <= 1.f; };
                if (!std::isfinite(transition->distance) || transition->distance < 0.f || transition->distance > 4.f ||
                    !std::isfinite(transition->zoom_amount) || transition->zoom_amount < 0.f || transition->zoom_amount > 1.f ||
                    !std::isfinite(transition->feather) || transition->feather < 0.f || transition->feather > 1.f ||
                    !unit(transition->center[0]) || !unit(transition->center[1]) ||
                    !std::isfinite(transition->seed) || !std::isfinite(transition->speed) || transition->speed <= 0.f ||
                    !std::isfinite(transition->remotion_direction) || !std::isfinite(transition->angle) ||
                    !std::isfinite(transition->dip_px) || transition->dip_px < 0.f || transition->dip_px > 4096.f)
                    return owner + " carries transition parameters outside RenderPlan bounds";
                for (const auto* color : {&transition->color, &transition->inner_color,
                                           &transition->mid_color, &transition->outer_color})
                    if (std::any_of(color->begin(), color->end(), [&](float c) { return !unit(c); }))
                        return owner + " carries a transition color outside [0, 1]";
                return std::string{};
            };
            for (const auto* transition : {&layer.transition_in, &layer.transition_out}) {
                const std::string problem = transitionProblem(*transition);
                if (!problem.empty()) return problem;
            }
            if (layer.transition_in || layer.transition_out)
                return owner + " carries a transition: the current Chronon3D V3 schema declares the field but its validator/consumer is not aligned";
            if (layer.fill_gradient_animation) return owner + " carries a gradient animation";
            const std::string textAnimators = textAnimatorProblem(layer, schema);
            if (!textAnimators.empty()) return textAnimators;
            if (layer.safe_insets) {
                if (schema != chronon3d::render_plan::kRenderPlanSchemaV3)
                    return owner + " carries safe insets, which require RenderPlan v3";
                for (const float inset : *layer.safe_insets)
                    if (!std::isfinite(inset) || inset < 0.f || inset > 1'000'000.f)
                        return owner + " carries safe insets outside the RenderPlan v3 bounds";
            }
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


} // namespace chrononmotion::motion::detail
