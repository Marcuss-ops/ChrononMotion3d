#include "chrononmotion/motion/RigDrivers.hpp"
#include "chrononmotion/motion/RigOperators.hpp"

#include <algorithm>
#include <cmath>
#include <queue>
#include <set>
#include <tuple>
#include <stdexcept>

namespace chrononmotion::motion {
    namespace {
        bool finite(const Vector3& v) {
            return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
        }
        unsigned dimensions(RigControllerKind kind) {
            switch (kind) {
                case RigControllerKind::Scalar:
                case RigControllerKind::Boolean:
                case RigControllerKind::Enum: return 1;
                case RigControllerKind::Joystick2D: return 2;
                case RigControllerKind::Spatial3D: return 3;
            }
            throw std::invalid_argument("RigDrivers: unknown controller kind");
        }
        unsigned arity(RigDriverKind kind) { return rigOperator(kind).inputArity; }
        template<class T> std::size_t byId(const std::vector<T>& values, std::uint32_t id) {
            const auto it = std::lower_bound(values.begin(), values.end(), id,
                                            [](const T& v, std::uint32_t key) { return v.id < key; });
            if (it == values.end() || it->id != id) throw std::invalid_argument("RigDrivers: missing reference");
            return static_cast<std::size_t>(it - values.begin());
        }
        template<class T> void sortIds(std::vector<T>& values) {
            std::sort(values.begin(), values.end(), [](const T& a, const T& b) { return a.id < b.id; });
            for (std::size_t i = 0; i < values.size(); ++i)
                if (!values[i].id || (i && values[i].id == values[i - 1].id))
                    throw std::invalid_argument("RigDrivers: zero or duplicate ID");
        }
        Vector3 controllerValue(const RigController& c, Vector3 value) {
            if (!finite(value)) throw std::invalid_argument("RigDrivers: nonfinite controller value");
            const auto dims = dimensions(c.kind);
            for (unsigned axis = 0; axis < 3; ++axis) {
                if (axis >= dims) {
                    if (value[axis] != 0.f) throw std::invalid_argument("RigDrivers: inactive controller component");
                    continue;
                }
                value[axis] = std::clamp(value[axis], c.minimum[axis], c.maximum[axis]);
            }
            if (c.kind == RigControllerKind::Boolean) value.x = value.x >= .5f ? 1.f : 0.f;
            if (c.kind == RigControllerKind::Enum) value.x = std::round(value.x);
            return value;
        }
        float noise(float coordinate, std::uint32_t seed) {
            // Integer hash at fixed cells; no global RNG or mutable history.
            if (std::abs(coordinate) > 1000000.f)
                throw std::invalid_argument("RigDrivers: noise coordinate budget exceeded");
            const auto cell = static_cast<std::int32_t>(std::floor(coordinate));
            const auto hash = [seed](std::int32_t v) {
                std::uint32_t x = static_cast<std::uint32_t>(v) ^ seed;
                x ^= x >> 16U; x *= 0x7feb352dU; x ^= x >> 15U;
                x *= 0x846ca68bU; x ^= x >> 16U;
                return static_cast<float>(x >> 8U) / 16777215.f * 2.f - 1.f;
            };
            float t = coordinate - std::floor(coordinate);
            t = t * t * (3.f - 2.f * t);
            return hash(cell) * (1.f - t) + hash(cell + 1) * t;
        }
    }

