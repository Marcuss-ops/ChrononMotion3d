// Effects, meshes, particles, light, and mask serialization of the emitter.

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

        void writeMaskTrack(detail::Writer& writer, const AnimationTrackPlan& track) {
            writer.beginObject();
            writer.key("easing"); writer.text(track.easing);
            writer.key("keyframes"); writeKeyframes(writer, track.keyframes, true);
            writer.endObject();
        }

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

    } // namespace
}
