#include "chrononmotion/plan/ResolvedFramePlan.hpp"

#include "FramePlanInternal.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>

namespace chrononmotion::plan {

    std::string_view framePlanProblemName(FramePlanProblem problem) {

        switch (problem) {
            case FramePlanProblem::None: return "none";
            case FramePlanProblem::Schema: return "schema";
            case FramePlanProblem::Space: return "space";
            case FramePlanProblem::Canvas: return "canvas";
            case FramePlanProblem::FrameRate: return "frame_rate";
            case FramePlanProblem::TimelineEmpty: return "timeline_empty";
            case FramePlanProblem::TimelineSize: return "timeline_size";
            case FramePlanProblem::TimelineOrder: return "timeline_order";
            case FramePlanProblem::TimelineSubFrame: return "timeline_sub_frame";
            case FramePlanProblem::TimelineSeconds: return "timeline_seconds";
            case FramePlanProblem::ShutterShape: return "shutter_shape";
            case FramePlanProblem::ShutterOffsets: return "shutter_offsets";
            case FramePlanProblem::ShutterCoverage: return "shutter_coverage";
            case FramePlanProblem::CameraCount: return "camera_count";
            case FramePlanProblem::CameraTime: return "camera_time";
            case FramePlanProblem::CameraPose: return "camera_pose";
            case FramePlanProblem::CameraOptics: return "camera_optics";
            case FramePlanProblem::LayerOrder: return "layer_order";
            case FramePlanProblem::LayerParent: return "layer_parent";
            case FramePlanProblem::LayerKind: return "layer_kind";
            case FramePlanProblem::LayerAsset: return "layer_asset";
            case FramePlanProblem::LayerSamples: return "layer_samples";
            case FramePlanProblem::LayerTimestamp: return "layer_timestamp";
            case FramePlanProblem::LayerTransform: return "layer_transform";
            case FramePlanProblem::LayerOpacity: return "layer_opacity";
            case FramePlanProblem::Digest: return "digest";
        }
        return "unknown";
    }

    namespace {

        /// Locations and values, never a bare "invalid plan": a consumer that
        /// rejects a plan has to say which field it rejected.
        [[nodiscard]] std::string numberText(double value) {

            char buffer[32];
            std::snprintf(buffer, sizeof(buffer), "%.9g", value);
            return buffer;
        }

        [[nodiscard]] FramePlanDiagnostic reject(FramePlanProblem code, std::string detail) {

            return FramePlanDiagnostic{code, std::move(detail)};
        }

        [[nodiscard]] bool finiteFloats(const float* values, std::size_t count) {

            return std::all_of(values, values + count, [](float value) { return std::isfinite(value); });
        }

        /// Relative comparison with an absolute floor, so a timestamp of 0 s is
        /// not held to a vanishing tolerance while frame 1000 is not excused one.
        [[nodiscard]] bool secondsMatch(double stored, double expected) {

            const double scale = std::max(1.0, std::abs(expected));
            return std::abs(stored - expected) <= 1e-9 * scale;
        }

        [[nodiscard]] bool timestampMatches(const PlanTimestamp& a, const PlanTimestamp& b) {

            return a.frame == b.frame && a.subFrameUnits == b.subFrameUnits &&
                   secondsMatch(a.seconds, b.seconds);
        }

        [[nodiscard]] bool frameRateValid(std::uint32_t numerator, std::uint32_t denominator) {

            if (numerator == 0 || denominator == 0) return false;
            const double rate = static_cast<double>(numerator) / static_cast<double>(denominator);
            return std::isfinite(rate) && rate > 0.0 && rate <= 1000.0;
        }

        [[nodiscard]] std::optional<FramePlanDiagnostic> verifyCanvas(const ResolvedFramePlan& plan) {

            if (plan.width == 0 || plan.height == 0)
                return reject(FramePlanProblem::Canvas, "canvas is " + std::to_string(plan.width) + "x" +
                                                            std::to_string(plan.height));
            constexpr std::uint32_t kMaxCanvas = 65536;
            if (plan.width > kMaxCanvas || plan.height > kMaxCanvas)
                return reject(FramePlanProblem::Canvas, "canvas " + std::to_string(plan.width) + "x" +
                                                            std::to_string(plan.height) + " exceeds " +
                                                            std::to_string(kMaxCanvas));
            return std::nullopt;
        }

