#include "chrononmotion/motion/MotionScene.hpp"
#include "chrononmotion/motion/CompiledScene.hpp"
#include "MotionScene_internal.hpp"
#include "chrononmotion/geometries/BoxGeometry.hpp"
#include "chrononmotion/geometries/PlaneGeometry.hpp"
#include "chrononmotion/geometries/SphereGeometry.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace chrononmotion::motion {
using scene_detail::geometryVertexCount;
using scene_detail::keyframeCount;
using scene_detail::linkTypeOf;
using scene_detail::primitiveSegmentCount;
using scene_detail::saturatedAdd;

    std::vector<std::string> MotionScene::validate() const {

        std::vector<std::string> problems;
        if (layers_.size() > policy_.maxLayers) problems.emplace_back("scene exceeds authoring layer budget");
        if (lights_.size() > policy_.maxLights) problems.emplace_back("scene exceeds authoring light budget");
        std::size_t totalKeyframes = 0;
        std::size_t totalGeometryVertices = 0;
        std::size_t totalPrimitiveSegments = 0;
        if (!policy_.allowAnimations) {
            bool hasAnimations = false;
            for (const auto& layer : layers_) {
                hasAnimations = hasAnimations || !layer.tracks.empty() || !layer.timeRemapTrack.empty();
                for (const auto& constraint : layer.constraints)
                    hasAnimations = hasAnimations || (constraint.amountTrack && !constraint.amountTrack->empty());
            }
            for (const auto& light : lights_) hasAnimations = hasAnimations || keyframeCount(*light) != 0;
            if (hasAnimations) problems.emplace_back("scene contains animations but authoring policy disables animations");
        }
        for (const auto& layer : layers_) {
            totalKeyframes = saturatedAdd(totalKeyframes, keyframeCount(layer));
            if (layer.mesh.geometry) {
                const std::string geometryType = layer.mesh.geometry->type();
                totalPrimitiveSegments = saturatedAdd(totalPrimitiveSegments,
                    primitiveSegmentCount(*layer.mesh.geometry));
                if (!policy_.allowedGeometryTypes.empty() &&
                    std::find(policy_.allowedGeometryTypes.begin(), policy_.allowedGeometryTypes.end(), geometryType) ==
                        policy_.allowedGeometryTypes.end())
                    problems.push_back("layer " + std::to_string(layer.id) + ": geometry type is not allowlisted: " + geometryType);
                if (const auto* positions = layer.mesh.geometry->getAttribute<FloatBufferAttribute>("position"))
                    totalGeometryVertices = saturatedAdd(totalGeometryVertices,
                        static_cast<std::size_t>(positions->count()));
            }
        }
        for (const auto& light : lights_) totalKeyframes = saturatedAdd(totalKeyframes, keyframeCount(*light));
        if (totalKeyframes > policy_.maxKeyframes) problems.emplace_back("scene exceeds authoring keyframe budget");
        if (totalGeometryVertices > policy_.maxGeometryVertices) problems.emplace_back("scene exceeds geometry vertex budget");
        if (totalPrimitiveSegments > policy_.maxPrimitiveSegments)
            problems.emplace_back("scene exceeds primitive segment budget");

        // The link graph over (layer, channel) nodes. A link is an edge; an edge
        // that leads back into one already on the resolution path is a cycle and
        // cannot be sampled, so it is an authoring problem here rather than a
        // stack overflow at frame time.
        struct LinkNode {
            Layer::Id layer;
            TrackChannel channel;
        };
        std::vector<LinkNode> linkNodes;
        std::vector<std::pair<std::size_t, std::size_t>> linkEdges;

        for (const auto& layer : layers_) {
            if (!layer.precomp.empty() && !layer.precomp.valid()) {
                problems.push_back("layer " + std::to_string(layer.id) + " ('" + layer.name +
                                   "'): invalid nested composition");
            }
            if (!layer.timeRemap.valid()) {
                problems.push_back("layer " + std::to_string(layer.id) + " ('" + layer.name +
                                   "'): invalid time remap");
            }
            const auto reportTrack = [&](const char* trackName, const std::string& problem) {
                if (!problem.empty()) {
                    problems.push_back("layer " + std::to_string(layer.id) + " ('" + layer.name +
                                       "'): " + trackName + ": " + problem);
                }
            };
            reportTrack("position track", layer.tracks.position.modifierProblem());
            reportTrack("anchor track", layer.tracks.anchor.modifierProblem());
            reportTrack("scale track", layer.tracks.scale.modifierProblem());
            reportTrack("rotation track", layer.tracks.rotation.modifierProblem());
            reportTrack("orientation track", layer.tracks.orientation.modifierProblem());
            reportTrack("opacity track", layer.tracks.opacity.modifierProblem());
            reportTrack("time remap track", layer.timeRemapTrack.modifierProblem());

            for (const auto& constraint : layer.constraints) {
                if (constraint.kind < ConstraintKind::Billboard || constraint.kind > ConstraintKind::LookAtWithRoll ||
                    !std::isfinite(constraint.amount))
                    problems.push_back("layer " + std::to_string(layer.id) + ": invalid constraint kind/amount");
                if (constraint.kind == ConstraintKind::LimitPosition || constraint.kind == ConstraintKind::LimitRotation ||
                    constraint.kind == ConstraintKind::LimitScale) {
                    for (unsigned axis = 0; axis < 3; ++axis)
                        if (!std::isfinite(constraint.minimum[axis]) || !std::isfinite(constraint.maximum[axis]) ||
                            constraint.minimum[axis] > constraint.maximum[axis] ||
                            (constraint.kind == ConstraintKind::LimitScale && constraint.minimum[axis] <= 0.f))
                            problems.push_back("layer " + std::to_string(layer.id) + ": invalid constraint limit interval");
                }
                if (constraint.kind == ConstraintKind::StretchTo &&
                    (!std::isfinite(constraint.restLength) || constraint.restLength <= 0.f))
                    problems.push_back("layer " + std::to_string(layer.id) + ": invalid StretchTo restLength");
                if (constraint.amountTrack) {
                    for (const auto& key : constraint.amountTrack->keys()) {
                        if (!std::isfinite(key.time) || !std::isfinite(key.value)) {
                            problems.push_back("layer " + std::to_string(layer.id) + " ('" + layer.name +
                                               "'): constraint amount track requires finite key times and values");
                            break;
                        }
                    }
                    const std::string amountTrackProblem = constraint.amountTrack->modifierProblem();
                    if (!amountTrackProblem.empty()) {
                        problems.push_back("layer " + std::to_string(layer.id) + " ('" + layer.name +
                                           "'): constraint amount track: " + amountTrackProblem);
                    }
                }
                if (constraint.kind == ConstraintKind::FollowPath && !constraint.path) {
                    problems.push_back("layer " + std::to_string(layer.id) + " ('" + layer.name +
                                       "'): FollowPath constraint requires a path");
                }
                if (constraint.targetId != Layer::kRoot && !hasLayer(constraint.targetId)) {
                    problems.push_back("layer " + std::to_string(layer.id) + " ('" + layer.name +
                                       "'): constraint references unknown target " + std::to_string(constraint.targetId));
                }
            }

            const auto checkLinks = [&](const char* trackName, TrackChannel requesting, const auto& track) {
                for (const auto& modifier : track.modifiers()) {
                    const auto* link = std::get_if<LinkModifier>(&modifier);
                    if (!link) continue;

                    const std::string where = "layer " + std::to_string(layer.id) + " ('" + layer.name +
                                              "'): " + trackName;
                    if (!hasLayer(link->layerId)) {
                        problems.push_back(where + " links to unknown layer " +
                                           std::to_string(link->layerId));
                        continue;
                    }
                    if (linkTypeOf(requesting) != linkTypeOf(link->channel)) {
                        problems.push_back(where + " links to a channel of a different type");
                        continue;
                    }

                    const auto indexOfNode = [&](Layer::Id id, TrackChannel channel) {
                        for (std::size_t i = 0; i < linkNodes.size(); ++i) {
                            if (linkNodes[i].layer == id && linkNodes[i].channel == channel) return i;
                        }
                        linkNodes.push_back(LinkNode{id, channel});
                        return linkNodes.size() - 1;
                    };
                    linkEdges.push_back({indexOfNode(layer.id, requesting),
                                         indexOfNode(link->layerId, link->channel)});
                }
            };

            checkLinks("position track", TrackChannel::Position, layer.tracks.position);
            checkLinks("anchor track", TrackChannel::Anchor, layer.tracks.anchor);
            checkLinks("scale track", TrackChannel::Scale, layer.tracks.scale);
            checkLinks("rotation track", TrackChannel::Rotation, layer.tracks.rotation);
            checkLinks("orientation track", TrackChannel::Orientation, layer.tracks.orientation);
            checkLinks("opacity track", TrackChannel::Opacity, layer.tracks.opacity);
            checkLinks("time remap track", TrackChannel::TimeRemap, layer.timeRemapTrack);
            for (const auto& constraint : layer.constraints) {
                if (constraint.amountTrack)
                    checkLinks("constraint amount track", TrackChannel::Opacity, *constraint.amountTrack);
            }

            float previousSwitchTime = -1.f;
            Layer::Id previousSpace = layer.parentId;
            for (const auto& point : layer.spaceSwitches) {
                if (!std::isfinite(point.seconds) || point.seconds < 0.f || point.seconds <= previousSwitchTime) {
                    problems.push_back("layer " + std::to_string(layer.id) + " ('" + layer.name +
                                       "'): space switches require strictly increasing finite non-negative times");
                    break;
                }
                previousSwitchTime = point.seconds;
                if (point.parentId != Layer::kRoot && !hasLayer(point.parentId)) {
                    problems.push_back("layer " + std::to_string(layer.id) + " ('" + layer.name +
                                       "'): space switch references unknown parent " + std::to_string(point.parentId));
                } else if (point.parentId == layer.id) {
                    problems.push_back("layer " + std::to_string(layer.id) + " ('" + layer.name +
                                       "'): space switch cannot parent a layer to itself");
                }
                if (point.parentId == previousSpace) {
                    problems.push_back("layer " + std::to_string(layer.id) + " ('" + layer.name +
                                       "'): space switch does not change the active parent");
                }
                previousSpace = point.parentId;
            }

            if (layer.parentId == Layer::kRoot) continue;

            if (!hasLayer(layer.parentId)) {
                problems.push_back("layer " + std::to_string(layer.id) + " ('" + layer.name +
                                   "'): unknown parent " + std::to_string(layer.parentId));
            } else if (layer.parentId == layer.id) {
                problems.push_back("layer " + std::to_string(layer.id) + " ('" + layer.name +
                                   "'): parented to itself");
            }
        }

        for (const auto& light : lights_) {
            if (light->parentId != Layer::kRoot && !hasLayer(light->parentId)) {
                problems.push_back("light " + std::to_string(light->id) + " ('" + light->name +
                                   "'): unknown parent " + std::to_string(light->parentId));
            }
            if ((light->kind() == LightKind::Directional || light->kind() == LightKind::Spot) &&
                light->sample(0.f).targetId != Layer::kRoot &&
                !hasLayer(light->sample(0.f).targetId)) {
                problems.push_back("light " + std::to_string(light->id) + " ('" + light->name +
                                   "'): unknown target");
            }
        }

        if (!linkEdges.empty()) {
            std::vector<char> linkColour(linkNodes.size(), 0);
            std::function<bool(std::size_t)> visitLink = [&](std::size_t index) -> bool {
                linkColour[index] = 1;
                for (const auto& edge : linkEdges) {
                    if (edge.first != index) continue;
                    if (linkColour[edge.second] == 1) return true;
                    if (linkColour[edge.second] == 0 && visitLink(edge.second)) return true;
                }
                linkColour[index] = 2;
                return false;
            };

            bool linkCycle = false;
            for (std::size_t i = 0; i < linkNodes.size() && !linkCycle; ++i) {
                if (linkColour[i] == 0) linkCycle = visitLink(i);
            }
            if (linkCycle) {
                problems.emplace_back("hierarchy contains a link cycle");
            }
        }

        const std::size_t count = layers_.size();
        std::vector<char> color(count, 0);
        bool cyclic = false;

        std::function<void(std::size_t)> visit = [&](std::size_t index) {
            color[index] = 1;

            const auto dependency = [&](Layer::Id id) {
                if (id == Layer::kRoot) return;
                const std::size_t target = indexOf(id);
                if (target == npos) return; // reported at its authoring boundary
                if (color[target] == 1) cyclic = true;
                else if (color[target] == 0) visit(target);
            };
            dependency(layers_[index].parentId);
            for (const auto& point : layers_[index].spaceSwitches) dependency(point.parentId);
            for (const auto& constraint : layers_[index].constraints) dependency(constraint.targetId);
            for (const auto& driver : layers_[index].drivers) {
                if (driver.targetId != Layer::kRoot && !hasLayer(driver.targetId))
                    problems.emplace_back("driver references unknown layer " + std::to_string(driver.targetId));
                dependency(driver.targetId);
            }

            color[index] = 2;
        };

        for (std::size_t index = 0; index < count; ++index) {
            if (color[index] == 0) visit(index);
        }

        if (cyclic) {
            problems.emplace_back("hierarchy contains a parent or evaluation dependency cycle");
        }

        return problems;
    }

    CompiledScene MotionScene::compile() const {
        CompiledScene result;
        result.name = name_;
        result.frame = lastFrame_;
        result.fps = fps_;
        result.transforms.reserve(layers_.size());
        result.precomps.reserve(layers_.size());
        for (std::size_t index = 0; index < layers_.size(); ++index) {
            if (!states_[index].evaluated) {
                throw std::logic_error("MotionScene::compile: evaluate the scene before compiling");
            }
            if (layers_[index].precomp.valid()) {
                const int nestedFrame = static_cast<int>(std::lround(
                        layers_[index].mapFrame(static_cast<float>(lastFrame_), lastPts_)));
                layers_[index].precomp.scene->evaluate(nestedFrame);
                auto nested = std::make_shared<CompiledScene>(layers_[index].precomp.scene->compile());
                result.precomps.push_back(CompiledPrecomp{
                    layers_[index].id, states_[index].world, states_[index].worldOpacity,
                    states_[index].visible, std::move(nested)});
            }
            result.transforms.push_back(CompiledTransform{
                    layers_[index].id,
                    states_[index].world,
                    states_[index].previousWorld,
                    states_[index].worldOpacity,
                    states_[index].previousWorldOpacity,
                    states_[index].visible,
                    states_[index].previousVisible,
                    layers_[index].castsShadow,
                    layers_[index].acceptsShadow,
                    layers_[index].shadowOpacity,
                    layers_[index].shadowBlur});
        }        for (std::size_t index = 0; index < layers_.size(); ++index) {
            const MeshAttachment& mesh = layers_[index].mesh;
            if (mesh.empty()) continue;

            MeshDescriptor descriptor;
            descriptor.layerId = layers_[index].id;
            descriptor.name = layers_[index].name;
            // The asset identity is the opaque one from the mesh attachment, or
            // the layer's content id when the attachment only carries handles.
            descriptor.assetId = mesh.assetId.empty() ? layers_[index].content.id : mesh.assetId;
            descriptor.geometry = mesh.geometry;
            descriptor.material = mesh.material;
            result.addMesh(std::move(descriptor));
        }

        result.lights.reserve(lightStates_.size());
        for (const auto& state : lightStates_) {
            if (!state.evaluated) throw std::logic_error("MotionScene::compile: light state is not evaluated");
            result.lights.push_back(state);
        }
        return result;
    }

    int MotionScene::contentEndFrame() const {

        int last = 0;

        for (const auto& layer : layers_) {
            if (!layer.lifetime.openEnded()) {
                last = std::max(last, layer.lifetime.outFrame);
            }
            const float endTime = layer.endTime();
            if (endTime > 0.f) {
                last = std::max(last, static_cast<int>(std::ceil(endTime * fps_)));
            }
        }

        return last;
    }

} // namespace chrononmotion::motion
