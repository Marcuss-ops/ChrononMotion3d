#include "chrononmotion/plan/ResolvedFramePlan.hpp"

#include "FramePlanInternal.hpp"

#include "chrononmotion/motion/CameraRig.hpp"
#include "chrononmotion/motion/Compositing.hpp"
#include "chrononmotion/motion/MotionScene.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <unordered_set>
#include <utility>
#include <vector>

namespace chrononmotion::plan {

    namespace {

        /// The rates that actually occur in delivery, plus the NTSC pair. A scene
        /// rate is a float, so the search is "the closest known rate within
        /// tolerance", not "the exact value".
        constexpr std::pair<std::uint32_t, std::uint32_t> kKnownRates[] = {
            {24000, 1001},
            {24, 1},
            {25, 1},
            {30000, 1001},
            {30, 1},
            {48000, 1001},
            {48, 1},
            {50, 1},
            {60000, 1001},
            {60, 1},
            {100, 1},
            {120, 1},
        };

        [[nodiscard]] PlanLayerKind planKindOf(const motion::Layer& layer) {

            if (layer.isMesh()) return PlanLayerKind::Mesh;
            switch (layer.content.kind) {
                case motion::ContentKind::None: return PlanLayerKind::Null;
                case motion::ContentKind::Text: return PlanLayerKind::Text;
                case motion::ContentKind::Image: return PlanLayerKind::Image;
                case motion::ContentKind::Video: return PlanLayerKind::Video;
                case motion::ContentKind::Mesh: return PlanLayerKind::Mesh;
                case motion::ContentKind::Particles: return PlanLayerKind::Particles;
            }
            return PlanLayerKind::Null;
        }

        /// Motion stores a column-major 4x4; the contract is row-major, so this is
        /// a documented transpose and not a reinterpretation of the numbers.
        [[nodiscard]] std::array<float, 16> rowMajorOf(const Matrix4& matrix) {

            std::array<float, 16> out{};
            for (std::size_t column = 0; column < 4; ++column) {
                for (std::size_t row = 0; row < 4; ++row) {
                    out[row * 4 + column] = matrix.elements[column * 4 + row];
                }
            }
            return out;
        }

        [[nodiscard]] std::array<float, 3> toArray(const Vector3& value) {

            return {value.x, value.y, value.z};
        }

        [[nodiscard]] std::array<float, 4> toArray(const Quaternion& value) {

            return {value.x, value.y, value.z, value.w};
        }

    }// namespace

    std::pair<std::uint32_t, std::uint32_t> rationalFrameRate(float fps) {

        if (!std::isfinite(fps) || fps <= 0.f)
            throw std::invalid_argument("rationalFrameRate: fps must be finite and positive");

        std::pair<std::uint32_t, std::uint32_t> best{kKnownRates[0].first, kKnownRates[0].second};
        double bestError = std::numeric_limits<double>::infinity();
        for (const auto& candidate : kKnownRates) {
            const double rate = static_cast<double>(candidate.first) / static_cast<double>(candidate.second);
            const double error = std::abs(rate - static_cast<double>(fps)) / static_cast<double>(fps);
            if (error < bestError) {
                bestError = error;
                best = candidate;
            }
        }

        // 1e-4 relative: enough for an NTSC rate stored as a float (23.976 is
        // 23.976 on the wire and 24000/1001 here), tight enough that a scene at a
        // genuinely different rate is rejected instead of quietly retimed.
        if (bestError > 1e-4)
            throw std::invalid_argument("rationalFrameRate: " + std::to_string(fps) +
                                        " does not match a supported delivery rate");
        return best;
    }