        [[nodiscard]] std::optional<FramePlanDiagnostic> verifyTimeline(const ResolvedFramePlan& plan) {

            if (plan.frameCount < 1)
                return reject(FramePlanProblem::TimelineEmpty, "frame_count is " +
                                                                   std::to_string(plan.frameCount));

            const auto expectedSize = static_cast<std::size_t>(plan.frameCount);
            if (plan.timeline.size() != expectedSize)
                return reject(FramePlanProblem::TimelineSize, "timeline holds " +
                                                                  std::to_string(plan.timeline.size()) +
                                                                  " entries for frame_count " +
                                                                  std::to_string(plan.frameCount));

            for (std::size_t index = 0; index < plan.timeline.size(); ++index) {
                const PlanTimestamp& time = plan.timeline[index];
                const auto where = "timeline[" + std::to_string(index) + "]";

                if (time.subFrameUnits < 0 || time.subFrameUnits >= kSubFrameUnits)
                    return reject(FramePlanProblem::TimelineSubFrame,
                                  where + ".sub_frame_units is " + std::to_string(time.subFrameUnits) +
                                      ", outside [0, " + std::to_string(kSubFrameUnits) + ")");

                const int expectedFrame = plan.firstFrame + static_cast<int>(index);
                if (time.frame != expectedFrame)
                    return reject(FramePlanProblem::TimelineOrder,
                                  where + ".frame is " + std::to_string(time.frame) + ", expected " +
                                      std::to_string(expectedFrame));

                if (index > 0 && time.absoluteUnits() <= plan.timeline[index - 1].absoluteUnits())
                    return reject(FramePlanProblem::TimelineOrder,
                                  where + " does not advance past timeline[" + std::to_string(index - 1) + "]");

                const double expectedSeconds =
                    planSeconds(time.frame, time.subFrameUnits, plan.fpsNumerator, plan.fpsDenominator);
                if (!std::isfinite(time.seconds) || !secondsMatch(time.seconds, expectedSeconds))
                    return reject(FramePlanProblem::TimelineSeconds,
                                  where + ".seconds is " + numberText(time.seconds) + ", expected " +
                                      numberText(expectedSeconds));
            }
            return std::nullopt;
        }

        [[nodiscard]] std::optional<FramePlanDiagnostic> verifyShutter(const ResolvedFramePlan& plan) {

            if (!plan.shutter) return std::nullopt;

            const PlanShutter& shutter = *plan.shutter;
            if (!std::isfinite(shutter.angleDegrees) || shutter.angleDegrees <= 0.f ||
                shutter.angleDegrees > 360.f)
                return reject(FramePlanProblem::ShutterShape,
                              "shutter.angle_deg is " + numberText(shutter.angleDegrees) +
                                  ", outside (0, 360]");

            const std::size_t samples = shutter.offsetUnits.size();
            if (samples < 2 || samples > 64)
                return reject(FramePlanProblem::ShutterShape,
                              "shutter.offset_units holds " + std::to_string(samples) +
                                  " samples, outside [2, 64]");

            // Half the window, in sub-frame units. The window is symmetric about
            // the frame: this is exactly what forces the first offset negative
            // and the last positive.
            const auto halfUnits = static_cast<std::int64_t>(
                std::llround(static_cast<double>(shutter.angleDegrees) / 720.0 *
                             static_cast<double>(kSubFrameUnits)));

            if (2 * halfUnits < static_cast<std::int64_t>(samples) - 1)
                return reject(FramePlanProblem::ShutterCoverage,
                              "shutter window of " + numberText(shutter.angleDegrees) + " degrees is " +
                                  std::to_string(2 * halfUnits) + " sub-frame units wide, too narrow for " +
                                  std::to_string(samples) + " distinct samples");

            std::optional<FramePlanDiagnostic> failure;
            for (std::size_t index = 0; index < samples; ++index) {
                const std::int32_t units = shutter.offsetUnits[index];
                if (std::abs(static_cast<std::int64_t>(units)) > halfUnits)
                    return reject(FramePlanProblem::ShutterOffsets,
                                  "shutter.offset_units[" + std::to_string(index) + "] is " +
                                      std::to_string(units) + ", outside the window +/-" +
                                      std::to_string(halfUnits) + " units");
                if (index > 0 && units <= shutter.offsetUnits[index - 1])
                    return reject(FramePlanProblem::ShutterOffsets,
                                  "shutter.offset_units[" + std::to_string(index) +
                                      "] does not advance past the previous sample");
            }

            if (shutter.offsetUnits.front() >= 0 || shutter.offsetUnits.back() <= 0)
                return reject(FramePlanProblem::ShutterOffsets,
                              "shutter window spans [" +
                                  std::to_string(shutter.offsetUnits.front()) + ", " +
                                  std::to_string(shutter.offsetUnits.back()) +
                                  "], expected a symmetric bracket of the frame");
            return failure;
        }

