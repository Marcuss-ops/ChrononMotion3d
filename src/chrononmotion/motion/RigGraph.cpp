#include "chrononmotion/motion/RigGraph.hpp"
#include "chrononmotion/motion/MotionScene.hpp"
#include "chrononmotion/motion/OfflineBaking.hpp"
#include "chrononmotion/motion/RigOperators.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>
#include <stdexcept>

namespace chrononmotion::motion {
    namespace {
        constexpr std::size_t noParent = std::numeric_limits<std::size_t>::max();
        constexpr std::size_t maxNodes = 10000;
        constexpr std::size_t maxSamples = 1000000;

        bool finite(const Vector3& v) {
            return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
        }
        void validateTransform(const Transform& t) {
            const auto& q = t.orientation;
            const float norm = q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
            if (!finite(t.position) || !finite(t.anchor) || !finite(t.rotation) || !finite(t.scale) ||
                !std::isfinite(norm) || std::abs(norm - 1.f) > 1e-4f ||
                t.scale.x <= 0.f || t.scale.y <= 0.f || t.scale.z <= 0.f ||
                !std::isfinite(t.opacity) || t.opacity < 0.f || t.opacity > 1.f)
                throw std::invalid_argument("RigGraph: invalid or singular transform");
            for (const float x : t.toMatrix().elements)
                if (!std::isfinite(x)) throw std::invalid_argument("RigGraph: transform overflow");
        }
        std::size_t findIndex(const std::vector<RigNodeId>& ids, RigNodeId id) {
            const auto it = std::lower_bound(ids.begin(), ids.end(), id);
            if (it == ids.end() || *it != id) throw std::invalid_argument("RigGraph: unknown node ID");
            return static_cast<std::size_t>(it - ids.begin());
        }
    }

    std::size_t RigEvaluation::index(RigNodeId id) const { return findIndex(ids, id); }
    Vector3 RigEvaluation::localToWorld(RigNodeId id, const Vector3& point) const {
        Vector3 out = point;
        return out.applyMatrix4(world.at(index(id)));
    }
    Vector3 RigEvaluation::worldToLocal(RigNodeId id, const Vector3& point) const {
        Matrix4 inverse = world.at(index(id));
        const float determinant = inverse.determinant();
        if (!std::isfinite(determinant) || determinant == 0.f)
            throw std::invalid_argument("RigGraph: non-invertible world transform");
        inverse.invert();
        Vector3 out = point;
        return out.applyMatrix4(inverse);
    }

    std::size_t RigEvaluator::index(RigNodeId id) const {
        const auto it = std::lower_bound(nodes_.begin(), nodes_.end(), id,
                                        [](const RigNode& node, RigNodeId key) { return node.id < key; });
        if (it == nodes_.end() || it->id != id) throw std::invalid_argument("RigGraph: unknown node ID");
        return static_cast<std::size_t>(it - nodes_.begin());
    }