    ResolvedFramePlan resolveFramePlan(motion::MotionScene& scene, const motion::CameraRig& rig,
                                       const ResolveFramePlanOptions& options) {

        if (options.width == 0 || options.height == 0)
            throw std::invalid_argument("resolveFramePlan: canvas must be positive");
        if (options.frameCount < 1)
            throw std::invalid_argument("resolveFramePlan: frame_count must be at least 1");

        const auto [fpsNumerator, fpsDenominator] = rationalFrameRate(scene.fps());
        if (options.fpsNumerator != 0) {
            if (options.fpsDenominator == 0)
                throw std::invalid_argument("resolveFramePlan: fps_denominator must be positive");
            const double requested = static_cast<double>(options.fpsNumerator) /
                                     static_cast<double>(options.fpsDenominator);
            const double sceneRate = static_cast<double>(fpsNumerator) /
                                     static_cast<double>(fpsDenominator);
            // The calendar belongs to the scene. A caller may insist on a rate,
            // but only the matching one: two rates in one plan is how a frame
            // lands half a frame off in one consumer.
            if (std::abs(requested - sceneRate) > 1e-4 * sceneRate)
                throw std::invalid_argument("resolveFramePlan: requested fps " +
                                            std::to_string(options.fpsNumerator) + "/" +
                                            std::to_string(options.fpsDenominator) +
                                            " does not match the scene rate " +
                                            std::to_string(fpsNumerator) + "/" +
                                            std::to_string(fpsDenominator));
        }

        ResolvedFramePlan plan;
        plan.planId = options.planId.empty() ? scene.name() : options.planId;
        plan.width = options.width;
        plan.height = options.height;
        plan.fpsNumerator = fpsNumerator;
        plan.fpsDenominator = fpsDenominator;
        plan.firstFrame = options.firstFrame;
        plan.frameCount = options.frameCount;

        plan.timeline.reserve(static_cast<std::size_t>(plan.frameCount));
        for (int index = 0; index < plan.frameCount; ++index) {
            PlanTimestamp time;
            time.frame = options.firstFrame + index;
            time.subFrameUnits = 0;
            time.seconds = planSeconds(time.frame, time.subFrameUnits, fpsNumerator, fpsDenominator);
            plan.timeline.push_back(time);
        }

        if (options.includeShutterSamples && scene.hasShutter()) {
            const motion::ShutterSettings& shutter = *scene.shutter();
            PlanShutter resolved;
            resolved.angleDegrees = shutter.shutterAngleDegrees;

            const auto samples = static_cast<std::int64_t>(shutter.samples);
            const auto halfUnits = static_cast<std::int64_t>(
                std::llround(static_cast<double>(shutter.shutterAngleDegrees) / 720.0 *
                             static_cast<double>(kSubFrameUnits)));
            if (samples < 2)
                throw std::invalid_argument("resolveFramePlan: shutter needs at least 2 samples");
            if (2 * halfUnits < samples - 1)
                throw std::invalid_argument("resolveFramePlan: shutter window of " +
                                            std::to_string(shutter.shutterAngleDegrees) +
                                            " degrees cannot hold " + std::to_string(samples) +
                                            " distinct samples at 1e-6 frame resolution");

            resolved.offsetUnits.reserve(static_cast<std::size_t>(samples));
            for (std::int64_t index = 0; index < samples; ++index) {
                // Symmetric about the frame, endpoints included: a 180 degree
                // shutter at 12 samples is -0.25..+0.25 frames in 11 steps.
                const auto units = static_cast<std::int32_t>(
                    -halfUnits + (2 * halfUnits * index) / (samples - 1));
                resolved.offsetUnits.push_back(units);
            }
            plan.shutter = std::move(resolved);
        }

        // Parents first, and only the layers that survive the filter. A child of
        // an excluded controller is re-based on its nearest included ancestor:
        // the world matrices below are already composed, so the parent link is
        // hierarchy for the consumer, never a recomputation.
        const std::vector<motion::Layer::Id> evaluationOrder = scene.evaluationOrder();
        std::vector<const motion::Layer*> selected;
        selected.reserve(evaluationOrder.size());
        for (const motion::Layer::Id id : evaluationOrder) {
            const motion::Layer* layer = scene.findLayer(id);
            if (layer == nullptr)
                throw std::invalid_argument("resolveFramePlan: evaluation order names layer " +
                                            std::to_string(id) + ", which is not in the scene");
            if (planKindOf(*layer) == PlanLayerKind::Null && !options.includeNullLayers) continue;
            selected.push_back(layer);
        }

        std::unordered_set<motion::Layer::Id> retained;
        for (const motion::Layer* layer : selected) retained.insert(layer->id);

        plan.layers.reserve(selected.size());
        for (std::size_t index = 0; index < selected.size(); ++index) {
            const motion::Layer& layer = *selected[index];
            PlanLayer out;
            out.id = layer.id;
            out.name = layer.name;
            out.kind = planKindOf(layer);
            // Content identity is the motion-side asset id; a mesh layer carries
            // its model identity on the attachment instead.
            out.asset = layer.content.id.empty() ? layer.mesh.assetId : layer.content.id;
            out.fingerprint = layer.content.metrics.fingerprint;
            out.baseColorTexture = layer.mesh.baseColorTextureAsset;
            out.metallicRoughnessTexture = layer.mesh.metallicRoughnessTextureAsset;
            out.parent = motion::Layer::kRoot;
            motion::Layer::Id ancestor = layer.parentId;
            while (ancestor != motion::Layer::kRoot && retained.count(ancestor) == 0) {
                const motion::Layer* parent = scene.findLayer(ancestor);
                if (parent == nullptr)
                    throw std::invalid_argument("resolveFramePlan: layer " + std::to_string(layer.id) +
                                                " has parent " + std::to_string(ancestor) +
                                                ", which is not in the scene");
                ancestor = parent->parentId;
            }
            out.parent = ancestor;
            out.order = static_cast<int>(index);
            plan.layers.push_back(std::move(out));
        }

        const auto cameraParent = rig.layer().parentId;
        Matrix4 cameraParentWorld;
        if (cameraParent != motion::Layer::kRoot) {
            if (scene.findLayer(cameraParent) == nullptr)
                throw std::invalid_argument("resolveFramePlan: camera parent " +
                                            std::to_string(cameraParent) + " is not a layer of the scene");
        }

        const float sceneFps = scene.fps();
        plan.camera.reserve(static_cast<std::size_t>(plan.frameCount));

        // Depth compositing is authored, not inferred: a stack-mode scene keeps
        // emitting exactly the document it emitted before this contract existed.
        const bool resolveCompositeOrder =
            scene.compositeMode() == motion::CompositeMode::Depth;
        if (resolveCompositeOrder)
            plan.compositeOrder.reserve(static_cast<std::size_t>(plan.frameCount));

        for (int index = 0; index < plan.frameCount; ++index) {
            const PlanTimestamp& clock = plan.timeline[static_cast<std::size_t>(index)];

            // The instant is (frame, fraction) with the scene's own rate: no
            // multiply by a float rate, so no frame can drift across a boundary.
            const motion::MotionTime time{clock.frame, sceneFps,
                                          static_cast<float>(clock.subFrameUnits) /
                                              static_cast<float>(kSubFrameUnits)};
            scene.evaluate(time);
            const float seconds = time.pts();

            if (cameraParent != motion::Layer::kRoot) cameraParentWorld = scene.worldMatrix(cameraParent);
            const motion::CameraPose pose = rig.sampleWorld(seconds, cameraParentWorld);

            PlanCameraSample camera;
            camera.time = clock;
            camera.position = toArray(pose.position);
            camera.orientation = toArray(pose.orientation);
            camera.fovDeg = pose.fov;
            camera.lens.focalLengthMm = pose.focalLength;
            camera.lens.sensorWidthMm = pose.sensorWidth;
            camera.lens.sensorHeightMm = pose.sensorHeight;
            camera.lens.lensShift = {pose.lensShift.x, pose.lensShift.y};
            camera.lens.fStop = pose.fStop;
            camera.lens.focusDistance = pose.focusDistance;
            plan.camera.push_back(camera);

            for (PlanLayer& layer : plan.layers) {
                const motion::LayerState& state = scene.state(layer.id);
                PlanLayerSample sample;
                sample.time = clock;
                sample.world = rowMajorOf(state.world);
                sample.opacity = state.worldOpacity;
                sample.visible = state.visible;
                layer.samples.push_back(sample);
            }

            if (resolveCompositeOrder) {
                const motion::CompositeStack stack =
                    motion::resolveCompositeStack(scene, pose);
                std::vector<std::uint32_t> order;
                order.reserve(stack.size());
                for (const motion::CompositeDraw& draw : stack.backToFront)
                    order.push_back(draw.layerId);
                plan.compositeOrder.push_back(std::move(order));
            }
        }

        // Stamp, then hold the producer itself to the consumer's rules.
        plan.digest = computeFramePlanDigest(plan);
        if (const auto problem = verifyFramePlan(plan))
            throw std::invalid_argument("resolveFramePlan: " + std::string(framePlanProblemName(problem->code)) +
                                        ": " + problem->detail);
        return plan;
    }

}// namespace chrononmotion::plan
