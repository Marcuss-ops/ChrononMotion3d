#include "FramePlanInternal.hpp"

#include "../motion/JsonWriter.hpp"

#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <string_view>

namespace chrononmotion::plan {

    std::string_view planLayerKindName(PlanLayerKind kind) {

        switch (kind) {
            case PlanLayerKind::Null: return "null";
            case PlanLayerKind::Text: return "text";
            case PlanLayerKind::Image: return "image";
            case PlanLayerKind::Video: return "video";
            case PlanLayerKind::Mesh: return "mesh";
        }
        return "null";
    }

    std::string resolvedFramePlanToJson(const ResolvedFramePlan& plan, int indentSpaces) {

        // The document is the contract. Emitting a plan that violates it would
        // hand a backend numbers it is required to reject, one process later.
        if (const auto problem = verifyFramePlan(plan)) {
            throw std::invalid_argument("resolved frame plan: " + problem->detail);
        }
        return detail::renderFramePlanText(plan, indentSpaces, true);
    }

}// namespace chrononmotion::plan

namespace chrononmotion::plan::detail {

    namespace {

        using motion::detail::Writer;

        /// Shortest decimal that reads back as the same float, and decimal rather
        /// than exponent notation for the magnitudes this contract carries.
        ///
        /// The shared writer's `%g` rule is right for a 0.85 weight and wrong for
        /// a lens: it spells 180 as `1.8e+02`, which is the same number and a
        /// worse contract. The digest covers this text, so the spelling is part
        /// of the format.
        [[nodiscard]] std::string planNumber(float value) {

            if (!std::isfinite(value))
                throw std::runtime_error("resolved frame plan: non-finite number reached the JSON writer");
            if (value == 0.f) return "0";

            char buffer[64];
            for (int precision = 1; precision <= 9; ++precision) {
                std::snprintf(buffer, sizeof(buffer), "%.*g", precision, static_cast<double>(value));
                if (static_cast<float>(std::strtod(buffer, nullptr)) != value) continue;
                if (std::string(buffer).find('e') == std::string::npos) return buffer;
                break;
            }

            const double magnitude = std::fabs(static_cast<double>(value));
            if (magnitude >= 1e-6 && magnitude < 1e9) {
                for (int decimals = 0; decimals <= 12; ++decimals) {
                    std::snprintf(buffer, sizeof(buffer), "%.*f", decimals, static_cast<double>(value));
                    if (static_cast<float>(std::strtod(buffer, nullptr)) == value) return buffer;
                }
            }

            std::snprintf(buffer, sizeof(buffer), "%.9g", static_cast<double>(value));
            return buffer;
        }

        void emitNumber(Writer& writer, float value) { writer.raw(planNumber(value).c_str()); }

        void writeTimestamp(Writer& writer, const PlanTimestamp& time) {

            writer.beginObject();
            writer.key("frame");
            writer.integer(time.frame);
            writer.key("sub_frame_units");
            writer.integer(time.subFrameUnits);
            writer.key("seconds");
            writer.exactNumber(time.seconds);
            writer.endObject();
        }

        void writeLens(Writer& writer, const PlanLens& lens) {

            writer.beginObject();
            writer.key("focal_length_mm");
            emitNumber(writer, lens.focalLengthMm);
            writer.key("sensor_width_mm");
            emitNumber(writer, lens.sensorWidthMm);
            writer.key("sensor_height_mm");
            emitNumber(writer, lens.sensorHeightMm);
            writer.key("lens_shift");
            writer.beginArray();
            writer.item();
            emitNumber(writer, lens.lensShift[0]);
            writer.item();
            emitNumber(writer, lens.lensShift[1]);
            writer.endArray();
            writer.key("f_stop");
            emitNumber(writer, lens.fStop);
            writer.key("focus_distance");
            emitNumber(writer, lens.focusDistance);
            writer.endObject();
        }

        void writeNumberArray(Writer& writer, const float* values, std::size_t count) {

            writer.beginArray();
            for (std::size_t index = 0; index < count; ++index) {
                writer.item();
                emitNumber(writer, values[index]);
            }
            writer.endArray();
        }

    }// namespace