    RigDriverEvaluator::RigDriverEvaluator(const RigEvaluator& rig, const RigDriverGraph& graph)
        : rig_(rig), graph_(graph) {
        if (graph_.nodes.size() > 10000 || graph_.controllers.size() > 10000 || graph_.outputs.size() > 30000)
            throw std::invalid_argument("RigDrivers: graph budget exceeded");
        sortIds(graph_.nodes); sortIds(graph_.controllers);
        for (const auto& controller : graph_.controllers) {
            const auto node = byId(rig_.nodes(), controller.id);
            if (rig_.nodes()[node].kind != RigNodeKind::Controller || !finite(controller.neutral) ||
                !finite(controller.minimum) || !finite(controller.maximum))
                throw std::invalid_argument("RigDrivers: invalid controller");
            const auto dims = dimensions(controller.kind);
            for (unsigned axis = 0; axis < 3; ++axis) {
                if (controller.minimum[axis] > controller.maximum[axis] ||
                    controller.neutral[axis] < controller.minimum[axis] || controller.neutral[axis] > controller.maximum[axis] ||
                    (axis >= dims && controller.neutral[axis] != 0.f))
                    throw std::invalid_argument("RigDrivers: invalid controller range");
            }
            if (controller.kind == RigControllerKind::Boolean &&
                (controller.minimum.x != 0.f || controller.maximum.x != 1.f ||
                 (controller.neutral.x != 0.f && controller.neutral.x != 1.f)))
                throw std::invalid_argument("RigDrivers: boolean range must be 0..1");
            if (controller.kind == RigControllerKind::Enum &&
                (std::floor(controller.minimum.x) != controller.minimum.x ||
                 std::floor(controller.maximum.x) != controller.maximum.x ||
                 std::floor(controller.neutral.x) != controller.neutral.x))
                throw std::invalid_argument("RigDrivers: enum values must be integers");
        }
        const auto count = graph_.nodes.size();
        inputs_.resize(count); controllerIndices_.resize(count);
        firstIndices_.resize(count); secondIndices_.resize(count);
        std::vector<std::vector<std::size_t>> outgoing(count);
        std::vector<unsigned> indegree(count, 0);
        std::size_t keyBudget = 0;
        for (std::size_t i = 0; i < count; ++i) {
            const auto& node = graph_.nodes[i];
            validateRigOperator(node);
            const auto n = arity(node.kind);
            if (!std::isfinite(node.value) || !std::isfinite(node.inMin) || !std::isfinite(node.inMax) ||
                !std::isfinite(node.outMin) || !std::isfinite(node.outMax))
                throw std::invalid_argument("RigDrivers: nonfinite parameter");
            for (unsigned j = 0; j < 2; ++j) {
                if (j >= n) {
                    if (node.inputs[j]) throw std::invalid_argument("RigDrivers: unexpected input");
                    continue;
                }
                const auto dependency = byId(graph_.nodes, node.inputs[j]);
                inputs_[i][j] = dependency;
                outgoing[dependency].push_back(i); ++indegree[i];
            }
            if ((node.kind == RigDriverKind::Remap || node.kind == RigDriverKind::Smoothstep) && node.inMin >= node.inMax)
                throw std::invalid_argument("RigDrivers: invalid input interval");
            if (node.kind == RigDriverKind::Clamp && node.inMin > node.inMax)
                throw std::invalid_argument("RigDrivers: invalid clamp interval");
            if (node.kind == RigDriverKind::Controller) {
                controllerIndices_[i] = byId(graph_.controllers, node.controller);
                if (node.component >= dimensions(graph_.controllers[controllerIndices_[i]].kind))
                    throw std::invalid_argument("RigDrivers: invalid controller component");
            }
            if (node.kind == RigDriverKind::Distance || node.kind == RigDriverKind::Angle) {
                firstIndices_[i] = byId(rig_.nodes(), node.first);
                secondIndices_[i] = byId(rig_.nodes(), node.second);
            }
            if (node.kind == RigDriverKind::Track) {
                keyBudget += node.track.keys().size();
                if (keyBudget > 1000000 || node.track.empty())
                    throw std::invalid_argument("RigDrivers: invalid track budget");
                // Driver tracks are self-contained. No hidden channel links or modifiers.
                if (!node.track.modifiers().empty())
                    throw std::invalid_argument("RigDrivers: driver tracks must be unmodified");
                for (const auto& key : node.track.keys())
                    if (!std::isfinite(key.time) || !std::isfinite(key.value))
                        throw std::invalid_argument("RigDrivers: invalid track key");
            }
        }
        std::priority_queue<std::size_t, std::vector<std::size_t>, std::greater<>> ready;
        for (std::size_t i = 0; i < count; ++i) if (!indegree[i]) ready.push(i);
        while (!ready.empty()) {
            const auto i = ready.top(); ready.pop(); order_.push_back(i);
            for (const auto child : outgoing[i]) if (--indegree[child] == 0) ready.push(child);
        }
        if (order_.size() != count) throw std::invalid_argument("RigDrivers: dependency cycle");
        std::set<std::tuple<RigNodeId, RigDrivenProperty, unsigned>> properties;
        for (const auto& output : graph_.outputs) {
            outputDrivers_.push_back(byId(graph_.nodes, output.driver));
            outputTargets_.push_back(byId(rig_.nodes(), output.target));
            if (output.property < RigDrivenProperty::Position || output.property > RigDrivenProperty::Opacity ||
                output.component >= (output.property == RigDrivenProperty::Opacity ? 1U : 3U) ||
                !properties.emplace(output.target, output.property, output.component).second)
                throw std::invalid_argument("RigDrivers: invalid or ambiguous output property");
        }
    }

