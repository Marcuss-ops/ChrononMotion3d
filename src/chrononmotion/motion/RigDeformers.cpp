#include "chrononmotion/motion/RigDeformers.hpp"

#include "chrononmotion/math/Finite.hpp"#include "chrononmotion/motion/RigOperators.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace chrononmotion::motion {
    namespace {
        constexpr std::size_t budget = 1000000;
        void validatePoints(std::span<const Vector3> points) {
            if (points.size() > budget) throw std::invalid_argument("RigGeometry: vertex budget exceeded");
            for (const auto& point : points)
                if (!math::isFinite(point)) throw std::invalid_argument("RigGeometry: nonfinite point");
        }
        void validate(const RigDeformer& d) {
            (void) rigOperator(d.kind);
            if (d.kind < RigDeformerKind::Bend || d.kind > RigDeformerKind::SquashStretch ||
                !std::isfinite(d.amount) || d.amount < 0.f || d.amount > 1.f || !std::isfinite(d.strength) ||
                !std::isfinite(d.wavelength) || d.wavelength <= 0.f || !std::isfinite(d.phase) || !math::isFinite(d.origin))
                throw std::invalid_argument("DeformerStack: invalid operator");
            if ((d.kind == RigDeformerKind::SquashStretch || d.kind == RigDeformerKind::Bulge ||
                 d.kind == RigDeformerKind::Taper) && d.strength <= -1.f)
                throw std::invalid_argument("DeformerStack: singular deformation");
            if (d.kind == RigDeformerKind::PathWarp && !d.path)
                throw std::invalid_argument("DeformerStack: missing MotionPath");
            if (d.kind == RigDeformerKind::Lattice) {
                const auto& l = d.lattice;
                if (l.columns < 2 || l.rows < 2 || l.columns > 64 || l.rows > 64 ||
                    l.offsets.size() != l.columns * l.rows || !math::isFinite(l.minimum) || !math::isFinite(l.maximum) ||
                    l.maximum.x <= l.minimum.x || l.maximum.y <= l.minimum.y)
                    throw std::invalid_argument("DeformerStack: invalid lattice");
                validatePoints(l.offsets);
            }
            if (d.pins.size() > 256) throw std::invalid_argument("DeformerStack: pin budget exceeded");
            for (const auto& pin : d.pins)
                if (pin.kind < RigPinKind::Position || pin.kind > RigPinKind::Stiffness ||
                    !math::isFinite(pin.rest) || !math::isFinite(pin.target) || !math::isFinite(pin.scale) ||
                    pin.scale.x <= 0.f || pin.scale.y <= 0.f || pin.scale.z <= 0.f ||
                    !std::isfinite(pin.rotation.lengthSq()) || std::abs(pin.rotation.lengthSq() - 1.f) > 1e-4f ||
                    !std::isfinite(pin.radius) || pin.radius <= 0.f || !std::isfinite(pin.weight) ||
                    pin.weight < 0.f || pin.weight > 1.f)
                    throw std::invalid_argument("DeformerStack: invalid pin");
        }
        Vector3 deform(const Vector3& point, const RigDeformer& d) {
            Vector3 p = point - d.origin;
            const float tau = 6.283185307179586f;
            switch (d.kind) {
                case RigDeformerKind::Wave:
                    p.y += d.strength * std::sin(tau * p.x / d.wavelength + d.phase); break;
                case RigDeformerKind::Bend: {
                    if (d.strength == 0.f) break;
                    const float angle = p.x * d.strength;
                    const float radius = 1.f / d.strength;
                    const float x = (radius - p.y) * std::sin(angle);
                    p.y = radius - (radius - p.y) * std::cos(angle); p.x = x; break;
                }
                case RigDeformerKind::Twist: {
                    const float a = d.strength * p.y;
                    const float x = p.x * std::cos(a) - p.z * std::sin(a);
                    p.z = p.x * std::sin(a) + p.z * std::cos(a); p.x = x; break;
                }
                case RigDeformerKind::Bulge: {
                    const float u = std::clamp(std::abs(p.y) / d.wavelength, 0.f, 1.f);
                    const float scale = 1.f + d.strength * (1.f - u * u);
                    p.x *= scale; p.z *= scale; break;
                }
                case RigDeformerKind::Taper: {
                    const float scale = 1.f + d.strength * std::clamp(p.y / d.wavelength, 0.f, 1.f);
                    p.x *= scale; p.z *= scale; break;
                }
                case RigDeformerKind::SquashStretch: {
                    const float scale = 1.f + d.strength;
                    p.y *= scale; p.x /= std::sqrt(scale); p.z /= std::sqrt(scale); break;
                }
                case RigDeformerKind::NoiseWarp: {
                    const float seed = static_cast<float>(d.seed % 65536U);
                    const Vector3 rest = p;
                    p.x += d.strength * std::sin(rest.y / d.wavelength + seed);
                    p.y += d.strength * std::sin(rest.x / d.wavelength + seed * .37f);
                    p.z += d.strength * std::sin(rest.z / d.wavelength + seed * .71f); break;
                }
                case RigDeformerKind::Lattice: {
                    const auto& l = d.lattice;
                    if (p.x < l.minimum.x || p.x > l.maximum.x || p.y < l.minimum.y || p.y > l.maximum.y) break;
                    const float x = (p.x - l.minimum.x) / (l.maximum.x - l.minimum.x) * (l.columns - 1);
                    const float y = (p.y - l.minimum.y) / (l.maximum.y - l.minimum.y) * (l.rows - 1);
                    const auto col = std::min(static_cast<unsigned>(x), l.columns - 2);
                    const auto row = std::min(static_cast<unsigned>(y), l.rows - 2);
                    Vector3 lower, upper, offset;
                    lower.lerpVectors(l.offsets[row * l.columns + col], l.offsets[row * l.columns + col + 1], x - col);
                    upper.lerpVectors(l.offsets[(row + 1) * l.columns + col], l.offsets[(row + 1) * l.columns + col + 1], x - col);
                    offset.lerpVectors(lower, upper, y - row); p.add(offset); break;
                }
                case RigDeformerKind::PinWarp: {
                    const Vector3 rest = p;
                    Vector3 delta{};
                    float total = 0.f, stiffness = 0.f;
                    for (const auto& pin : d.pins) {
                        const float t = std::clamp(1.f - rest.distanceTo(pin.rest) / pin.radius, 0.f, 1.f);
                        const float weight = pin.weight * t * t * (3.f - 2.f * t);
                        if (pin.kind == RigPinKind::Stiffness) { stiffness = std::max(stiffness, weight); continue; }
                        Vector3 desired = rest;
                        if (pin.kind == RigPinKind::Position) desired.add(pin.target - pin.rest);
                        else {
                            desired.sub(pin.rest);
                            if (pin.kind == RigPinKind::Rotation) desired.applyQuaternion(pin.rotation);
                            else desired.multiply(pin.scale);
                            desired.add(pin.rest);
                        }
                        delta.addScaledVector(desired - rest, weight); total += weight;
                    }
                    if (total > 1.f) delta.divideScalar(total);
                    p.addScaledVector(delta, 1.f - stiffness); break;
                }
                case RigDeformerKind::PathWarp: {
                    const float progress = std::clamp(p.x / d.wavelength, 0.f, 1.f);
                    Vector3 offset{0.f, p.y, p.z};
                    offset.applyQuaternion(d.path->orientToPath(progress));
                    p = d.path->sampleUniform(progress); p.add(offset); break;
                }
            }
            p.add(d.origin);
            return p;
        }
    }
    std::vector<Vector3> DeformerStack::evaluate(std::span<const Vector3> source) const {
        validatePoints(source);
        if (operators.size() > 256 || (!source.empty() && operators.size() > budget * 16 / source.size()))
            throw std::invalid_argument("DeformerStack: work budget exceeded");
        std::size_t workPerVertex = 0;
        for (const auto& op : operators) {
            validate(op);
            workPerVertex += op.kind == RigDeformerKind::PinWarp ? std::max<std::size_t>(1, op.pins.size()) : 1;
        }
        if (!source.empty() && workPerVertex > budget * 16 / source.size())
            throw std::invalid_argument("DeformerStack: pin work budget exceeded");
        std::vector<Vector3> result(source.begin(), source.end());
        for (const auto& op : operators) {
            if (op.amount == 0.f) continue;
            for (auto& point : result) {
                const Vector3 desired = deform(point, op);
                if (op.amount == 1.f) point = desired;
                else point.lerpVectors(point, desired, op.amount);
                if (!math::isFinite(point)) throw std::invalid_argument("DeformerStack: nonfinite result");
            }
        }
        return result;
    }
    std::vector<Vector3> skinRigGeometry(const RigEvaluator& rig, const RigEvaluation& pose,
                                         std::span<const Vector3> source, std::span<const RigSkinVertex> bindings,
                                         std::span<const RigSkinMatrixBinding> inverseBinds) {
        validatePoints(source);
        const auto bind = rig.bindPose();
        if (bindings.size() != source.size() || pose.ids != bind.ids || pose.world.size() != bind.world.size())
            throw std::invalid_argument("RigSkin: topology mismatch");
        if (inverseBinds.size() > bind.ids.size()) throw std::invalid_argument("RigSkin: inverse bind budget exceeded");
        std::vector<bool> overridden(bind.ids.size(), false);
        for (const auto& entry : inverseBinds) {
            const auto index = bind.index(entry.joint);
            if (overridden[index]) throw std::invalid_argument("RigSkin: duplicate inverse bind");
            overridden[index] = true;
            for (const auto value : entry.inverseBind.elements)
                if (!std::isfinite(value)) throw std::invalid_argument("RigSkin: nonfinite inverse bind");
            const auto& e = entry.inverseBind.elements;
            if (e[3] != 0.f || e[7] != 0.f || e[11] != 0.f || e[15] != 1.f || entry.inverseBind.determinant() == 0.f)
                throw std::invalid_argument("RigSkin: nonaffine or singular inverse bind");
        }
        std::vector<Matrix4> matrices;
        for (std::size_t i = 0; i < bind.world.size(); ++i) {
            Matrix4 inverse = bind.world[i];
            if (inverse.determinant() == 0.f) throw std::invalid_argument("RigSkin: singular bind pose");
            inverse.invert();
            for (const auto& entry : inverseBinds) if (entry.joint == bind.ids[i]) inverse = entry.inverseBind;
            Matrix4 matrix = pose.world[i]; matrix.multiply(inverse);
            for (const float x : matrix.elements)
                if (!std::isfinite(x)) throw std::invalid_argument("RigSkin: invalid matrix");
            matrices.push_back(matrix);
        }
        std::vector<Vector3> result;
        result.reserve(source.size());
        for (std::size_t vertex = 0; vertex < source.size(); ++vertex) {
            const auto& binding = bindings[vertex];
            float total = 0.f;
            Vector3 point{};
            for (unsigned influence = 0; influence < 4; ++influence) {
                const float weight = binding.weights[influence];
                if (!std::isfinite(weight) || weight < 0.f || weight > 1.f)
                    throw std::invalid_argument("RigSkin: invalid weight");
                if (weight == 0.f) {
                    if (binding.joints[influence]) throw std::invalid_argument("RigSkin: unused influence must be zero");
                    continue;
                }
                for (unsigned previous = 0; previous < influence; ++previous)
                    if (binding.joints[previous] == binding.joints[influence])
                        throw std::invalid_argument("RigSkin: duplicate influence");
                const auto i = pose.index(binding.joints[influence]);
                Vector3 transformed = source[vertex]; transformed.applyMatrix4(matrices[i]);
                point.addScaledVector(transformed, weight); total += weight;
            }
            if (std::abs(total - 1.f) > 1e-5f) throw std::invalid_argument("RigSkin: weights must sum to one");
            if (!math::isFinite(point)) throw std::invalid_argument("RigSkin: nonfinite result");
            result.push_back(point);
        }
        return result;
    }
    std::vector<Vector3> mixRigMorphs(std::span<const Vector3> base, std::span<const RigMorphTarget> targets) {
        validatePoints(base);
        if (targets.size() > 256 || (!base.empty() && targets.size() > budget * 16 / base.size()))
            throw std::invalid_argument("RigMorph: work budget exceeded");
        for (const auto& target : targets) {
            if (target.positions.size() != base.size() || !std::isfinite(target.weight) || target.weight < 0.f || target.weight > 1.f)
                throw std::invalid_argument("RigMorph: topology or weight invalid");
            validatePoints(target.positions);
        }
        std::vector<Vector3> result(base.begin(), base.end());
        for (const auto& target : targets) {
            if (target.weight == 0.f) continue;
            for (std::size_t i = 0; i < base.size(); ++i) result[i].addScaledVector(target.positions[i] - base[i], target.weight);
        }
        validatePoints(result);
        return result;
    }
}
