#include "chrononmotion/motion/CameraTrajectory.hpp"

#include "chrononmotion/math/Finite.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace chrononmotion::motion::camera {
namespace {

constexpr unsigned kArcSamples = 64;

Vector3 catmull(const Vector3& p0, const Vector3& p1,
                const Vector3& p2, const Vector3& p3, float t) {
    const float t2 = t * t, t3 = t2 * t;
    Vector3 out = p1.clone().multiplyScalar(2.f);
    out.addScaledVector(p2.clone().sub(p0), t);
    out.addScaledVector(p0.clone().multiplyScalar(2.f)
        .addScaledVector(p1, -5.f).addScaledVector(p2, 4.f).sub(p3), t2);
    out.addScaledVector(p0.clone().multiplyScalar(-1.f)
        .addScaledVector(p1, 3.f).addScaledVector(p2, -3.f).add(p3), t3);
    return out.multiplyScalar(0.5f);
}

Vector3 catmullDerivative(const Vector3& p0, const Vector3& p1,
                          const Vector3& p2, const Vector3& p3, float t) {
    const float t2 = t * t;
    Vector3 out = p2.clone().sub(p0);
    out.addScaledVector(p0.clone().multiplyScalar(2.f)
        .addScaledVector(p1, -5.f).addScaledVector(p2, 4.f).sub(p3), 2.f * t);
    out.addScaledVector(p0.clone().multiplyScalar(-1.f)
        .addScaledVector(p1, 3.f).addScaledVector(p2, -3.f).add(p3), 3.f * t2);
    return out.multiplyScalar(0.5f);
}

Vector3 bezier(const Vector3& p0, const Vector3& p1,
               const Vector3& p2, const Vector3& p3, float t) {
    const float u = 1.f - t;
    return p0.clone().multiplyScalar(u * u * u)
        .addScaledVector(p1, 3.f * u * u * t)
        .addScaledVector(p2, 3.f * u * t * t)
        .addScaledVector(p3, t * t * t);
}

Vector3 bezierDerivative(const Vector3& p0, const Vector3& p1,
                         const Vector3& p2, const Vector3& p3, float t) {
    const float u = 1.f - t;
    Vector3 out = p1.clone().sub(p0).multiplyScalar(3.f * u * u);
    out.addScaledVector(p2.clone().sub(p1), 6.f * u * t);
    out.addScaledVector(p3.clone().sub(p2), 3.f * t * t);
    return out;
}

Vector3 catmullNeighbor(const std::vector<CameraTrajectoryPoint>& points,
                        unsigned from, unsigned to, bool previous) {
    if (previous) {
        if (from > 0) return points[from - 1].position;
        return points[from].position.clone().multiplyScalar(2.f).sub(points[to].position);
    }
    if (to + 1 < points.size()) return points[to + 1].position;
    return points[to].position.clone().multiplyScalar(2.f).sub(points[from].position);
}

} // namespace

CameraTrajectorySample CameraTrajectory::sample(float frame) const {
    if (points_.empty()) return {};
    if (segments_.empty()) {
        const auto& p = points_.front();
        return {p.position, {}, p.target, p.rollDegrees};
    }
    if (!std::isfinite(frame)) throw std::invalid_argument("CameraTrajectory::sample: frame must be finite");
    const float clamped = std::clamp(frame, 0.f, totalFrames());
    float cursor = 0.f;
    for (std::size_t i = 0; i < segments_.size(); ++i) {
        const auto& segment = segments_[i];
        const float end = cursor + segment.durationFrames;
        if (clamped <= end || i + 1 == segments_.size()) {
            const float fraction = std::clamp((clamped - cursor) /
                std::max(segment.durationFrames, 1.f), 0.f, 1.f);
            float t = fraction;
            if (i < arcTables_.size() && !arcTables_[i].empty()) {
                const auto& table = arcTables_[i];
                const float distance = fraction * table.back().distance;
                const auto it = std::lower_bound(table.begin(), table.end(), distance,
                    [](const ArcEntry& entry, float d) { return entry.distance < d; });
                if (it == table.begin()) t = it->parameter;
                else if (it == table.end()) t = table.back().parameter;
                else {
                    const auto& b = *it;
                    const auto& a = *(it - 1);
                    const float span = b.distance - a.distance;
                    const float alpha = span > 1e-8f ? (distance - a.distance) / span : 0.f;
                    t = a.parameter + (b.parameter - a.parameter) * alpha;
                }
            }
            const auto& p0 = points_[segment.fromIndex];
            const auto& p1 = points_[segment.toIndex];
            Vector3 position, tangent;
            if (segment.kind == TrajectorySegmentKind::Hold) {
                position = p0.position;
            } else if (segment.kind == TrajectorySegmentKind::Linear) {
                position = p0.position.clone().lerp(p1.position, t);
                tangent = p1.position.clone().sub(p0.position);
            } else if (segment.kind == TrajectorySegmentKind::CubicBezier) {
                const Vector3 c1 = p0.position.clone().add(p0.handleOut);
                const Vector3 c2 = p1.position.clone().add(p1.handleIn);
                position = bezier(p0.position, c1, c2, p1.position, t);
                tangent = bezierDerivative(p0.position, c1, c2, p1.position, t);
            } else {
                const Vector3 prev = catmullNeighbor(points_, segment.fromIndex, segment.toIndex, true);
                const Vector3 next = catmullNeighbor(points_, segment.fromIndex, segment.toIndex, false);
                position = catmull(prev, p0.position, p1.position, next, t);
                tangent = catmullDerivative(prev, p0.position, p1.position, next, t);
            }
            if (tangent.lengthSq() > 1e-12f) tangent.normalize();
            const auto target = p1.target ? p1.target : p0.target;
            const float roll = p0.rollDegrees + (p1.rollDegrees - p0.rollDegrees) * t;
            return {position, tangent, target, roll};
        }
        cursor = end;
    }
    const auto& p = points_.back();
    return {p.position, {}, p.target, p.rollDegrees};
}