        [[nodiscard]] std::optional<FramePlanDiagnostic> verifyCamera(const ResolvedFramePlan& plan) {

            const auto expectedSize = static_cast<std::size_t>(plan.frameCount);
            if (plan.camera.size() != expectedSize)
                return reject(FramePlanProblem::CameraCount, "camera holds " +
                                                                 std::to_string(plan.camera.size()) +
                                                                 " samples for frame_count " +
                                                                 std::to_string(plan.frameCount));

            for (std::size_t index = 0; index < plan.camera.size(); ++index) {
                const PlanCameraSample& sample = plan.camera[index];
                const auto where = "camera[" + std::to_string(index) + "]";

                if (!timestampMatches(sample.time, plan.timeline[index]))
                    return reject(FramePlanProblem::CameraTime,
                                  where + ".time is frame " + std::to_string(sample.time.frame) + " + " +
                                      std::to_string(sample.time.subFrameUnits) +
                                      " units, expected the timeline entry " +
                                      std::to_string(plan.timeline[index].frame) + " + " +
                                      std::to_string(plan.timeline[index].subFrameUnits));

                if (!finiteFloats(sample.position.data(), sample.position.size()) ||
                    !finiteFloats(sample.orientation.data(), sample.orientation.size()))
                    return reject(FramePlanProblem::CameraPose, where + " holds a non-finite pose");

                const double norm = std::sqrt(static_cast<double>(sample.orientation[0]) * sample.orientation[0] +
                                              static_cast<double>(sample.orientation[1]) * sample.orientation[1] +
                                              static_cast<double>(sample.orientation[2]) * sample.orientation[2] +
                                              static_cast<double>(sample.orientation[3]) * sample.orientation[3]);
                if (std::abs(norm - 1.0) > 1e-4)
                    return reject(FramePlanProblem::CameraPose,
                                  where + ".orientation has length " + numberText(norm) +
                                      ", expected a unit quaternion");

                const PlanLens& lens = sample.lens;
                if (!std::isfinite(sample.fovDeg) || sample.fovDeg <= 0.f || sample.fovDeg >= 180.f)
                    return reject(FramePlanProblem::CameraOptics,
                                  where + ".fov_deg is " + numberText(sample.fovDeg) + ", outside (0, 180)");
                if (!std::isfinite(lens.focalLengthMm) || lens.focalLengthMm < 0.f || lens.focalLengthMm > 1000.f)
                    return reject(FramePlanProblem::CameraOptics,
                                  where + ".lens.focal_length_mm is " + numberText(lens.focalLengthMm) +
                                      ", outside [0, 1000]");
                if (!std::isfinite(lens.sensorWidthMm) || !std::isfinite(lens.sensorHeightMm) ||
                    lens.sensorWidthMm < 0.f || lens.sensorHeightMm < 0.f ||
                    lens.sensorWidthMm > 200.f || lens.sensorHeightMm > 200.f)
                    return reject(FramePlanProblem::CameraOptics,
                                  where + ".lens sensor gate is " + numberText(lens.sensorWidthMm) + "x" +
                                      numberText(lens.sensorHeightMm) + " mm, outside [0, 200]");
                if (!finiteFloats(lens.lensShift.data(), lens.lensShift.size()) ||
                    std::abs(lens.lensShift[0]) > 1.f || std::abs(lens.lensShift[1]) > 1.f)
                    return reject(FramePlanProblem::CameraOptics,
                                  where + ".lens.lens_shift is " + numberText(lens.lensShift[0]) + ", " +
                                      numberText(lens.lensShift[1]) + ", outside [-1, 1]");
                if (!std::isfinite(lens.fStop) ||
                    (lens.fStop != 0.f && (lens.fStop < 0.7f || lens.fStop > 32.f)))
                    return reject(FramePlanProblem::CameraOptics,
                                  where + ".lens.f_stop is " + numberText(lens.fStop) +
                                      ", expected 0 (not authored) or [0.7, 32]");
                if (!std::isfinite(lens.focusDistance) || lens.focusDistance < 0.f)
                    return reject(FramePlanProblem::CameraOptics,
                                  where + ".lens.focus_distance is " + numberText(lens.focusDistance) +
                                      ", expected a non-negative distance");
            }
            return std::nullopt;
        }