    RigEvaluator RigGraph::compile() const {
        if (nodes.size() > maxNodes) throw std::invalid_argument("RigGraph: node budget exceeded");
        RigEvaluator result;
        result.nodes_ = nodes;
        std::sort(result.nodes_.begin(), result.nodes_.end(),
                  [](const RigNode& a, const RigNode& b) { return a.id < b.id; });
        const auto count = nodes.size();
        result.parents_.resize(count, noParent);
        result.constraintTargets_.resize(count);
        std::vector<std::vector<std::size_t>> outgoing(count);
        std::vector<std::size_t> indegree(count, 0);
        std::vector<Layer::Id> bindings;
        std::size_t edgeCount = 0;
        for (std::size_t i = 0; i < count; ++i) {
            const auto& node = result.nodes_[i];
            if (node.id == 0 || (i && node.id == result.nodes_[i - 1].id))
                throw std::invalid_argument("RigGraph: zero or duplicate ID");
            if (node.kind < RigNodeKind::Bone || node.kind > RigNodeKind::Pin)
                throw std::invalid_argument("RigGraph: unknown node kind");
            validateTransform(node.bind);
            validateTransform(node.rest);
            if (!std::isfinite(node.length) || node.length <= 0.f || !finite(node.primaryAxis) ||
                std::abs(node.primaryAxis.lengthSq() - 1.f) > 1e-4f)
                throw std::invalid_argument("RigGraph: invalid bone length or axis");
            if (node.layerId) bindings.push_back(node.layerId);
            edgeCount += node.dependencies.size() + (node.parent ? 1 : 0);
            if (edgeCount > maxNodes * 16) throw std::invalid_argument("RigGraph: edge budget exceeded");
            std::vector<std::size_t> deps;
            if (node.parent) {
                result.parents_[i] = result.index(node.parent);
                deps.push_back(result.parents_[i]);
            }
            for (const auto dependency : node.dependencies) deps.push_back(result.index(dependency));
            if (node.constraints.size() > 256) throw std::invalid_argument("RigGraph: constraint budget exceeded");
            for (const auto& constraint : node.constraints) {
                validateRigOperator(constraint);
                if (constraint.kind < ConstraintKind::Billboard || constraint.kind > ConstraintKind::LookAtWithRoll ||
                    !std::isfinite(constraint.amount) || constraint.amount < 0.f || constraint.amount > 1.f ||
                    !std::isfinite(constraint.distance) || constraint.distance < 0.f ||
                    !std::isfinite(constraint.progress) || !finite(constraint.minimum) || !finite(constraint.maximum) ||
                    !std::isfinite(constraint.restLength) || constraint.restLength <= 0.f ||
                    constraint.amountTrack || (constraint.kind == ConstraintKind::FollowPath && !constraint.path))
                    throw std::invalid_argument("RigGraph: invalid or unsupported constraint spec");
                for (unsigned axis = 0; axis < 3; ++axis)
                    if (constraint.minimum[axis] > constraint.maximum[axis] ||
                        (constraint.kind == ConstraintKind::LimitScale && constraint.minimum[axis] <= 0.f))
                        throw std::invalid_argument("RigGraph: invalid limit interval");
                const bool limit = constraint.kind == ConstraintKind::LimitPosition ||
                                   constraint.kind == ConstraintKind::LimitRotation || constraint.kind == ConstraintKind::LimitScale;
                if (!constraint.targetId && !limit && constraint.kind != ConstraintKind::FollowPath)
                    throw std::invalid_argument("RigGraph: constraint requires target");
                const auto target = constraint.targetId ? result.index(constraint.targetId) : noParent;
                result.constraintTargets_[i].push_back(target);
                if (target != noParent) deps.push_back(target);
                if (++edgeCount > maxNodes * 16) throw std::invalid_argument("RigGraph: edge budget exceeded");
            }
            std::sort(deps.begin(), deps.end());
            deps.erase(std::unique(deps.begin(), deps.end()), deps.end());
            for (const auto dependency : deps) {
                outgoing[dependency].push_back(i);
                ++indegree[i];
            }
        }
        std::sort(bindings.begin(), bindings.end());
        if (std::adjacent_find(bindings.begin(), bindings.end()) != bindings.end())
            throw std::invalid_argument("RigGraph: duplicate layer binding");
        std::priority_queue<std::size_t, std::vector<std::size_t>, std::greater<>> ready;
        for (std::size_t i = 0; i < count; ++i) if (!indegree[i]) ready.push(i);
        while (!ready.empty()) {
            const auto i = ready.top();
            ready.pop();
            result.order_.push_back(i);
            result.orderIds_.push_back(result.nodes_[i].id);
            for (const auto child : outgoing[i]) if (--indegree[child] == 0) ready.push(child);
        }
        if (result.order_.size() != count) throw std::invalid_argument("RigGraph: dependency cycle");
        // Evaluate once to reject finite locals whose hierarchy overflows.
        (void) result.evaluate();
        (void) result.bindPose();
        return result;
    }