float CameraTrajectory::totalFrames() const {
    float total = 0.f;
    for (const auto& segment : segments_) total += segment.durationFrames;
    return total;
}

CameraTrajectoryBuilder& CameraTrajectoryBuilder::moveTo(Vector3 position,
                                                          std::optional<Vector3> target,
                                                          float rollDegrees) {
    points_.push_back({position, {}, {}, target, rollDegrees});
    if (points_.size() >= 2)
        segments_.push_back({TrajectorySegmentKind::Linear,
            static_cast<unsigned>(points_.size() - 2), static_cast<unsigned>(points_.size() - 1), 30.f});
    return *this;
}

CameraTrajectoryBuilder& CameraTrajectoryBuilder::bezierTo(Vector3 handleIn,
                                                             Vector3 handleOut, Vector3 to) {
    if (points_.empty()) { points_.push_back({to}); return *this; }
    points_.push_back({to, handleIn, handleOut});
    segments_.push_back({TrajectorySegmentKind::CubicBezier,
        static_cast<unsigned>(points_.size() - 2), static_cast<unsigned>(points_.size() - 1), 30.f});
    return *this;
}

CameraTrajectoryBuilder& CameraTrajectoryBuilder::catmullRomTo(Vector3 to) {
    if (points_.empty()) { points_.push_back({to}); return *this; }
    points_.push_back({to});
    segments_.push_back({TrajectorySegmentKind::CatmullRom,
        static_cast<unsigned>(points_.size() - 2), static_cast<unsigned>(points_.size() - 1), 30.f});
    return *this;
}

CameraTrajectoryBuilder& CameraTrajectoryBuilder::holdFor(float frames) {
    if (!points_.empty())
        segments_.push_back({TrajectorySegmentKind::Hold,
            static_cast<unsigned>(points_.size() - 1), static_cast<unsigned>(points_.size() - 1),
            std::max(0.f, frames)});
    return *this;
}

CameraTrajectoryBuilder& CameraTrajectoryBuilder::durationFrames(float frames) {
    if (!segments_.empty()) segments_.back().durationFrames = std::max(1.f, frames);
    return *this;
}

CameraTrajectoryBuilder& CameraTrajectoryBuilder::arcLengthParameterized(bool enabled) {
    arcLength_ = enabled;
    return *this;
}

