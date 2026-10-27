#include "GltfReaderInternal.hpp"

#include <algorithm>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace chrononmotion::gltf_detail {

void Document::parseMotionAnimations(const std::vector<JsonValue>& nodes,
                                     const std::vector<std::vector<std::size_t>>& nodeGeometry,
                                     GltfMotionAsset& result) {
    using namespace motion;
    const auto nodeIndex = [&](const JsonValue& value) {
        const auto index = indexValue(value, "node index");
        if (index >= nodes.size()) invalid("motion node reference out of range");
        return index;
    };
    const auto floats = [&](const JsonValue& value, std::size_t components) {
        const auto index = indexValue(value, "accessor");
        if (index >= accessors_.size()) invalid("motion accessor out of range");
        return floatAttribute(accessors_[index], components, "motion");
    };

    if (const auto* animations = find(root_, "animations")) {
        if (animations->array("animations").size() > 256) invalid("clip budget exceeded");
        std::size_t keyBudget = 0;
        for (const auto& animation : animations->array("animations")) {
            rejectUnknownKeys(animation, {"name", "samplers", "channels"}, "animation");
            GltfMotionClip clip; if (find(animation, "name")) clip.name = find(animation, "name")->string("name");
            const auto& samplers = required(animation, "samplers", "animation").array("samplers");
            const auto& channels = required(animation, "channels", "animation").array("channels");
            std::set<std::pair<std::size_t, std::string>> unique;
            for (const auto& channel : channels) {
                rejectUnknownKeys(channel, {"sampler", "target"}, "channel");
                const auto& target = required(channel, "target", "channel"); rejectUnknownKeys(target, {"node", "path"}, "target");
                const auto node = nodeIndex(required(target, "node", "target"));
                const auto path = required(target, "path", "target").string("path");
                if (path != "translation" && path != "rotation" && path != "scale" && path != "weights") invalid("unsupported motion animation path");
                if (!unique.emplace(node, path).second) invalid("duplicate animation channel");
                const auto& sampler = at(samplers, indexValue(required(channel, "sampler", "channel"), "sampler"), "sampler");
                rejectUnknownKeys(sampler, {"input", "output", "interpolation"}, "sampler");
                const auto interpolation = find(sampler, "interpolation") ? find(sampler, "interpolation")->string("interpolation") : "LINEAR";
                if (interpolation != "LINEAR" && interpolation != "STEP") invalid("only LINEAR and STEP motion interpolation supported");
                const auto times = floats(required(sampler, "input", "sampler"), 1);
                if (path == "weights") {
                    if (nodeGeometry[node].empty()) invalid("weight channel requires mesh node");
                    const auto morphCount = result.geometry[nodeGeometry[node].front()].morphs.size();
                    if (!morphCount) invalid("weight channel requires morph targets");
                    const auto values = floats(required(sampler, "output", "sampler"), 1);
                    if (values.size() != times.size() * morphCount) invalid("morph animation topology mismatch");
                    keyBudget += values.size(); if (keyBudget > 1000000) invalid("morph animation key budget exceeded");
                    for (const auto geometryIndex : nodeGeometry[node]) {
                        const auto& geometry = result.geometry[geometryIndex];
                        if (geometry.morphs.size() != morphCount) invalid("mesh primitives disagree on morph count");
                        GltfMotionClip::MorphChannel morph; morph.geometryId = geometry.id; morph.weights.resize(morphCount);
                        float previous = -1.f;
                        for (std::size_t key = 0; key < times.size(); ++key) {
                            if (times[key] < 0.f || times[key] <= previous) invalid("morph times must increase");
                            previous = times[key]; clip.duration = std::max(clip.duration, times[key]);
                            for (std::size_t targetIndex = 0; targetIndex < morphCount; ++targetIndex) {
                                const auto value = values[key * morphCount + targetIndex];
                                if (value < 0.f || value > 1.f) invalid("morph animated weight outside 0..1");
                                if (interpolation == "STEP") morph.weights[targetIndex].addDiscrete(times[key], value);
                                else morph.weights[targetIndex].add(times[key], value);
                            }
                        }
                        clip.morphs.push_back(std::move(morph));
                    }
                    continue;
                }
                const auto values = floats(required(sampler, "output", "sampler"), path == "rotation" ? 4 : 3);
                keyBudget += times.size(); if (keyBudget > 1000000 || values.size() != times.size() * (path == "rotation" ? 4 : 3)) invalid("clip key budget/topology invalid");
                auto it = std::find_if(clip.animation.begin(), clip.animation.end(), [node](const RigAnimatedNode& a) { return a.id == node + 1; });
                if (it == clip.animation.end()) { clip.animation.push_back({static_cast<RigNodeId>(node + 1), {}}); it = clip.animation.end() - 1; }
                float previous = -1.f;
                for (std::size_t key = 0; key < times.size(); ++key) {
                    const float time = times[key]; if (time < 0.f || time <= previous) invalid("animation time must be increasing nonnegative"); previous = time;
                    if (path == "rotation") {
                        Quaternion q(values[key * 4], values[key * 4 + 1], values[key * 4 + 2], values[key * 4 + 3]);
                        if (std::abs(q.lengthSq() - 1.f) > 1e-4f) invalid("animation quaternion must be normalized");
                        if (interpolation == "STEP") it->tracks.orientation.addDiscrete(time, q); else it->tracks.orientation.add(time, q);
                    } else {
                        Vector3 value{values[key * 3], values[key * 3 + 1], values[key * 3 + 2]};
                        auto& track = path == "translation" ? it->tracks.position : it->tracks.scale;
                        if (interpolation == "STEP") track.addDiscrete(time, value); else track.add(time, value);
                    }
                    clip.duration = std::max(clip.duration, time);
                }
            }
            std::sort(clip.animation.begin(), clip.animation.end(), [](const RigAnimatedNode& a, const RigAnimatedNode& b) { return a.id < b.id; });
            RigProgram program; program.graph = result.graph; program.animation = clip.animation; program.geometry = result.geometry;
            for (const auto& morph : clip.morphs) program.geometry[morph.geometryId - 1].morphTracks = morph.weights;
            (void) RigProgramEvaluator(program);
            result.clips.push_back(std::move(clip));
        }
    }
}

} // namespace chrononmotion::gltf_detail