    void RigEvaluator::evaluate(std::span<const RigPoseValue> pose, RigEvaluation& output) const {
        evaluateImpl(pose, output, true);
    }
    RigEvaluation RigEvaluator::evaluateFK(std::span<const RigPoseValue> pose) const {
        RigEvaluation output;
        evaluateImpl(pose, output, false);
        return output;
    }
    RigEvaluation RigEvaluator::evaluateAt(float seconds, std::span<const RigPoseValue> pose,
                                           std::span<const RigConstraintWeight> weights) const {
        if (!std::isfinite(seconds) || seconds < 0.f) throw std::invalid_argument("RigGraph: invalid evaluation time");
        RigEvaluation output;
        if (weights.size() > maxNodes * 16) throw std::invalid_argument("RigGraph: weight budget exceeded");
        for (std::size_t i = 0; i < weights.size(); ++i) {
            const auto& weight = weights[i];
            if (weight.constraint >= nodes_[index(weight.node)].constraints.size() || !std::isfinite(weight.amount) ||
                weight.amount < 0.f || weight.amount > 1.f)
                throw std::invalid_argument("RigGraph: invalid constraint weight");
            for (std::size_t j = 0; j < i; ++j)
                if (weights[j].node == weight.node && weights[j].constraint == weight.constraint)
                    throw std::invalid_argument("RigGraph: duplicate constraint weight");
        }
        evaluateImpl(pose, output, true, seconds, weights);
        return output;
    }
    RigEvaluator RigEvaluator::withoutConstraints() const {
        RigEvaluator result = *this;
        for (auto& node : result.nodes_) node.constraints.clear();
        for (auto& targets : result.constraintTargets_) targets.clear();
        return result;
    }
    void RigEvaluator::evaluateImpl(std::span<const RigPoseValue> pose, RigEvaluation& output, bool constraints, float seconds,
                                    std::span<const RigConstraintWeight> weights) const {
        if (pose.size() > nodes_.size()) throw std::invalid_argument("RigGraph: pose budget exceeded");
        // Validate before overwriting output; sorted pose IDs avoid quadratic work
        // and any transient allocation in the steady-state evaluation path.
        RigNodeId previous = 0;
        for (const auto& value : pose) {
            if (value.id <= previous) throw std::invalid_argument("RigGraph: pose IDs must be unique and sorted");
            previous = value.id;
            (void) index(value.id);
            validateTransform(value.local);
        }
        output.ids.resize(nodes_.size());
        output.local.resize(nodes_.size());
        output.world.resize(nodes_.size());
        std::size_t poseIndex = 0;
        for (std::size_t i = 0; i < nodes_.size(); ++i) {
            output.ids[i] = nodes_[i].id;
            output.local[i] = nodes_[i].rest;
            if (poseIndex < pose.size() && pose[poseIndex].id == nodes_[i].id)
                output.local[i] = pose[poseIndex++].local;
        }
        for (const auto i : order_) {
            // Canonical stages: authored local → ordered constraints → limits → FK world.
            // Constraint insertion order is explicit within a stage; limits always last.
            for (unsigned stage = 0; constraints && stage < 2; ++stage) {
                for (std::size_t c = 0; c < nodes_[i].constraints.size(); ++c) {
                    const auto& constraint = nodes_[i].constraints[c];
                    const bool limit = constraint.kind == ConstraintKind::LimitPosition ||
                                       constraint.kind == ConstraintKind::LimitRotation || constraint.kind == ConstraintKind::LimitScale;
                    if (limit != (stage == 1)) continue;
                    Vector3 position{}, scale{1.f, 1.f, 1.f};
                    Quaternion rotation;
                    const auto target = constraintTargets_[i][c];
                    if (target != noParent) {
                        Matrix4 relative = output.world[target];
                        if (parents_[i] != noParent) {
                            Matrix4 inverse = output.world[parents_[i]];
                            if (inverse.determinant() == 0.f) throw std::invalid_argument("RigGraph: singular constraint parent");
                            relative.premultiply(inverse.invert());
                        }
                        relative.decompose(position, rotation, scale);
                    }
                    ConstraintSpec effective = constraint;
                    for (const auto& weight : weights)
                        if (weight.node == nodes_[i].id && weight.constraint == c) effective.amount = weight.amount;
                    if (!applyConstraint(effective, output.local[i], position, rotation, scale, seconds))
                        throw std::invalid_argument("RigGraph: constraint evaluation failed");
                }
            }
            validateTransform(output.local[i]);
            output.world[i] = output.local[i].toMatrix();
            if (parents_[i] != noParent) output.world[i].premultiply(output.world[parents_[i]]);
            for (const float x : output.world[i].elements)
                if (!std::isfinite(x)) throw std::invalid_argument("RigGraph: hierarchy overflow");
        }
    }
    RigEvaluation RigEvaluator::evaluate(std::span<const RigPoseValue> pose) const {
        RigEvaluation result;
        evaluate(pose, result);
        return result;
    }
    RigEvaluation RigEvaluator::bindPose() const {
        RigPose pose;
        pose.reserve(nodes_.size());
        for (const auto& node : nodes_) pose.push_back({node.id, node.bind});
        return evaluateFK(pose);
    }