    RigPose RigDriverEvaluator::evaluate(float seconds, std::span<const RigControllerValue> controls,
                                        std::span<const RigPoseValue> pose) const {
        if (!std::isfinite(seconds) || controls.size() > graph_.controllers.size())
            throw std::invalid_argument("RigDrivers: invalid evaluation inputs");
        std::vector<Vector3> values;
        values.reserve(graph_.controllers.size());
        for (const auto& controller : graph_.controllers) values.push_back(controller.neutral);
        RigNodeId previous = 0;
        for (const auto& control : controls) {
            if (control.id <= previous) throw std::invalid_argument("RigDrivers: controller inputs must be unique/sorted");
            previous = control.id;
            const auto i = byId(graph_.controllers, control.id);
            values[i] = controllerValue(graph_.controllers[i], control.value);
        }
        const auto fk = rig_.evaluateFK(pose);
        std::vector<float> computed(graph_.nodes.size(), 0.f);
        for (const auto i : order_) {
            const auto& node = graph_.nodes[i];
            const float a = computed.empty() ? 0.f : computed[inputs_[i][0]];
            const float b = computed.empty() ? 0.f : computed[inputs_[i][1]];
            float value = 0.f;
            switch (node.kind) {
                case RigDriverKind::Constant: value = node.value; break;
                case RigDriverKind::Controller: value = values[controllerIndices_[i]][node.component]; break;
                case RigDriverKind::Time: value = seconds; break;
                case RigDriverKind::Track: value = node.track.sample(seconds); break;
                case RigDriverKind::Distance: {
                    Vector3 p, q;
                    p.setFromMatrixPosition(fk.world[firstIndices_[i]]);
                    q.setFromMatrixPosition(fk.world[secondIndices_[i]]);
                    value = p.distanceTo(q); break;
                }
                case RigDriverKind::Angle: {
                    Vector3 p, q, scale;
                    Quaternion first, second;
                    fk.world[firstIndices_[i]].decompose(p, first, scale);
                    fk.world[secondIndices_[i]].decompose(q, second, scale);
                    value = first.angleTo(second); break;
                }
                case RigDriverKind::Remap: {
                    const float t = std::clamp((a - node.inMin) / (node.inMax - node.inMin), 0.f, 1.f);
                    value = node.outMin * (1.f - t) + node.outMax * t; break;
                }
                case RigDriverKind::Clamp: value = std::clamp(a, node.inMin, node.inMax); break;
                case RigDriverKind::Smoothstep: {
                    const float t = std::clamp((a - node.inMin) / (node.inMax - node.inMin), 0.f, 1.f);
                    value = t * t * (3.f - 2.f * t); break;
                }
                case RigDriverKind::Add: value = a + b; break;
                case RigDriverKind::Multiply: value = a * b; break;
                case RigDriverKind::Min: value = std::min(a, b); break;
                case RigDriverKind::Max: value = std::max(a, b); break;
                case RigDriverKind::Noise: value = noise(a, node.seed); break;
            }
            if (!std::isfinite(value)) throw std::invalid_argument("RigDrivers: nonfinite result");
            computed[i] = value;
        }
        RigPose result;
        for (std::size_t i = 0; i < fk.ids.size(); ++i) result.push_back({fk.ids[i], fk.local[i]});
        for (std::size_t i = 0; i < graph_.outputs.size(); ++i) {
            const auto& output = graph_.outputs[i];
            auto& local = result[outputTargets_[i]].local;
            const float value = computed[outputDrivers_[i]];
            switch (output.property) {
                case RigDrivenProperty::Position: local.position[output.component] = value; break;
                case RigDrivenProperty::Rotation: local.rotation[output.component] = value; break;
                case RigDrivenProperty::Scale: local.scale[output.component] = value; break;
                case RigDrivenProperty::Opacity: local.opacity = value; break;
            }
        }
        (void) rig_.evaluate(result); // Same fail-closed pose authority.
        return result;
    }

