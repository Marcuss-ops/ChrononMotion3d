#include "chrononmotion/motion/OfflineBaking.hpp"
#include "OfflineBakingInternal.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_set>

namespace chrononmotion::motion {
using detail::addVectorKey;
using detail::kEpsilon;
using detail::kMaximumBakedFrames;
using detail::kMaximumBakedWork;
using detail::validateFrameRange;
using detail::validateFrameTimes;
using detail::wrapAngle;

namespace {
struct DoubleVector3 {
    double x;
    double y;
    double z;
};

DoubleVector3 toDouble(const Vector3& value) {
    return {value.x, value.y, value.z};
}

DoubleVector3 add(const DoubleVector3& a, const DoubleVector3& b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

DoubleVector3 subtract(const DoubleVector3& a, const DoubleVector3& b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

DoubleVector3 multiply(const DoubleVector3& value, double scalar) {
    return {value.x * scalar, value.y * scalar, value.z * scalar};
}

double dot(const DoubleVector3& a, const DoubleVector3& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

DoubleVector3 cross(const DoubleVector3& a, const DoubleVector3& b) {
    return {a.y * b.z - a.z * b.y,
            a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x};
}

double length(const DoubleVector3& value) {
    return std::hypot(std::hypot(value.x, value.y), value.z);
}

DoubleVector3 normalized(const DoubleVector3& value) {
    const double magnitude = length(value);
    return magnitude > 0.0 ? multiply(value, 1.0 / magnitude) : DoubleVector3{0.0, 0.0, 0.0};
}

Vector3 toFloat(const DoubleVector3& value, const char* function) {
    const double maximum = std::numeric_limits<float>::max();
    if (!std::isfinite(value.x) || !std::isfinite(value.y) || !std::isfinite(value.z) ||
        std::abs(value.x) > maximum || std::abs(value.y) > maximum || std::abs(value.z) > maximum) {
        throw std::invalid_argument(std::string(function) + ": solved pose exceeds finite float range");
    }
    return Vector3(static_cast<float>(value.x), static_cast<float>(value.y), static_cast<float>(value.z));
}
} // namespace

BakedVerletChain bakeVerletChain(const VerletChainSpec& spec, int firstFrame, int lastFrame) {
    validateFrameRange(firstFrame, lastFrame);
    if (spec.joints.size() < 2 || spec.joints.size() > 65536 || !std::isfinite(spec.fps) || spec.fps <= 0.f ||
        !std::isfinite(spec.damping) || spec.damping < 0.f || spec.damping >= 1.f ||
        !math::isFinite(spec.gravity) || spec.iterations == 0 || spec.iterations > 256 ||
        !std::isfinite(spec.stiffness) || spec.stiffness < 0.f || !std::isfinite(spec.mass) || spec.mass <= 0.f ||
        !math::isFinite(spec.force) || !std::isfinite(spec.drag) || spec.drag < 0.f ||
        !std::isfinite(spec.maxAngle) || spec.maxAngle < 0.f || spec.maxAngle > math::PI)
        throw std::invalid_argument("bakeVerletChain: invalid chain or simulation parameters");
    const std::size_t requestedFrames = static_cast<std::size_t>(lastFrame - firstFrame) + 1;
    if (spec.joints.size() > kMaximumBakedFrames / requestedFrames ||
        spec.iterations > kMaximumBakedWork / requestedFrames / spec.joints.size())
        throw std::invalid_argument("bakeVerletChain: requested samples or solver work exceed the offline budget");
    validateFrameTimes(spec.fps, firstFrame, lastFrame, "bakeVerletChain");
    for (const auto& point : spec.joints)
        if (!math::isFinite(point)) throw std::invalid_argument("bakeVerletChain: joints must be finite");
    const std::vector<Vector2> rest = spec.joints;
    std::vector<float> lengths;
    lengths.reserve(rest.size() - 1);
    for (std::size_t i = 1; i < rest.size(); ++i) {
        const float length = rest[i].distanceTo(rest[i - 1]);
        if (!std::isfinite(length) || !(length > kEpsilon))
            throw std::invalid_argument("bakeVerletChain: adjacent joints must differ by a finite distance");
        lengths.push_back(length);
    }
    const std::size_t frames = static_cast<std::size_t>(lastFrame - firstFrame) + 1;
    BakedVerletChain result;
    result.samples.reserve(frames);
    result.jointPositions.resize(rest.size());
    result.segmentRotations.resize(rest.size() - 1);
    auto current = rest;
    auto previous = rest;
    const float dt = 1.f / spec.fps;
    if (!std::isfinite(dt) || !std::isfinite(dt * dt) || !std::isfinite(1.f / spec.mass) ||
        spec.stiffness / spec.mass * dt * dt > 1.f)
        throw std::invalid_argument("bakeVerletChain: nonfinite or unstable fixed step");
    const float dragFactor = std::exp(-spec.drag * dt);
    const Vector2 acceleration = spec.gravity + spec.force * (1.f / spec.mass);
    if (!math::isFinite(acceleration)) throw std::invalid_argument("bakeVerletChain: force acceleration overflow");
    for (std::size_t frame = 0; frame < frames; ++frame) {
        result.samples.push_back(current);
        const int frameNumber = firstFrame + static_cast<int>(frame);
        for (std::size_t joint = 0; joint < current.size(); ++joint) {
            if (!math::isFinite(current[joint]))
                throw std::invalid_argument("bakeVerletChain: simulation state overflowed finite range");
            addVectorKey(result.jointPositions[joint], frameNumber, spec.fps, current[joint]);
        }
        for (std::size_t joint = 1; joint < current.size(); ++joint) {
            const        Vector2 position = current[joint];
            Vector2 velocity = (current[joint] - previous[joint]) * ((1.f - spec.damping) * dragFactor);
            Vector2 restoring{};
            if (spec.stiffness > 0.f) restoring = (rest[joint] - current[joint]) * (spec.stiffness / spec.mass);
            current[joint] = current[joint] + velocity + (acceleration + restoring) * (dt * dt);
            previous[joint] = position;
        }
        for (unsigned iteration = 0; iteration < spec.iterations; ++iteration) {
            current.front() = rest.front();
            for (std::size_t joint = 0; joint + 1 < current.size(); ++joint) {
                Vector2 delta = current[joint + 1] - current[joint];
                const float distance = delta.length();
                if (distance <= kEpsilon) continue;
                const Vector2 correction = delta * ((distance - lengths[joint]) / distance);
                if (joint == 0) current[joint + 1] -= correction;
                else {
                    current[joint] += correction * 0.5f;
                    current[joint + 1] -= correction * 0.5f;
                }
            }
        }
        current.front() = rest.front();
        if (spec.maxAngle < math::PI) {
            // Parent-first angular projection also reconstructs exact rest lengths.
            for (std::size_t joint = 1; joint < current.size(); ++joint) {
                const Vector2 restDirection = rest[joint] - rest[joint - 1];
                const Vector2 direction = current[joint] - current[joint - 1];
                const float restAngle = std::atan2(restDirection.y, restDirection.x);
                const float delta = std::clamp(wrapAngle(std::atan2(direction.y, direction.x) - restAngle),
                                               -spec.maxAngle, spec.maxAngle);
                const float angle = restAngle + delta;
                current[joint] = current[joint - 1] + Vector2(std::cos(angle), std::sin(angle)) * lengths[joint - 1];
            }
        }
        for (const Vector2& point : current)
            if (!math::isFinite(point))
                throw std::invalid_argument("bakeVerletChain: simulation state overflowed finite range");
        const auto& sampledPose = result.samples.back();
        for (std::size_t segment = 0; segment + 1 < rest.size(); ++segment) {
            const Vector2 direction = sampledPose[segment + 1] - sampledPose[segment];
            result.segmentRotations[segment].add(static_cast<float>(frameNumber) / spec.fps,
                                                  Vector3(0.f, 0.f, std::atan2(direction.y, direction.x)));
        }
    }
    return result;
}

BakedRigidBodies2D bakeRigidBodies2D(std::vector<RigidBody2D> bodies,
                                      const RigidBodyWorld2D& world,
                                      int firstFrame, int lastFrame) {
    validateFrameRange(firstFrame, lastFrame);
    if (!std::isfinite(world.fps) || world.fps <= 0.f || !math::isFinite(world.gravity) ||
        !std::isfinite(world.floorY) || !std::isfinite(world.left) ||
        !std::isfinite(world.right) || world.left >= world.right ||
        bodies.size() > 4096 || world.solverIterations == 0 || world.solverIterations > 128 ||
        !std::isfinite(world.right - world.left))
        throw std::invalid_argument("bakeRigidBodies2D: invalid world parameters");
    const float dt = 1.f / world.fps;
    if (!std::isfinite(dt) || !std::isfinite(dt * dt))
        throw std::invalid_argument("bakeRigidBodies2D: fps is too small for a finite fixed step");
    validateFrameTimes(world.fps, firstFrame, lastFrame, "bakeRigidBodies2D");
    const std::size_t requestedFrames = static_cast<std::size_t>(lastFrame - firstFrame) + 1;
    const std::size_t pairCount = bodies.size() < 2 ? 0 : bodies.size() * (bodies.size() - 1) / 2;
    if (bodies.size() != 0 && requestedFrames > kMaximumBakedFrames / bodies.size())
        throw std::invalid_argument("bakeRigidBodies2D: requested position samples exceed the offline sample budget");
    if (pairCount != 0 && world.solverIterations > kMaximumBakedWork / requestedFrames / pairCount)
        throw std::invalid_argument("bakeRigidBodies2D: collision work exceeds the offline operation budget");
    std::unordered_set<std::uint32_t> ids;
    for (const auto& body : bodies) {
        if (!ids.insert(body.id).second || !math::isFinite(body.position) || !math::isFinite(body.velocity) ||
            !std::isfinite(body.radius) || body.radius <= 0.f || !std::isfinite(body.mass) ||
            !std::isfinite(body.restitution) || body.restitution < 0.f || body.restitution > 1.f ||
            !std::isfinite(body.friction) || body.friction < 0.f || body.friction > 1.f ||
            static_cast<double>(body.radius) * 2.0 > static_cast<double>(world.right) - world.left ||
            static_cast<double>(body.position.x) - body.radius < world.left ||
            static_cast<double>(body.position.x) + body.radius > world.right ||
            static_cast<double>(body.position.y) - body.radius < world.floorY ||
            (body.mass > 0.f && !std::isfinite(1.f / body.mass)) ||
            !std::isfinite(world.floorY + body.radius))
            throw std::invalid_argument("bakeRigidBodies2D: invalid or duplicate body");
    }
    BakedRigidBodies2D result;
    result.positionTracks.resize(bodies.size());
    result.ids.reserve(bodies.size());
    for (const auto& body : bodies) result.ids.push_back(body.id);
    const std::size_t frameCount = static_cast<std::size_t>(lastFrame - firstFrame) + 1;
    result.samples.reserve(frameCount);
    for (std::size_t frame = 0; frame < frameCount; ++frame) {
        std::vector<Vector2> positions;
        positions.reserve(bodies.size());
        const int frameNumber = firstFrame + static_cast<int>(frame);
        for (std::size_t i = 0; i < bodies.size(); ++i) {
            auto& body = bodies[i];
            if (body.mass > 0.f && world.gravity.y >= 0.f &&
                (body.position.y - body.radius < world.floorY ||
                 (body.position.y - body.radius == world.floorY && body.velocity.y <= 0.f))) {
                body.position.y = world.floorY + body.radius;
                if (body.velocity.y < 0.f) body.velocity.y = 0.f;
            }
            if (body.mass > 0.f && world.gravity.y < 0.f &&
                body.position.y - body.radius <= world.floorY && body.velocity.y < 0.f) {
                body.position.y = world.floorY + body.radius;
                body.velocity.y = -body.velocity.y * body.restitution;
                body.velocity.x *= 1.f - body.friction;
            }
            if (!math::isFinite(body.position) || !math::isFinite(body.velocity))
                throw std::invalid_argument("bakeRigidBodies2D: simulation state overflowed");
            positions.push_back(body.position);
            addVectorKey(result.positionTracks[i], frameNumber, world.fps, body.position);
            if (body.mass > 0.f) {
                body.velocity.add(world.gravity * dt);
                body.position.add(body.velocity * dt);
            }
        }
        result.samples.push_back(std::move(positions));
        for (unsigned iteration = 0; iteration < world.solverIterations; ++iteration) {
            for (auto& body : bodies) {
                if (body.mass <= 0.f) continue;
                const float floor = world.floorY + body.radius;
                if (body.position.y <= floor && body.velocity.y < 0.f) {
                    body.position.y = floor;
                    body.velocity.y = -body.velocity.y * body.restitution;
                    body.velocity.x *= 1.f - body.friction;
                } else if (body.position.y < floor) {
                    body.position.y = floor;
                }
                if (body.position.x - body.radius < world.left) {
                    body.position.x = world.left + body.radius;
                    if (body.velocity.x < 0.f) body.velocity.x = -body.velocity.x * body.restitution;
                }
                if (body.position.x + body.radius > world.right) {
                    body.position.x = world.right - body.radius;
                    if (body.velocity.x > 0.f) body.velocity.x = -body.velocity.x * body.restitution;
                }
            }
            for (std::size_t i = 0; i < bodies.size(); ++i) {
                for (std::size_t j = i + 1; j < bodies.size(); ++j) {
                    auto& a = bodies[i];
                    auto& b = bodies[j];
                    const float invA = a.mass > 0.f ? 1.f / a.mass : 0.f;
                    const float invB = b.mass > 0.f ? 1.f / b.mass : 0.f;
                    const float inverseMass = invA + invB;
                    if (inverseMass <= 0.f) continue;
                    Vector2 normal = b.position - a.position;
                    const float distance = normal.length();
                    const float overlap = a.radius + b.radius - distance;
                    if (overlap <= 0.f) continue;
                    if (distance <= kEpsilon) normal.set(1.f, 0.f);
                    else normal.divideScalar(distance);
                    a.position -= normal * (overlap * invA / inverseMass);
                    b.position += normal * (overlap * invB / inverseMass);
                    Vector2 relativeVelocity = b.velocity - a.velocity;
                    const float normalSpeed = relativeVelocity.dot(normal);
                    if (normalSpeed >= 0.f) continue;
                    const float restitution = std::min(a.restitution, b.restitution);
                    const float impulse = -(1.f + restitution) * normalSpeed / inverseMass;
                    const Vector2 normalImpulse = normal * impulse;
                    a.velocity -= normalImpulse * invA;
                    b.velocity += normalImpulse * invB;
                    relativeVelocity = b.velocity - a.velocity;
                    Vector2 tangent = relativeVelocity - normal * relativeVelocity.dot(normal);
                    if (tangent.lengthSq() > kEpsilon * kEpsilon) {
                        tangent.normalize();
                        float frictionImpulse = -relativeVelocity.dot(tangent) / inverseMass;
                        const float coefficient = std::sqrt(a.friction * b.friction);
                        frictionImpulse = std::clamp(frictionImpulse, -impulse * coefficient, impulse * coefficient);
                        const Vector2 friction = tangent * frictionImpulse;
                        a.velocity -= friction * invA;
                        b.velocity += friction * invB;
                    }
                }
            }
            // Pair separation can push a body through a wall after the first
            // boundary pass; clamp again before the next solver iteration/frame.
        for (auto& body : bodies) {
            if (body.mass <= 0.f) continue;
            if (body.position.x - body.radius < world.left) {
                    body.position.x = world.left + body.radius;
                    if (body.velocity.x < 0.f) body.velocity.x = -body.velocity.x * body.restitution;
                }
                if (body.position.x + body.radius > world.right) {
                    body.position.x = world.right - body.radius;
                    if (body.velocity.x > 0.f) body.velocity.x = -body.velocity.x * body.restitution;
                }
            }
            for (const auto& body : bodies) {
                if (!math::isFinite(body.position) || !math::isFinite(body.velocity))
                    throw std::invalid_argument("bakeRigidBodies2D: simulation state overflowed finite range");
            }
        }
    }
    return result;
}

IK2Result solveTwoSegmentIK2D(const Vector2& root, const Vector2& target,
                               float upperLength, float lowerLength,
                               float parentRotation, bool bendPositive) {
    if (!math::isFinite(root) || !math::isFinite(target) || !std::isfinite(upperLength) ||
        !std::isfinite(lowerLength) || upperLength <= 0.f || lowerLength <= 0.f ||
        !std::isfinite(parentRotation))
        throw std::invalid_argument("solveTwoSegmentIK2D: positions and positive segment lengths must be finite");
    const double deltaX = static_cast<double>(target.x) - root.x;
    const double deltaY = static_cast<double>(target.y) - root.y;
    const double distance = std::hypot(deltaX, deltaY);
    const double upper = upperLength;
    const double lower = lowerLength;
    const double maximum = upper + lower;
    const double minimum = std::abs(upper - lower);
    if (!std::isfinite(distance) || !std::isfinite(maximum) || !std::isfinite(minimum))
        throw std::invalid_argument("solveTwoSegmentIK2D: target and segment distances must be finite");
    const double scale = std::max({maximum, distance, static_cast<double>(std::numeric_limits<float>::min())});
    const double tolerance = 8.0 * std::numeric_limits<float>::epsilon() * scale;
    const bool reachable = distance <= maximum + tolerance && distance + tolerance >= minimum;
    const double solvedDistance = std::clamp(distance, minimum, maximum);
    const float directionAngle = distance > 0.0
        ? static_cast<float>(std::atan2(deltaY, deltaX)) : parentRotation;
    const double directionX = std::cos(directionAngle);
    const double directionY = std::sin(directionAngle);
    const double bendSign = bendPositive ? 1.0 : -1.0;

    double shoulder;
    double endX;
    double endY;
    if (solvedDistance == 0.0) {
        // Equal-length segments folded back onto the root have no unique target
        // direction; use the parent direction and a deterministic quarter turn.
        shoulder = static_cast<double>(directionAngle) + bendSign * math::PI * 0.5;
        endX = root.x;
        endY = root.y;
    } else {
        const double cosine = std::clamp((upper * upper + solvedDistance * solvedDistance - lower * lower) /
                                         (2.0 * upper * solvedDistance), -1.0, 1.0);
        shoulder = static_cast<double>(directionAngle) + bendSign * std::acos(cosine);
        endX = static_cast<double>(root.x) + directionX * solvedDistance;
        endY = static_cast<double>(root.y) + directionY * solvedDistance;
    }
    const double elbowX = static_cast<double>(root.x) + std::cos(shoulder) * upper;
    const double elbowY = static_cast<double>(root.y) + std::sin(shoulder) * upper;
    const float lowerAngle = static_cast<float>(std::atan2(endY - elbowY, endX - elbowX));
    IK2Result result;
    result.elbow.set(static_cast<float>(elbowX), static_cast<float>(elbowY));
    result.end = reachable ? target : Vector2(static_cast<float>(endX), static_cast<float>(endY));
    result.shoulderRotation = wrapAngle(static_cast<float>(shoulder) - parentRotation);
    result.elbowRotation = wrapAngle(lowerAngle - static_cast<float>(shoulder));
    result.targetReachable = reachable;
    if (!math::isFinite(result.elbow) || !math::isFinite(result.end) || !std::isfinite(result.shoulderRotation) ||
        !std::isfinite(result.elbowRotation))
        throw std::invalid_argument("solveTwoSegmentIK2D: solved pose exceeds finite float range");
    return result;
}

IK3Result solveIKChain3D(std::span<const Vector3> initialJoints,
                         const Vector3& target, const Vector3& pole,
                         unsigned iterations, float tolerance) {
    if (initialJoints.size() < 2 || initialJoints.size() > 65536 ||
        !math::isFinite(target) || !math::isFinite(pole) || iterations == 0 || iterations > 256 ||
        !std::isfinite(tolerance) || tolerance <= 0.f) {
        throw std::invalid_argument("solveIKChain3D: chain, target, pole and solver settings must be valid");
    }
    for (const auto& joint : initialJoints) {
        if (!math::isFinite(joint)) throw std::invalid_argument("solveIKChain3D: initial joints must be finite");
    }

    std::vector<DoubleVector3> positions;
    positions.reserve(initialJoints.size());
    for (const auto& joint : initialJoints) positions.push_back(toDouble(joint));
    std::vector<double> lengths;
    lengths.reserve(initialJoints.size() - 1);
    double totalLength = 0.0;
    double longestLength = 0.0;
    std::size_t longestIndex = 0;
    for (std::size_t index = 1; index < positions.size(); ++index) {
        const double segmentLength = length(subtract(positions[index], positions[index - 1]));
        if (!std::isfinite(segmentLength) || segmentLength <= kEpsilon) {
            throw std::invalid_argument("solveIKChain3D: adjacent joints must have positive finite separation");
        }
        lengths.push_back(segmentLength);
        totalLength += segmentLength;
        if (segmentLength > longestLength) {
            longestLength = segmentLength;
            longestIndex = index - 1;
        }
    }
    if (!std::isfinite(totalLength))
        throw std::invalid_argument("solveIKChain3D: total chain length is not finite");

    const DoubleVector3 root = positions.front();
    const DoubleVector3 requestedTarget = toDouble(target);
    DoubleVector3 rootToTarget = subtract(requestedTarget, root);
    const double targetDistance = length(rootToTarget);
    if (!std::isfinite(targetDistance))
        throw std::invalid_argument("solveIKChain3D: target distance is not finite");
    const double minimumReach = std::max(0.0, 2.0 * longestLength - totalLength);
    const double reachTolerance = std::max(static_cast<double>(tolerance),
            totalLength * 8.0 * std::numeric_limits<float>::epsilon());
    const bool reachable = targetDistance <= totalLength + reachTolerance &&
                           targetDistance + reachTolerance >= minimumReach;
    if (targetDistance <= 1e-12) {
        rootToTarget = subtract(positions.back(), root);
        if (length(rootToTarget) <= 1e-12)
            rootToTarget = subtract(positions[1], root);
        if (length(rootToTarget) <= 1e-12) rootToTarget = {1.0, 0.0, 0.0};
    }
    const DoubleVector3 targetDirection = normalized(rootToTarget);
    const double solvedDistance = std::clamp(targetDistance, minimumReach, totalLength);
    const DoubleVector3 solvedTarget = add(root, multiply(targetDirection, solvedDistance));
    if (!reachable) {
        positions.front() = root;
        if (targetDistance > totalLength) {
            double travelled = 0.0;
            for (std::size_t index = 1; index < positions.size(); ++index) {
                travelled += lengths[index - 1];
                positions[index] = add(root, multiply(targetDirection, travelled));
            }
        } else {
            double travelled = 0.0;
            for (std::size_t index = 1; index < positions.size(); ++index) {
                const double sign = index - 1 == longestIndex ? 1.0 : -1.0;
                travelled += sign * lengths[index - 1];
                positions[index] = add(root, multiply(targetDirection, travelled));
            }
        }
    }

    const DoubleVector3 polePoint = toDouble(pole);
    const auto applyPole = [&]() {
        for (std::size_t index = 1; index + 1 < positions.size(); ++index) {
            const DoubleVector3 axis = normalized(subtract(positions[index + 1], positions[index - 1]));
            if (length(axis) <= 1e-12) continue;
            const DoubleVector3 jointOffset = subtract(positions[index], positions[index - 1]);
            const DoubleVector3 poleOffset = subtract(polePoint, positions[index - 1]);
            const DoubleVector3 jointRadial = subtract(jointOffset, multiply(axis, dot(jointOffset, axis)));
            const DoubleVector3 poleRadial = subtract(poleOffset, multiply(axis, dot(poleOffset, axis)));
            if (length(jointRadial) <= 1e-12 || length(poleRadial) <= 1e-12) continue;
            const DoubleVector3 from = normalized(jointRadial);
            const DoubleVector3 toward = normalized(poleRadial);
            const double angle = std::atan2(dot(axis, cross(from, toward)),
                                            std::clamp(dot(from, toward), -1.0, 1.0));
            const double cosine = std::cos(angle);
            const double sine = std::sin(angle);
            const DoubleVector3 rotated = add(
                    add(multiply(jointRadial, cosine), multiply(cross(axis, jointRadial), sine)),
                    multiply(axis, dot(axis, jointRadial) * (1.0 - cosine)));
            positions[index] = add(positions[index - 1],
                    add(multiply(axis, dot(jointOffset, axis)), rotated));
        }
    };

    if (reachable) {
        for (unsigned iteration = 0; iteration < iterations; ++iteration) {
            positions.back() = solvedTarget;
            for (std::size_t index = positions.size() - 1; index > 0; --index) {
                DoubleVector3 direction = normalized(subtract(positions[index - 1], positions[index]));
                if (length(direction) <= 1e-12) direction = multiply(targetDirection, -1.0);
                positions[index - 1] = add(positions[index], multiply(direction, lengths[index - 1]));
            }
            positions.front() = root;
            for (std::size_t index = 1; index < positions.size(); ++index) {
                DoubleVector3 direction = normalized(subtract(positions[index], positions[index - 1]));
                if (length(direction) <= 1e-12) direction = targetDirection;
                positions[index] = add(positions[index - 1], multiply(direction, lengths[index - 1]));
            }
            applyPole();
            if (length(subtract(positions.back(), solvedTarget)) <= tolerance) break;
        }
    } else {
        applyPole();
    }

    IK3Result result;
    result.joints.reserve(positions.size());
    for (const auto& position : positions) result.joints.push_back(toFloat(position, "solveIKChain3D"));
    result.targetReachable = reachable;
    return result;
}

BakedIKChain3D bakeIKChain3D(const IK3BakeSpec& spec, int firstFrame, int lastFrame) {
    validateFrameRange(firstFrame, lastFrame);
    if (spec.initialJoints.size() < 2 || spec.targetTrack.empty() ||
        !math::isFinite(spec.pole) || !std::isfinite(spec.fps) || spec.fps <= 0.f ||
        spec.iterations == 0 || spec.iterations > 256 ||
        !std::isfinite(spec.tolerance) || spec.tolerance <= 0.f) {
        throw std::invalid_argument("bakeIKChain3D: chain, target track and solver settings must be valid");
    }
    if (!spec.targetTrack.modifierProblem().empty())
        throw std::invalid_argument("bakeIKChain3D: target track has invalid modifiers");
    for (const auto& modifier : spec.targetTrack.modifiers()) {
        if (std::holds_alternative<LinkModifier>(modifier))
            throw std::invalid_argument("bakeIKChain3D: target track must not require a scene link resolver");
    }
    for (const auto& key : spec.targetTrack.keys()) {
        if (!std::isfinite(key.time) || !math::isFinite(key.value))
            throw std::invalid_argument("bakeIKChain3D: target track keys must be finite");
    }
    const std::size_t frameCount = static_cast<std::size_t>(lastFrame - firstFrame) + 1;
    const std::size_t jointCount = spec.initialJoints.size();
    if (jointCount > kMaximumBakedFrames / frameCount ||
        spec.iterations > kMaximumBakedWork / frameCount / (jointCount - 1)) {
        throw std::invalid_argument("bakeIKChain3D: requested samples or solver work exceed the offline budget");
    }
    validateFrameTimes(spec.fps, firstFrame, lastFrame, "bakeIKChain3D");

    BakedIKChain3D result;
    result.samples.reserve(frameCount);
    result.jointPositions.resize(jointCount);
    result.targetReachable.reserve(frameCount);
    for (std::size_t offset = 0; offset < frameCount; ++offset) {
        const int frame = firstFrame + static_cast<int>(offset);
        const float seconds = static_cast<float>(frame) / spec.fps;
        const Vector3 target = spec.targetTrack.sample(seconds);
        const IK3Result solved = solveIKChain3D(spec.initialJoints, target, spec.pole,
                                                spec.iterations, spec.tolerance);
        result.samples.push_back(solved.joints);
        result.targetReachable.push_back(solved.targetReachable);
        for (std::size_t joint = 0; joint < jointCount; ++joint) {
            result.jointPositions[joint].add(seconds, solved.joints[joint]);
        }
    }
    return result;
}

} // namespace chrononmotion::motion