    RigPose RigEvaluator::matchWorldPose(const RigEvaluation& source) const {
        for (const auto& node : nodes_)
            if (!node.constraints.empty())
                throw std::invalid_argument("RigGraph: world matching requires raw FK graph");
        if (source.ids.size() != nodes_.size() || source.world.size() != nodes_.size() ||
            source.local.size() != nodes_.size())
            throw std::invalid_argument("RigGraph: incompatible world pose");
        RigPose result;
        result.reserve(nodes_.size());
        for (std::size_t i = 0; i < nodes_.size(); ++i) {
            if (source.ids[i] != nodes_[i].id)
                throw std::invalid_argument("RigGraph: world pose ID mismatch");
            Matrix4 local = source.world[i];
            if (parents_[i] != noParent) {
                Matrix4 inverse = source.world[parents_[i]];
                const float determinant = inverse.determinant();
                if (!std::isfinite(determinant) || determinant == 0.f)
                    throw std::invalid_argument("RigGraph: singular matching parent");
                local.premultiply(inverse.invert());
            }
            Transform t;
            local.decompose(t.position, t.orientation, t.scale);
            t.anchor = source.local[i].anchor;
            t.opacity = source.local[i].opacity;
            Vector3 offset = t.anchor;
            offset.multiply(t.scale).applyQuaternion(t.orientation);
            t.position.add(offset);
            validateTransform(t);
            const auto recomposed = t.toMatrix();
            for (std::size_t j = 0; j < 16; ++j)
                if (!std::isfinite(local.elements[j]) ||
                    std::abs(recomposed.elements[j] - local.elements[j]) >
                        1e-4f * std::max(1.f, std::abs(local.elements[j])))
                    throw std::invalid_argument("RigGraph: world pose contains unrepresentable shear");
            result.push_back({nodes_[i].id, t});
        }
        return result;
    }

    RigPose RigEvaluator::solveIK(const RigIKChain& chain, std::span<const RigPoseValue> pose) const {
        if (chain.joints.size() < 2 || chain.joints.size() > 257 || !finite(chain.target) ||
            !finite(chain.pole) || !std::isfinite(chain.amount) || chain.amount < 0.f || chain.amount > 1.f ||
            chain.iterations == 0 || chain.iterations > 4096 || !std::isfinite(chain.tolerance) || chain.tolerance <= 0.f)
            throw std::invalid_argument("RigGraph: invalid IK chain");
        for (const auto& node : nodes_)
            if (!node.constraints.empty())
                throw std::invalid_argument("RigGraph: IK matching requires unconstrained pose graph");
        auto evaluated = evaluate(pose);
        std::vector<std::size_t> joints;
        std::vector<Vector3> positions;
        for (const auto id : chain.joints) {
            const auto i = index(id);
            if (std::find(joints.begin(), joints.end(), i) != joints.end() ||
                (!joints.empty() && parents_[i] != joints.back()))
                throw std::invalid_argument("RigGraph: IK joints must form a unique parent chain");
            joints.push_back(i);
            positions.push_back(evaluated.localToWorld(id, {}));
        }
        const auto solved = solveIKChain3D(positions, chain.target, chain.pole,
                                           chain.iterations, chain.tolerance);
        // Align each bone's world orientation with its solved segment. This
        // rotates child offsets rather than translating them, retaining FK lengths.
        for (std::size_t joint = 0; joint + 1 < joints.size(); ++joint) {
            const auto i = joints[joint];
            Vector3 from = positions[joint + 1] - positions[joint];
            Vector3 to = solved.joints[joint + 1] - solved.joints[joint];
            if (from.lengthSq() <= 1e-12f || to.lengthSq() <= 1e-12f)
                throw std::invalid_argument("RigGraph: degenerate IK segment");
            Quaternion rotation;
            rotation.setFromUnitVectors(from.normalize(), to.normalize());
            Vector3 translation, scale;
            Quaternion orientation;
            evaluated.world[i].decompose(translation, orientation, scale);
            // Nonuniform world scale has no rigid-chain interpretation.
            if (std::abs(scale.x - scale.y) > 1e-5f || std::abs(scale.x - scale.z) > 1e-5f)
                throw std::invalid_argument("RigGraph: IK requires uniform world scale");
            orientation.premultiply(rotation).normalize();
            evaluated.world[i].compose(solved.joints[joint], orientation, scale);
        }
        {
            const auto i = joints.back();
            evaluated.world[i].setPosition(solved.joints.back());
        }
        // Move descendants outside the solved chain with their solved parents.
        for (const auto i : order_) {
            if (std::find(joints.begin(), joints.end(), i) != joints.end()) continue;
            evaluated.world[i] = evaluated.local[i].toMatrix();
            if (parents_[i] != noParent) evaluated.world[i].premultiply(evaluated.world[parents_[i]]);
        }
        const auto matched = matchWorldPose(evaluated);
        const auto fk = evaluate(pose);
        RigPose result;
        result.reserve(nodes_.size());
        for (std::size_t i = 0; i < nodes_.size(); ++i) {
            if (chain.amount == 0.f) result.push_back({nodes_[i].id, fk.local[i]});
            else if (chain.amount == 1.f) result.push_back(matched[i]);
            else result.push_back({nodes_[i].id, Transform::blend(fk.local[i], matched[i].local, chain.amount)});
        }
        return result;
    }