    std::string renderFramePlanText(const ResolvedFramePlan& plan, int indent, bool includeDigest) {

        Writer writer(indent);
        writer.beginObject();

        writer.key("schema");
        writer.text(plan.schema);
        writer.key("space");
        writer.text(plan.space);
        writer.key("plan_id");
        writer.text(plan.planId);

        writer.key("canvas");
        writer.beginObject();
        writer.key("width");
        writer.unsignedInteger(plan.width);
        writer.key("height");
        writer.unsignedInteger(plan.height);
        writer.endObject();

        writer.key("fps");
        writer.beginObject();
        writer.key("numerator");
        writer.unsignedInteger(plan.fpsNumerator);
        writer.key("denominator");
        writer.unsignedInteger(plan.fpsDenominator);
        writer.endObject();

        writer.key("frames");
        writer.beginObject();
        writer.key("first");
        writer.integer(plan.firstFrame);
        writer.key("count");
        writer.integer(plan.frameCount);
        writer.endObject();

        writer.key("timeline");
        writer.beginArray();
        for (const PlanTimestamp& time : plan.timeline) {
            writer.item();
            writeTimestamp(writer, time);
        }
        writer.endArray();

        writer.key("shutter");
        if (plan.shutter) {
            writer.beginObject();
            writer.key("angle_deg");
            emitNumber(writer, plan.shutter->angleDegrees);
            writer.key("offset_units");
            writer.beginArray();
            for (const std::int32_t units : plan.shutter->offsetUnits) {
                writer.item();
                writer.integer(units);
            }
            writer.endArray();
            writer.endObject();
        } else {
            writer.null();
        }

        writer.key("camera");
        writer.beginArray();
        for (const PlanCameraSample& sample : plan.camera) {
            writer.item();
            writer.beginObject();
            writer.key("time");
            writeTimestamp(writer, sample.time);
            writer.key("position");
            writeNumberArray(writer, sample.position.data(), sample.position.size());
            writer.key("orientation");
            writeNumberArray(writer, sample.orientation.data(), sample.orientation.size());
            writer.key("fov_deg");
            emitNumber(writer, sample.fovDeg);
            writer.key("lens");
            writeLens(writer, sample.lens);
            writer.endObject();
        }
        writer.endArray();

        writer.key("layers");
        writer.beginArray();
        for (const PlanLayer& layer : plan.layers) {
            writer.item();
            writer.beginObject();
            writer.key("id");
            writer.unsignedInteger(layer.id);
            writer.key("name");
            writer.text(layer.name);
            writer.key("kind");
            writer.token(std::string(planLayerKindName(layer.kind)).c_str());
            writer.key("asset");
            writer.text(layer.asset);
            writer.key("fingerprint");
            writer.text(layer.fingerprint);
            writer.key("base_color_texture");
            writer.text(layer.baseColorTexture);
            writer.key("metallic_roughness_texture");
            writer.text(layer.metallicRoughnessTexture);
            writer.key("parent");
            writer.unsignedInteger(layer.parent);
            writer.key("order");
            writer.integer(layer.order);
            writer.key("samples");
            writer.beginArray();
            for (const PlanLayerSample& sample : layer.samples) {
                writer.item();
                writer.beginObject();
                writer.key("time");
                writeTimestamp(writer, sample.time);
                writer.key("world");
                writeNumberArray(writer, sample.world.data(), sample.world.size());
                writer.key("opacity");
                emitNumber(writer, sample.opacity);
                writer.key("visible");
                writer.boolean(sample.visible);
                writer.endObject();
            }
            writer.endArray();
            writer.endObject();
        }
        writer.endArray();

        if (includeDigest) {
            writer.key("digest");
            writer.unsignedInteger(plan.digest);
        }

        writer.endObject();
        return writer.take();
    }

    std::uint64_t fnv1a64(std::string_view bytes) {

        std::uint64_t hash = 14695981039346656037ULL;
        for (const char raw : bytes) {
            hash ^= static_cast<std::uint64_t>(static_cast<unsigned char>(raw));
            hash *= 1099511628211ULL;
        }
        return hash;
    }

}// namespace chrononmotion::plan::detail
