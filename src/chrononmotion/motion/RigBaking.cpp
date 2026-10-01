#include "chrononmotion/motion/RigBaking.hpp"
#include "chrononmotion/motion/MotionScene.hpp"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <stdexcept>

namespace chrononmotion::motion {
    namespace {
        void validateChain(const RigEvaluator& rig, const std::vector<RigNodeId>& ids) {
            if (ids.size() < 2 || ids.size() > 257) throw std::invalid_argument("RigBaking: invalid chain size");
            for (const auto& node : rig.nodes())
                if (!node.constraints.empty()) throw std::invalid_argument("RigBaking: matching requires raw FK graph");
            for (std::size_t i = 0; i < ids.size(); ++i) {
                const auto it = std::lower_bound(rig.nodes().begin(), rig.nodes().end(), ids[i],
                    [](const RigNode& node, RigNodeId id) { return node.id < id; });
                if (it == rig.nodes().end() || it->id != ids[i] ||
                    std::find(ids.begin(), ids.begin() + static_cast<std::ptrdiff_t>(i), ids[i]) != ids.begin() + static_cast<std::ptrdiff_t>(i) ||
                    (i && it->parent != ids[i - 1]))
                    throw std::invalid_argument("RigBaking: invalid parent chain");
            }
        }
        RigPose fromPositions(const RigEvaluator& rig, const RigEvaluation& rest,
                              const std::vector<RigNodeId>& ids, std::span<const Vector3> positions) {
            auto output = rest;
            for (std::size_t joint = 0; joint < ids.size(); ++joint) {
                const auto i = output.index(ids[joint]);
                Vector3 position, scale;
                Quaternion orientation;
                rest.world[i].decompose(position, orientation, scale);
                if (joint + 1 < ids.size()) {
                    Vector3 from = rest.localToWorld(ids[joint + 1], {}) - rest.localToWorld(ids[joint], {});
                    Vector3 to = positions[joint + 1] - positions[joint];
                    if (from.lengthSq() <= 1e-12f || to.lengthSq() <= 1e-12f)
                        throw std::invalid_argument("RigBaking: degenerate segment");
                    Quaternion delta; delta.setFromUnitVectors(from.normalize(), to.normalize());
                    orientation.premultiply(delta).normalize();
                }
                output.world[i].compose(positions[joint], orientation, scale);
            }
            // Parent order is already compiled; untouched descendants follow.
            for (const auto id : rig.evaluationOrder()) {
                if (std::find(ids.begin(), ids.end(), id) != ids.end()) continue;
                const auto i = output.index(id);
                output.world[i] = output.local[i].toMatrix();
                if (rig.nodes()[i].parent) output.world[i].premultiply(output.world[output.index(rig.nodes()[i].parent)]);
            }
            return rig.matchWorldPose(output);
        }
    }
    RigPose evaluateRigSpaces(const RigEvaluator& rig, std::span<const RigAnimatedNode> animation,
                              std::span<const RigSpaceSwitch> switches, float seconds) {
        if (!std::isfinite(seconds) || seconds < 0.f || animation.size() > rig.nodes().size() ||
            switches.size() > rig.nodes().size())
            throw std::invalid_argument("RigSpaces: invalid inputs");
        MotionScene scene("rig-spaces", 60.f);
        for (const auto& node : rig.nodes()) {
            if (!node.constraints.empty()) throw std::invalid_argument("RigSpaces: use raw authored graph before constraints");
            auto& layer = scene.addNull(node.id, "rig-node", node.parent);
            layer.transform = node.rest;
        }
        RigNodeId previous = 0;
        for (const auto& node : animation) {
            if (node.id <= previous || !scene.hasLayer(node.id))
                throw std::invalid_argument("RigSpaces: animation IDs must be unique/sorted");
            previous = node.id;
            const auto validate = [](const auto& track) {
                if (!track.modifiers().empty() || track.keys().size() > 1000000)
                    throw std::invalid_argument("RigSpaces: animation must use bounded unlinked tracks");
            };
            validate(node.tracks.position); validate(node.tracks.anchor); validate(node.tracks.rotation);
            validate(node.tracks.scale); validate(node.tracks.orientation); validate(node.tracks.opacity);
            scene.findLayer(node.id)->tracks = node.tracks;
        }
        std::vector<RigNodeId> switched;
        std::size_t eventBudget = 0;
        for (const auto& spec : switches) {
            auto* layer = scene.findLayer(spec.node);
            if (!layer || std::find(switched.begin(), switched.end(), spec.node) != switched.end() ||
                spec.spaces.empty() || spec.spaces.size() > 256 || !spec.preserveWorldTransform)
                throw std::invalid_argument("RigSpaces: invalid switch node/spaces/preservation");
            switched.push_back(spec.node);
            std::vector<RigNodeId> unique = spec.spaces;
            std::sort(unique.begin(), unique.end());
            if (std::adjacent_find(unique.begin(), unique.end()) != unique.end())
                throw std::invalid_argument("RigSpaces: duplicate space");
            for (const auto space : spec.spaces)
                if (space && !scene.hasLayer(space)) throw std::invalid_argument("RigSpaces: missing space");
            eventBudget += spec.switches.size();
            if (eventBudget > 4096) throw std::invalid_argument("RigSpaces: event budget exceeded");
            for (const auto& event : spec.switches) {
                if (event.activeSpace >= spec.spaces.size()) throw std::invalid_argument("RigSpaces: invalid active space");
                layer->spaceSwitches.push_back({event.seconds, spec.spaces[event.activeSpace]});
            }
        }
        if (!scene.validate().empty()) throw std::invalid_argument("RigSpaces: invalid switch graph");
        scene.evaluatePtsExact(seconds);
        auto result = rig.evaluateFK();
        for (std::size_t i = 0; i < rig.nodes().size(); ++i) {
            result.local[i] = scene.state(rig.nodes()[i].id).local;
            result.world[i] = scene.worldMatrix(rig.nodes()[i].id);
        }
        return rig.matchWorldPose(result);
    }

