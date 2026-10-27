// Shape serialization of the emitter.

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

    } // namespace
}
