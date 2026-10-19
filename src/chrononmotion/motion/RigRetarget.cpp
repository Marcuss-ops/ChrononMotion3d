// ChrononMotion — FK motion retargeting (see RigRetarget.hpp).

#include "chrononmotion/motion/RigRetarget.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <stdexcept>

namespace chrononmotion::motion {
    namespace {

        void requireFiniteTransform(const Transform& transform, const char* what) {
            const float values[] = {transform.position.x, transform.position.y, transform.position.z,
                                    transform.rotation.x, transform.rotation.y, transform.rotation.z,
                                    transform.scale.x,    transform.scale.y,    transform.scale.z,
                                    transform.orientation.x, transform.orientation.y, transform.orientation.z,
                                    transform.orientation.w, transform.opacity};
            for (float value : values)
                if (!std::isfinite(value)) throw std::invalid_argument(what);
        }

        // Nodes are sorted by id at compile time; the pose span is not.
        const RigPoseValue* findPoseValue(std::span<const RigPoseValue> pose, RigNodeId id) {
            for (const auto& value : pose)
                if (value.id == id) return &value;
            return nullptr;
        }

        const RigNode& findNode(const std::vector<RigNode>& nodes, RigNodeId id, const char* what) {
            const auto it = std::lower_bound(nodes.begin(), nodes.end(), id,
                                             [](const RigNode& node, RigNodeId key) { return node.id < key; });
            if (it == nodes.end() || it->id != id) throw std::invalid_argument(what);
            return *it;
        }

    }// namespace

    RigPose retargetPose(const RigEvaluator& source, std::span<const RigPoseValue> sourcePose,
                         const RigEvaluator& target, std::span<const RetargetPair> map,
                         const RetargetOptions& options) {

        if (map.empty()) throw std::invalid_argument("retarget: joint map is empty");
        if (!std::isfinite(options.rootTranslationScale))
            throw std::invalid_argument("retarget: root translation scale must be finite");

        const std::vector<RigNode>& sourceNodes = source.nodes();
        const std::vector<RigNode>& targetNodes = target.nodes();
        if (sourceNodes.empty()) throw std::invalid_argument("retarget: source graph is empty");
        if (targetNodes.empty()) throw std::invalid_argument("retarget: target graph is empty");

        for (const auto& value : sourcePose) {
            requireFiniteTransform(value.local, "retarget: source pose carries a non-finite transform");
            findNode(sourceNodes, value.id, "retarget: source pose names an unknown joint");
        }
        for (const auto& pair : map) {
            if (pair.source == 0 || pair.target == 0)
                throw std::invalid_argument("retarget: joint ids must be nonzero");
            findNode(sourceNodes, pair.source, "retarget: map names an unknown source joint");
            findNode(targetNodes, pair.target, "retarget: map names an unknown target joint");
        }
        for (std::size_t i = 0; i < map.size(); ++i)
            for (std::size_t j = i + 1; j < map.size(); ++j) {
                if (map[i].source == map[j].source)
                    throw std::invalid_argument("retarget: map names a source joint twice");
                if (map[i].target == map[j].target)
                    throw std::invalid_argument("retarget: map names a target joint twice");
            }

        const RigEvaluation sourceBind = source.bindPose();
        const RigEvaluation targetBind = target.bindPose();

        RigPose result;
        result.reserve(targetNodes.size());
        for (RigNodeId targetId : target.evaluationOrder()) {
            const RigNode& targetNode = findNode(targetNodes, targetId, "retarget: target order is inconsistent");
            const std::size_t targetSlot = targetBind.index(targetId);

            const RetargetPair* pair = nullptr;
            for (const auto& candidate : map)
                if (candidate.target == targetId) {
                    pair = &candidate;
                    break;
                }
            if (!pair) {
                result.push_back({targetId, targetBind.local[targetSlot]});
                continue;
            }

            const RigNode& sourceNode = findNode(sourceNodes, pair->source, "retarget: map is inconsistent");
            Transform local;
            if (const RigPoseValue* posed = findPoseValue(sourcePose, pair->source)) local = posed->local;
            else local = sourceBind.local[sourceBind.index(pair->source)];

            if (sourceNode.parent == 0) {
                local.position.x *= options.rootTranslationScale;
                local.position.y *= options.rootTranslationScale;
                local.position.z *= options.rootTranslationScale;
            } else if (options.scalePositionsToTarget) {
                if (!(sourceNode.length > 0.f) || !(targetNode.length > 0.f))
                    throw std::invalid_argument("retarget: scaling needs positive bind lengths");
                const float ratio = targetNode.length / sourceNode.length;
                local.position.x *= ratio;
                local.position.y *= ratio;
                local.position.z *= ratio;
            }
            result.push_back({targetId, local});
        }
        return result;
    }

}// namespace chrononmotion::motion