    std::vector<RigPose> bakeRigSecondaryMotion(const RigEvaluator& rig, const RigSecondaryMotion& spec,
                                               int firstFrame, int lastFrame, float fps) {
        validateChain(rig, spec.joints);
        if (firstFrame < 0 || lastFrame < firstFrame || !std::isfinite(fps) || fps <= 0.f ||
            static_cast<std::uint64_t>(lastFrame) - static_cast<std::uint64_t>(firstFrame) + 1 >
                1000000 / std::max<std::size_t>(1, rig.nodes().size()))
            throw std::invalid_argument("RigBaking: invalid frame range or pose sample budget");
        const auto rest = rig.evaluateFK();
        VerletChainSpec verlet;
        verlet.gravity = spec.gravity; verlet.damping = spec.damping;
        verlet.iterations = spec.iterations; verlet.fps = fps;
        verlet.stiffness = spec.stiffness; verlet.mass = spec.mass; verlet.force = spec.force;
        verlet.drag = spec.drag; verlet.maxAngle = spec.maxAngle;
        float z = 0.f;
        for (std::size_t joint = 0; joint < spec.joints.size(); ++joint) {
            const auto p = rest.localToWorld(spec.joints[joint], {});
            if (!joint) z = p.z;
            if (std::abs(p.z - z) > 1e-5f) throw std::invalid_argument("RigBaking: secondary chain must be planar XY");
            verlet.joints.push_back({p.x, p.y});
        }
        const auto baked = bakeVerletChain(verlet, firstFrame, lastFrame);
        std::vector<RigPose> result;
        result.reserve(baked.samples.size());
        std::vector<Vector3> positions(spec.joints.size());
        for (const auto& sample : baked.samples) {
            for (std::size_t joint = 0; joint < sample.size(); ++joint)
                positions[joint] = {sample[joint].x, sample[joint].y, z};
            result.push_back(fromPositions(rig, rest, spec.joints, positions));
        }
        return result;
    }
    RigPose solveRigSplineIK(const RigEvaluator& rig, const RigSplineIK& spec) {
        validateChain(rig, spec.joints);
        if (!spec.path || !std::isfinite(spec.start) || !std::isfinite(spec.end) ||
            spec.start < 0.f || spec.end > 1.f || spec.start >= spec.end ||
            spec.resolutionSteps == 0 || spec.resolutionSteps > 4096)
            throw std::invalid_argument("RigBaking: invalid spline interval or resolution");
        const auto rest = rig.evaluateFK();
        std::vector<float> chord(spec.joints.size() - 1);
        for (std::size_t joint = 0; joint + 1 < spec.joints.size(); ++joint) {
            chord[joint] = rest.localToWorld(spec.joints[joint], {}).distanceTo(rest.localToWorld(spec.joints[joint + 1], {}));
            if (chord[joint] <= 1e-6f || !std::isfinite(chord[joint])) throw std::invalid_argument("RigBaking: invalid rest length");
        }
        // Exact rigid-chord placement: each joint is the FIRST point forward on
        // the path whose straight-line distance to its parent equals the chord.
        // Parameters strictly increase, so joints keep arc-length order; on a
        // curved path the arc consumed between joints exceeds the chord, which
        // is exactly the backward distribution curvature demands. Segments are
        // never stretched. The root slides forward just enough to fit the chain.
        const auto& path = *spec.path;
        const float span = path.length();
        if (!std::isfinite(span) || span <= 0.f) throw std::invalid_argument("RigBaking: degenerate spline length");
        const auto crossing = [&](float from, float length) {
            // Bracket: the arc needed is at least `length`, at most 2x within
            // smooth curvature; the coarse scan brackets the first crossing.
            float low = from;
            float high = std::min(spec.end, from + 2.f * length / span);
            if (path.sampleUniform(high).distanceTo(path.sampleUniform(low)) < length) return -1.f;
            constexpr unsigned scan = 64;
            float previous = low;
            for (unsigned step = 1; step <= scan; ++step) {
                const float parameter = low + (high - low) * static_cast<float>(step) / scan;
                if (path.sampleUniform(parameter).distanceTo(path.sampleUniform(from)) >= length) {
                    high = parameter;
                    low = previous;
                    break;
                }
                previous = parameter;
            }
            for (unsigned iteration = 0; iteration < 64; ++iteration) {
                const float middle = (low + high) * .5f;
                if (path.sampleUniform(middle).distanceTo(path.sampleUniform(from)) >= length) high = middle; else low = middle;
            }
            return high;
        };
        const auto walk = [&](float root) {
            std::vector<float> parameters{root};
            for (const float length : chord) {
                const float next = crossing(parameters.back(), length);
                if (next < 0.f || next > spec.end + 1e-6f) return std::vector<float>{};
                parameters.push_back(next);
            }
            return parameters;
        };
        // fits(root) is monotone decreasing in root: a later root pushes every
        // crossing later, so the tip leaves [start, end] sooner. Bisection finds
        // the largest fitting root; a chain that cannot fit at all fails closed.
        const auto positions = [&](float root) {
            std::vector<Vector3> result;
            for (const float parameter : walk(root)) result.push_back(path.sampleUniform(parameter));
            return result;
        };
        if (walk(spec.start).empty()) throw std::invalid_argument("RigBaking: rigid chain does not fit the spline interval");
        float low = spec.start, high = spec.end;
        for (unsigned iteration = 0; iteration < 64; ++iteration) {
            const float middle = (low + high) * .5f;
            if (!walk(middle).empty()) low = middle; else high = middle;
        }
        auto result = positions(low);
        if (result.size() != spec.joints.size()) throw std::invalid_argument("RigBaking: rigid chain does not fit the spline interval");
        // Fail closed on any residual chord error instead of shipping a stretch.
        for (std::size_t joint = 0; joint + 1 < result.size(); ++joint)
            if (std::abs(result[joint].distanceTo(result[joint + 1]) - chord[joint]) > 1e-4f * std::max(1.f, chord[joint]))
                throw std::invalid_argument("RigBaking: rigid chord placement did not converge");
        return fromPositions(rig, rest, spec.joints, result);
    }
}