    RigPose mixRigPoses(const RigEvaluator& rig, std::span<const RigPoseValue> base,
                         std::span<const RigPoseLayer> layers) {
        if (layers.size() > 1024) throw std::invalid_argument("RigPoses: layer budget exceeded");
        const auto evaluated = rig.evaluateFK(base);
        RigPose result;
        for (std::size_t i = 0; i < evaluated.ids.size(); ++i) result.push_back({evaluated.ids[i], evaluated.local[i]});
        for (const auto& layer : layers) {
            if (!std::isfinite(layer.weight) || layer.weight < 0.f || layer.weight > 1.f ||
                layer.mode < RigPoseBlendMode::Override || layer.mode > RigPoseBlendMode::MaskedBlend ||
                layer.mask.size() > result.size())
                throw std::invalid_argument("RigPoses: invalid layer");
            (void) rig.evaluate(layer.pose);
            RigNodeId previous = 0;
            for (const auto id : layer.mask) {
                if (id <= previous) throw std::invalid_argument("RigPoses: mask must be unique/sorted");
                previous = id; (void) byId(rig.nodes(), id);
            }
            if (layer.weight == 0.f) continue;
            for (const auto& value : layer.pose) {
                if (layer.mode == RigPoseBlendMode::MaskedBlend &&
                    !std::binary_search(layer.mask.begin(), layer.mask.end(), value.id)) continue;
                const auto i = byId(rig.nodes(), value.id);
                auto& current = result[i].local;
                if (layer.mode != RigPoseBlendMode::Additive) {
                    if (layer.weight == 1.f) current = value.local;
                    else current = Transform::blend(current, value.local, layer.weight);
                    continue;
                }
                const auto& rest = rig.nodes()[i].rest;
                current.position.addScaledVector(value.local.position - rest.position, layer.weight);
                current.anchor.addScaledVector(value.local.anchor - rest.anchor, layer.weight);
                current.opacity += (value.local.opacity - rest.opacity) * layer.weight;
                Vector3 ratio = value.local.scale; ratio.divide(rest.scale);
                ratio.lerpVectors({1.f, 1.f, 1.f}, ratio, layer.weight);
                current.scale.multiply(ratio);
                Quaternion inverse = rest.localQuaternion(); inverse.invert();
                Quaternion delta = value.local.localQuaternion(); delta.multiply(inverse).normalize();
                Quaternion weighted; weighted.slerpQuaternions(Quaternion{}, delta, layer.weight);
                current.orientation = current.localQuaternion();
                current.orientation.premultiply(weighted).normalize(); current.rotation = {};
            }
        }
        (void) rig.evaluate(result);
        return result;
    }
}