        [[nodiscard]] std::optional<FramePlanDiagnostic> verifyLayers(const ResolvedFramePlan& plan) {

            const auto expectedSize = static_cast<std::size_t>(plan.frameCount);
            std::unordered_set<std::uint32_t> seen;

            for (std::size_t index = 0; index < plan.layers.size(); ++index) {
                const PlanLayer& layer = plan.layers[index];
                const auto where = "layers[" + std::to_string(index) + "]";

                if (layer.order != static_cast<int>(index))
                    return reject(FramePlanProblem::LayerOrder,
                                  where + ".order is " + std::to_string(layer.order) +
                                      ", expected the list position " + std::to_string(index));
                if (layer.id == 0)
                    return reject(FramePlanProblem::LayerOrder, where + ".id is 0, which is the scene root");
                if (!seen.insert(layer.id).second)
                    return reject(FramePlanProblem::LayerOrder,
                                  where + " repeats layer id " + std::to_string(layer.id));
                if (layer.parent != 0 && seen.count(layer.parent) == 0)
                    return reject(FramePlanProblem::LayerParent,
                                  where + ".parent is " + std::to_string(layer.parent) +
                                      ", which is neither the root nor an earlier layer");

                switch (layer.kind) {
                    case PlanLayerKind::Null:
                        if (!layer.asset.empty())
                            return reject(FramePlanProblem::LayerKind,
                                          where + " is a null layer carrying asset \"" + layer.asset + "\"");
                        break;
                    case PlanLayerKind::Text:
                    case PlanLayerKind::Image:
                    case PlanLayerKind::Video:
                        if (layer.asset.empty())
                            return reject(FramePlanProblem::LayerAsset,
                                          where + " is a " + std::string(planLayerKindName(layer.kind)) +
                                              " layer with no asset");
                        break;
                    case PlanLayerKind::Mesh:
                        // An inline-geometry mesh has no asset id by design; the
                        // renderer-facing handles below carry its identity.
                        break;
                    case PlanLayerKind::Particles:
                        // Particles are procedural content; no asset id is required.
                        break;
                }

                if (layer.samples.size() != expectedSize)
                    return reject(FramePlanProblem::LayerSamples,
                                  where + " holds " + std::to_string(layer.samples.size()) +
                                      " samples for frame_count " + std::to_string(plan.frameCount));

                for (std::size_t sampleIndex = 0; sampleIndex < layer.samples.size(); ++sampleIndex) {
                    const PlanLayerSample& sample = layer.samples[sampleIndex];
                    const auto sampleWhere = where + ".samples[" + std::to_string(sampleIndex) + "]";

                    if (!timestampMatches(sample.time, plan.timeline[sampleIndex]))
                        return reject(FramePlanProblem::LayerTimestamp,
                                      sampleWhere + ".time is frame " + std::to_string(sample.time.frame) + " + " +
                                          std::to_string(sample.time.subFrameUnits) +
                                          " units, expected the timeline entry");

                    if (!finiteFloats(sample.world.data(), sample.world.size()))
                        return reject(FramePlanProblem::LayerTransform,
                                      sampleWhere + ".world holds a non-finite element");

                    if (!std::isfinite(sample.opacity) || sample.opacity < 0.f || sample.opacity > 1.f)
                        return reject(FramePlanProblem::LayerOpacity,
                                      sampleWhere + ".opacity is " + numberText(sample.opacity) +
                                          ", outside [0, 1]");
                }
            }
            return std::nullopt;
        }

    }// namespace

    std::optional<FramePlanDiagnostic> verifyFramePlan(const ResolvedFramePlan& plan) {

        if (plan.schema != kResolvedFramePlanSchema)
            return reject(FramePlanProblem::Schema, "schema is \"" + plan.schema + "\", expected \"" +
                                                        std::string(kResolvedFramePlanSchema) + "\"");
        if (plan.space != kResolvedFramePlanSpace)
            return reject(FramePlanProblem::Space, "space is \"" + plan.space + "\", expected \"" +
                                                      std::string(kResolvedFramePlanSpace) + "\"");
        if (!frameRateValid(plan.fpsNumerator, plan.fpsDenominator))
            return reject(FramePlanProblem::FrameRate,
                          "fps is " + std::to_string(plan.fpsNumerator) + "/" +
                              std::to_string(plan.fpsDenominator) + ", expected a finite rate in (0, 1000]");

        if (auto failure = verifyCanvas(plan)) return failure;
        if (auto failure = verifyTimeline(plan)) return failure;
        if (auto failure = verifyShutter(plan)) return failure;
        if (auto failure = verifyCamera(plan)) return failure;
        if (auto failure = verifyLayers(plan)) return failure;

        if (plan.digest == 0)
            return reject(FramePlanProblem::Digest,
                          "digest is not stamped for plan_id \"" + plan.planId + "\"");
        if (plan.digest != computeFramePlanDigest(plan))
            return reject(FramePlanProblem::Digest,
                          "digest is " + std::to_string(plan.digest) + ", recomputed " +
                              std::to_string(computeFramePlanDigest(plan)));
        return std::nullopt;
    }

}// namespace chrononmotion::plan