    RigIKChain RigEvaluator::matchIK(const RigIKChain& chain, std::span<const RigPoseValue> pose) const {
        if (chain.joints.size() < 2 || chain.joints.size() > 257)
            throw std::invalid_argument("RigGraph: invalid matching chain");
        const auto fk = evaluateFK(pose);
        for (std::size_t joint = 0; joint < chain.joints.size(); ++joint) {
            const auto i = index(chain.joints[joint]);
            if (joint && parents_[i] != index(chain.joints[joint - 1]))
                throw std::invalid_argument("RigGraph: matching requires parent chain");
        }
        RigIKChain result = chain;
        result.target = fk.localToWorld(chain.joints.back(), {});
        const Vector3 root = fk.localToWorld(chain.joints.front(), {});
        const Vector3 axis = result.target - root;
        Vector3 bend{};
        for (std::size_t joint = 1; joint + 1 < chain.joints.size(); ++joint) {
            Vector3 offset = fk.localToWorld(chain.joints[joint], {}) - root;
            if (axis.lengthSq() > 1e-12f) offset.addScaledVector(axis, -offset.dot(axis) / axis.lengthSq());
            if (offset.lengthSq() > bend.lengthSq()) bend = offset;
        }
        if (bend.lengthSq() > 1e-12f) result.pole = root + bend;
        result.amount = 1.f;
        // Canonical solver validates segment lengths, work bounds and inputs.
        (void) solveIK(result, pose);
        return result;
    }

    BakedRig RigEvaluator::bake(std::span<const RigPose> frames, int firstFrame, float fps) const {
        if (firstFrame < 0 || !std::isfinite(fps) || fps <= 0.f || frames.empty() ||
            frames.size() > maxSamples || (nodes_.size() && frames.size() > maxSamples / nodes_.size()) ||
            frames.size() > static_cast<std::size_t>(std::numeric_limits<int>::max() - firstFrame))
            throw std::invalid_argument("RigGraph: invalid bake range or sample budget");
        BakedRig result;
        result.firstFrame = firstFrame;
        result.lastFrame = firstFrame + static_cast<int>(frames.size()) - 1;
        result.fps = fps;
        for (const auto& node : nodes_) result.nodes.push_back({node.id, node.layerId, {}});
        RigEvaluation evaluated;
        float previousTime = -1.f;
        for (std::size_t frame = 0; frame < frames.size(); ++frame) {
            const float seconds = static_cast<float>(firstFrame + static_cast<int>(frame)) / fps;
            if (!std::isfinite(seconds) || seconds <= previousTime)
                throw std::invalid_argument("RigGraph: unrepresentable sample time");
            previousTime = seconds;
            evaluate(frames[frame], evaluated);
            for (std::size_t i = 0; i < nodes_.size(); ++i) {
                auto& tracks = result.nodes[i].tracks;
                const auto& t = evaluated.local[i];
                tracks.position.add(seconds, t.position);
                tracks.anchor.add(seconds, t.anchor);
                tracks.rotation.add(seconds, t.rotation);
                tracks.orientation.add(seconds, t.orientation);
                tracks.scale.add(seconds, t.scale);
                tracks.opacity.add(seconds, t.opacity);
            }
        }
        return result;
    }