std::shared_ptr<CameraTrajectory> CameraTrajectoryBuilder::build() {
    if (points_.empty()) throw std::invalid_argument("CameraTrajectoryBuilder: at least one point is required");
    for (const auto& point : points_)
        if (!math::isFinite(point.position) || !math::isFinite(point.handleIn) || !math::isFinite(point.handleOut) ||
            (point.target && !math::isFinite(*point.target)) || !std::isfinite(point.rollDegrees))
            throw std::invalid_argument("CameraTrajectoryBuilder: point values must be finite");
    for (const auto& segment : segments_)
        if (!std::isfinite(segment.durationFrames) || segment.durationFrames < 0.f ||
            segment.fromIndex >= points_.size() || segment.toIndex >= points_.size())
            throw std::invalid_argument("CameraTrajectoryBuilder: invalid segment");

    auto result = std::make_shared<CameraTrajectory>();
    result->points_ = std::move(points_);
    result->segments_ = std::move(segments_);
    if (arcLength_) {
        result->arcTables_.resize(result->segments_.size());
        for (std::size_t i = 0; i < result->segments_.size(); ++i) {
            auto& table = result->arcTables_[i];
            table.reserve(kArcSamples);
            const auto& segment = result->segments_[i];
            const auto& p0 = result->points_[segment.fromIndex];
            const auto& p1 = result->points_[segment.toIndex];
            Vector3 previous{};
            float length = 0.f;
            for (unsigned j = 0; j < kArcSamples; ++j) {
                const float t = static_cast<float>(j) / static_cast<float>(kArcSamples - 1);
                Vector3 position;
                if (segment.kind == TrajectorySegmentKind::Hold) position = p0.position;
                else if (segment.kind == TrajectorySegmentKind::Linear) position = p0.position.clone().lerp(p1.position, t);
                else if (segment.kind == TrajectorySegmentKind::CubicBezier) {
                    position = bezier(p0.position, p0.position.clone().add(p0.handleOut),
                        p1.position.clone().add(p1.handleIn), p1.position, t);
                } else {
                    position = catmull(catmullNeighbor(result->points_, segment.fromIndex, segment.toIndex, true),
                        p0.position, p1.position,
                        catmullNeighbor(result->points_, segment.fromIndex, segment.toIndex, false), t);
                }
                if (j) length += position.distanceTo(previous);
                table.push_back({length, t});
                previous = position;
            }
        }
    }
    return result;
}

void authorCameraTrajectory(CameraRig& rig, const CameraTrajectory& trajectory,
                            int startFrame, float fps, unsigned samplesPerFrame,
                            CameraTrajectoryAuthoringOptions options) {
    if (startFrame < 0 || !(fps > 0.f) || !std::isfinite(fps) || samplesPerFrame == 0)
        throw std::invalid_argument("authorCameraTrajectory: invalid timing or sampling density");
    const float duration = trajectory.totalFrames();
    const double sampleCountValue = std::ceil(static_cast<double>(duration) * samplesPerFrame);
    if (!std::isfinite(duration) || duration < 0.f || !std::isfinite(sampleCountValue) ||
        sampleCountValue > 1000000.0)
        throw std::invalid_argument("authorCameraTrajectory: sample count exceeds one million keys");
    const auto sampleCount = static_cast<unsigned long long>(sampleCountValue);
    bool hasTargetChannel = rig.hasTarget();
    for (const auto& point : trajectory.points()) hasTargetChannel |= point.target.has_value();
    Vector3 fallbackTarget = rig.target();
    if (!rig.hasTarget()) {
        const auto firstTarget = std::find_if(trajectory.points().begin(), trajectory.points().end(),
            [](const CameraTrajectoryPoint& point) { return point.target.has_value(); });
        if (firstTarget != trajectory.points().end()) fallbackTarget = *firstTarget->target;
    }
    Vector3 lastTangent{};
    Vector3 finalTarget = fallbackTarget;
    float finalRoll = 0.f;
    for (unsigned long long i = 0; i <= sampleCount; ++i) {
        const float pathFrame = std::min(duration,
            static_cast<float>(i) / static_cast<float>(samplesPerFrame));
        const float time = (static_cast<float>(startFrame) + pathFrame) / fps;
        const auto pose = trajectory.sample(pathFrame);
        rig.positionTrack().add(time, pose.position, Easing::linear());
        if (options.orientAlongPath) {
            Vector3 direction = pose.tangent;
            if (direction.lengthSq() > 1e-12f) {
                direction.normalize();
                lastTangent = direction;
            } else if (lastTangent.lengthSq() > 1e-12f) {
                direction = lastTangent;
            }
            finalTarget = direction.lengthSq() > 1e-12f
                ? Vector3{}.addVectors(pose.position, direction)
                : pose.target.value_or(fallbackTarget);
            rig.targetTrack().add(time, finalTarget, Easing::linear());
            finalRoll = options.keepHorizon ? 0.f :
                pose.rollDegrees * 3.14159265358979323846f / 180.f;
            rig.rollTrack().add(time, finalRoll, Easing::linear());
        } else {
            finalTarget = pose.target.value_or(fallbackTarget);
            finalRoll = pose.rollDegrees * 3.14159265358979323846f / 180.f;
            rig.rollTrack().add(time, finalRoll, Easing::linear());
            if (hasTargetChannel)
                rig.targetTrack().add(time, finalTarget, Easing::linear());
        }
    }
    const auto finalPose = trajectory.sample(duration);
    rig.setPosition(finalPose.position).setRoll(finalRoll);
    if (hasTargetChannel || options.orientAlongPath) rig.setTarget(finalTarget);
}

} // namespace chrononmotion::motion::camera