    void RigEvaluator::applyBake(const BakedRig& bake, MotionScene& scene) const {
        if (bake.nodes.size() != nodes_.size() || !std::isfinite(bake.fps) || bake.fps != scene.fps() ||
            bake.firstFrame < 0 || bake.lastFrame < bake.firstFrame ||
            static_cast<std::uint64_t>(bake.lastFrame) - static_cast<std::uint64_t>(bake.firstFrame) + 1 > maxSamples ||
            (!nodes_.empty() && static_cast<std::uint64_t>(bake.lastFrame) - static_cast<std::uint64_t>(bake.firstFrame) + 1 >
                maxSamples / nodes_.size()))
            throw std::invalid_argument("RigGraph: incompatible bake");
        for (std::size_t i = 0; i < nodes_.size(); ++i) {
            const auto& node = nodes_[i];
            if (bake.nodes[i].id != node.id || bake.nodes[i].layerId != node.layerId)
                throw std::invalid_argument("RigGraph: bake binding mismatch");
            const auto& tracks = bake.nodes[i].tracks;
            const auto expected = static_cast<std::size_t>(bake.lastFrame - bake.firstFrame) + 1;
            const auto validateTrack = [&](const auto& track) {
                if (track.keys().size() != expected || !track.modifiers().empty())
                    throw std::invalid_argument("RigGraph: invalid baked track topology");
                for (std::size_t key = 0; key < expected; ++key) {
                    const float time = static_cast<float>(bake.firstFrame + static_cast<int>(key)) / bake.fps;
                    if (!std::isfinite(time) || track.keys()[key].time != time)
                        throw std::invalid_argument("RigGraph: invalid baked key time");
                }
            };
            validateTrack(tracks.position); validateTrack(tracks.anchor); validateTrack(tracks.scale);
            validateTrack(tracks.rotation); validateTrack(tracks.orientation); validateTrack(tracks.opacity);
            for (std::size_t key = 0; key < expected; ++key) {
                Transform t;
                t.position = tracks.position.keys()[key].value; t.anchor = tracks.anchor.keys()[key].value;
                t.scale = tracks.scale.keys()[key].value; t.rotation = tracks.rotation.keys()[key].value;
                t.orientation = tracks.orientation.keys()[key].value; t.opacity = tracks.opacity.keys()[key].value;
                validateTransform(t);
            }
            if (!node.layerId) continue;
            const auto* layer = scene.findLayer(node.layerId);
            const auto parent = parents_[i];
            if (!layer || (parent != noParent && !nodes_[parent].layerId) ||
                layer->parentId != (parent == noParent ? 0 : nodes_[parent].layerId))
                throw std::invalid_argument("RigGraph: incompatible layer hierarchy");
            if (!layer->constraints.empty() || !layer->drivers.empty() || !layer->spaceSwitches.empty() ||
                !layer->timeRemapTrack.empty() || layer->timeRemap.offsetFrames != 0.f ||
                layer->timeRemap.stretch != 1.f || layer->timeRemap.reverse || layer->timeRemap.freeze ||
                layer->timeRemap.loopEnd >= 0)
                throw std::invalid_argument("RigGraph: layer has conflicting evaluation operators");
        }
        // Copy everything before commit, including allocation failure.
        std::vector<LayerTracks> staged;
        staged.reserve(nodes_.size());
        for (const auto& node : bake.nodes) staged.push_back(node.tracks);
        for (std::size_t i = 0; i < nodes_.size(); ++i)
            if (nodes_[i].layerId) scene.findLayer(nodes_[i].layerId)->tracks = std::move(staged[i]);
    }
} // namespace chrononmotion::motion
