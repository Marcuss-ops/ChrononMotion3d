#include "chrononmotion/motion/OfflineBaking.hpp"

#include "chrononmotion/math/MathUtils.hpp"
#include "chrononmotion/motion/Layer.hpp"
#include "chrononmotion/motion/MotionScene.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_set>

namespace chrononmotion::motion {
namespace {

constexpr float kEpsilon = 1e-6f;
constexpr std::size_t kMaximumBakedFrames = 1'000'001;
constexpr std::size_t kMaximumBakedWork = 100'000'000;
constexpr std::size_t kMaximumBakedPathCommands = 1'000'000;

bool finite(const Vector2& value) {
    return std::isfinite(value.x) && std::isfinite(value.y);
}

void validateFrameRange(int firstFrame, int lastFrame) {
    if (firstFrame < 0 || lastFrame < firstFrame ||
        static_cast<std::size_t>(lastFrame - firstFrame) + 1 > kMaximumBakedFrames) {
        throw std::invalid_argument("offline bake requires 0 <= firstFrame <= lastFrame <= firstFrame + 1000000");
    }
}

void validateFrameTimes(float fps, int firstFrame, int lastFrame, const char* function) {
    const float firstTime = static_cast<float>(firstFrame) / fps;
    const float lastTime = static_cast<float>(lastFrame) / fps;
    const float previousLastTime = lastFrame > firstFrame
        ? static_cast<float>(lastFrame - 1) / fps : lastTime;
    if (!std::isfinite(firstTime) || !std::isfinite(lastTime) ||
        (firstFrame < lastFrame &&
         (!(static_cast<float>(firstFrame + 1) / fps > firstTime) || !(lastTime > previousLastTime))))
        throw std::invalid_argument(std::string(function) + ": frame times are not representable as strictly increasing finite floats");
}

float wrapAngle(float angle) {
    return std::remainder(angle, math::TWO_PI);
}

void addVectorKey(Track<Vector3>& track, int frame, float fps, const Vector2& value) {
    track.add(static_cast<float>(frame) / fps, Vector3(value.x, value.y, 0.f));
}

Vector2 transformed(const Vector2& point, const Vector2& anchor,
                    const Vector2& scale, float angle, const Vector2& translation) {
    const float x = (point.x - anchor.x) * scale.x;
    const float y = (point.y - anchor.y) * scale.y;
    const float c = std::cos(angle);
    const float s = std::sin(angle);
    return Vector2(anchor.x + x * c - y * s + translation.x,
                   anchor.y + x * s + y * c + translation.y);
}

class SvgParser {
public:
    explicit SvgParser(std::string_view input) : input_(input) {}

    BakedPath parse() {
        if (input_.size() > 8U * 1024U * 1024U)
            fail("path data exceeds the 8 MiB input limit");
        BakedPath output;
        Vector2 current{};
        Vector2 subpathStart{};
        Vector2 previousCubicControl{};
        Vector2 previousQuadraticControl{};
        std::size_t commandCount = 0;
        char command = 0;
        char previousCommand = 0;
        while (skipSeparators(), position_ < input_.size()) {
            if (isCommand(input_[position_])) {
                command = input_[position_++];
                commaMayFollowNumber_ = false;
            } else if (command == 0) fail("expected an SVG path command");
            const bool relative = command >= 'a' && command <= 'z';
            const char upper = static_cast<char>(relative ? command - 'a' + 'A' : command);
            switch (upper) {
                case 'M': {
                    Vector2 point = readPoint(relative, current);
                    if (++commandCount > 65536) fail("path exceeds the 65536 command limit");
                    output.push_back({BakedPathCommandType::MoveTo, point, {}, {}});
                    current = subpathStart = point;
                    // Subsequent coordinate pairs after M are implicit L.
                    command = relative ? 'l' : 'L';
                    previousCommand = 'M';
                    break;
                }
                case 'L': {
                    Vector2 point = readPoint(relative, current);
                    line(output, current, point);
                    current = point;
                    previousCommand = 'L';
                    break;
                }
                case 'H': {
                    const float x = readNumber();
                    Vector2 point(relative ? current.x + x : x, current.y);
                    line(output, current, point);
                    current = point;
                    previousCommand = 'H';
                    break;
                }
                case 'V': {
                    const float y = readNumber();
                    Vector2 point(current.x, relative ? current.y + y : y);
                    line(output, current, point);
                    current = point;
                    previousCommand = 'V';
                    break;
                }
                case 'C': {
                    if (output.size() + 1 > 65536) fail("path exceeds the 65536 command limit");
                    Vector2 c1 = readPoint(relative, current);
                    Vector2 c2 = readPoint(relative, current);
                    Vector2 point = readPoint(relative, current);
                    output.push_back({BakedPathCommandType::CubicTo, point, c1, c2});
                    current = point;
                    previousCubicControl = c2;
                    previousCommand = 'C';
                    break;
                }
                case 'S': {
                    if (output.size() + 1 > 65536) fail("path exceeds the 65536 command limit");
                    Vector2 c1 = (previousCommand == 'C' || previousCommand == 'S')
                        ? current * 2.f - previousCubicControl : current;
                    Vector2 c2 = readPoint(relative, current);
                    Vector2 point = readPoint(relative, current);
                    output.push_back({BakedPathCommandType::CubicTo, point, c1, c2});
                    current = point;
                    previousCubicControl = c2;
                    previousCommand = 'S';
                    break;
                }
                case 'Q': {
                    if (output.size() + 1 > 65536) fail("path exceeds the 65536 command limit");
                    Vector2 control = readPoint(relative, current);
                    Vector2 point = readPoint(relative, current);
                    quadratic(output, current, control, point);
                    current = point;
                    previousQuadraticControl = control;
                    previousCommand = 'Q';
                    break;
                }
                case 'T': {
                    if (output.size() + 1 > 65536) fail("path exceeds the 65536 command limit");
                    Vector2 control = (previousCommand == 'Q' || previousCommand == 'T')
                        ? current * 2.f - previousQuadraticControl : current;
                    Vector2 point = readPoint(relative, current);
                    quadratic(output, current, control, point);
                    current = point;
                    previousQuadraticControl = control;
                    previousCommand = 'T';
                    break;
                }
                case 'A': {
                    const float rx = std::abs(readNumber());
                    const float ry = std::abs(readNumber());
                    const float rotation = math::degToRad(readNumber());
                    const bool largeArc = readFlag();
                    const bool sweep = readFlag();
                    Vector2 point = readPoint(relative, current);
                    arc(output, current, point, rx, ry, rotation, largeArc, sweep);
                    current = point;
                    previousCommand = 'A';
                    break;
                }
                case 'Z':
                    if (++commandCount > 65536) fail("path exceeds the 65536 command limit");
                    output.push_back({BakedPathCommandType::Close, {}, {}, {}});
                    current = subpathStart;
                    previousCommand = 'Z';
                    command = 0;
                    break;
                default: fail("unsupported SVG path command");
            }
            if (position_ < input_.size() && !isCommand(input_[position_]) &&
                (position_ == 0 || (input_[position_ - 1] == ','))) {
                fail("malformed SVG path number sequence");
            }
        }
        if (output.empty() || output.front().type != BakedPathCommandType::MoveTo)
            fail("SVG path must begin with moveto");
        for (const auto& value : output) {
            if (value.type != BakedPathCommandType::Close && !finite(value.point))
                fail("parsed coordinates overflow finite range");
            if (value.type == BakedPathCommandType::CubicTo &&
                (!finite(value.control1) || !finite(value.control2)))
                fail("parsed controls overflow finite range");
        }
        return output;
    }

private:
    [[noreturn]] void fail(const char* message) const {
        throw std::invalid_argument(std::string("SVG path: ") + message + " at byte " + std::to_string(position_));
    }
    static bool isCommand(char c) {
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
    }
    void skipSeparators() {
        bool commaSeen = false;
        while (position_ < input_.size()) {
            const char current = input_[position_];
            if (current == ',' ) {
                if (commaSeen || !commaMayFollowNumber_) fail("comma must follow a numeric argument");
                commaSeen = true;
                commaMayFollowNumber_ = false;
                ++position_;
            } else if (current == ' ' || current == '\n' || current == '\r' || current == '\t') {
                ++position_;
            } else {
                break;
            }
        }
        if (commaSeen && (position_ == input_.size() || isCommand(input_[position_])))
            fail("comma must separate numeric arguments");
    }
    bool readFlag() {
        skipSeparators();
        if (position_ >= input_.size() || (input_[position_] != '0' && input_[position_] != '1'))
            fail("arc flags must be zero or one");
        commaMayFollowNumber_ = true;
        return input_[position_++] == '1';
    }
    float readNumber() {
        skipSeparators();
        if (position_ >= input_.size() || isCommand(input_[position_])) fail("expected a number");
        const std::size_t start = position_;
        if (input_[position_] == '+' || input_[position_] == '-') ++position_;
        std::size_t digits = 0;
        while (position_ < input_.size() && input_[position_] >= '0' && input_[position_] <= '9') {
            ++position_;
            ++digits;
        }
        if (position_ < input_.size() && input_[position_] == '.') {
            ++position_;
            while (position_ < input_.size() && input_[position_] >= '0' && input_[position_] <= '9') {
                ++position_;
                ++digits;
            }
        }
        if (digits == 0) fail("expected a number");
        if (position_ < input_.size() && (input_[position_] == 'e' || input_[position_] == 'E')) {
            ++position_;
            if (position_ < input_.size() && (input_[position_] == '+' || input_[position_] == '-')) ++position_;
            const std::size_t exponentStart = position_;
            while (position_ < input_.size() && input_[position_] >= '0' && input_[position_] <= '9') ++position_;
            if (position_ == exponentStart) fail("malformed numeric exponent");
        }
        const std::string token(input_.substr(start, position_ - start));
        char* end = nullptr;
        const float value = std::strtof(token.c_str(), &end);
        if (end != token.c_str() + token.size() || !std::isfinite(value))
            fail("expected a finite number");
        commaMayFollowNumber_ = true;
        return value;
    }
    Vector2 readPoint(bool relative, const Vector2& origin) {
        const float x = readNumber();
        const float y = readNumber();
        return relative ? Vector2(origin.x + x, origin.y + y) : Vector2(x, y);
    }
    static void line(BakedPath& path, const Vector2& from, const Vector2& to) {
        if (path.size() >= 65536) throw std::invalid_argument("SVG path exceeds the 65536 command limit");
        const double dx = static_cast<double>(to.x) - from.x;
        const double dy = static_cast<double>(to.y) - from.y;
        const Vector2 c1(static_cast<float>(from.x + dx / 3.0), static_cast<float>(from.y + dy / 3.0));
        const Vector2 c2(static_cast<float>(from.x + dx * (2.0 / 3.0)),
                         static_cast<float>(from.y + dy * (2.0 / 3.0)));
        if (!finite(c1) || !finite(c2))
            throw std::invalid_argument("SVG path: line conversion overflowed finite range");
        path.push_back({BakedPathCommandType::CubicTo, to, c1, c2});
        // The command stores controls in the established (point, c1, c2) form.
        path.back().point = to;
    }
    static void quadratic(BakedPath& path, const Vector2& from,
                          const Vector2& control, const Vector2& to) {
        if (path.size() >= 65536) throw std::invalid_argument("SVG path exceeds the 65536 command limit");
        const Vector2 c1(static_cast<float>((static_cast<double>(from.x) + 2.0 * control.x) / 3.0),
                         static_cast<float>((static_cast<double>(from.y) + 2.0 * control.y) / 3.0));
        const Vector2 c2(static_cast<float>((2.0 * control.x + to.x) / 3.0),
                         static_cast<float>((2.0 * control.y + to.y) / 3.0));
        if (!finite(c1) || !finite(c2))
            throw std::invalid_argument("SVG path: quadratic conversion overflowed finite range");
        path.push_back({BakedPathCommandType::CubicTo, to, c1, c2});
    }
    static void arc(BakedPath& path, const Vector2& from, const Vector2& to,
                    float rx, float ry, float phi, bool largeArc, bool sweep) {
        if (from == to) return;
        if (rx < kEpsilon || ry < kEpsilon) {
            line(path, from, to);
            return;
        }
        if (!std::isfinite(phi))
            throw std::invalid_argument("SVG path: arc rotation overflowed finite range");
        const double cp = std::cos(static_cast<double>(phi));
        const double sp = std::sin(static_cast<double>(phi));
        const double dx = (static_cast<double>(from.x) - to.x) * 0.5;
        const double dy = (static_cast<double>(from.y) - to.y) * 0.5;
        const double xp = cp * dx + sp * dy;
        const double yp = -sp * dx + cp * dy;
        double rx2 = static_cast<double>(rx) * rx;
        double ry2 = static_cast<double>(ry) * ry;
        double rxValue = rx, ryValue = ry;
        double lambda = xp * xp / rx2 + yp * yp / ry2;
        if (!std::isfinite(lambda))
            throw std::invalid_argument("SVG path: arc radii overflowed conversion range");
        if (lambda > 1.0) {
            const double scale = std::sqrt(lambda);
            rxValue *= scale;
            ryValue *= scale;
            rx2 = rxValue * rxValue;
            ry2 = ryValue * ryValue;
        }
        const double numerator = std::max(0.0, rx2 * ry2 - rx2 * yp * yp - ry2 * xp * xp);
        const double denominator = rx2 * yp * yp + ry2 * xp * xp;
        const double sign = largeArc == sweep ? -1.0 : 1.0;
        const double factor = denominator > 0.0 ? sign * std::sqrt(numerator / denominator) : 0.0;
        const double cxp = factor * rxValue * yp / ryValue;
        const double cyp = factor * -ryValue * xp / rxValue;
        const double cx = cp * cxp - sp * cyp + (static_cast<double>(from.x) + to.x) * 0.5;
        const double cy = sp * cxp + cp * cyp + (static_cast<double>(from.y) + to.y) * 0.5;
        const auto angle = [](double ux, double uy, double vx, double vy) {
            return std::atan2(ux * vy - uy * vx, ux * vx + uy * vy);
        };
        const double ux = (xp - cxp) / rxValue, uy = (yp - cyp) / ryValue;
        const double vx = (-xp - cxp) / rxValue, vy = (-yp - cyp) / ryValue;
        double start = std::atan2(uy, ux);
        double delta = angle(ux, uy, vx, vy);
        if (!sweep && delta > 0.f) delta -= math::TWO_PI;
        if (sweep && delta < 0.f) delta += math::TWO_PI;
        if (!std::isfinite(start) || !std::isfinite(delta) || !std::isfinite(cx) || !std::isfinite(cy) ||
            !std::isfinite(rxValue) || !std::isfinite(ryValue) || rxValue > std::numeric_limits<float>::max() ||
            ryValue > std::numeric_limits<float>::max())
            throw std::invalid_argument("SVG path: arc computation overflowed finite range");
        const unsigned segments = std::max(1u, static_cast<unsigned>(std::ceil(std::abs(delta) / (math::PI * 0.5))));
        const double step = delta / static_cast<double>(segments);
        if (path.size() + segments > 65536)
            throw std::invalid_argument("SVG path exceeds the 65536 command limit");
        for (unsigned i = 0; i < segments; ++i) {
            const double end = start + step;
            const double alpha = 4.0 / 3.0 * std::tan((end - start) * 0.25);
            const auto map = [&](double x, double y) {
                return Vector2(static_cast<float>(cx + rxValue * cp * x - ryValue * sp * y),
                               static_cast<float>(cy + rxValue * sp * x + ryValue * cp * y));
            };
            const Vector2 p3 = map(std::cos(end), std::sin(end));
            const Vector2 c1 = map(std::cos(start) - alpha * std::sin(start),
                                   std::sin(start) + alpha * std::cos(start));
            const Vector2 c2 = map(std::cos(end) + alpha * std::sin(end),
                                   std::sin(end) - alpha * std::cos(end));
            path.push_back({BakedPathCommandType::CubicTo, p3, c1, c2});
            start = end;
        }
        path.back().point = to; // Preserve the exact authored endpoint.
    }

    std::string_view input_;
    std::size_t position_{0};
    bool commaMayFollowNumber_{false};
};

void validateBakedPath(const BakedPath& path, const char* function) {
    if (path.empty() || path.size() > 65536 || path.front().type != BakedPathCommandType::MoveTo)
        throw std::invalid_argument(std::string(function) + ": path must begin with moveto and contain 1..65536 commands");
    bool hasCurrentSubpath = false;
    for (const auto& command : path) {
        if (static_cast<unsigned>(command.type) > static_cast<unsigned>(BakedPathCommandType::Close))
            throw std::invalid_argument(std::string(function) + ": path contains an unsupported command type");
        if (command.type == BakedPathCommandType::Close) {
            if (!hasCurrentSubpath)
                throw std::invalid_argument(std::string(function) + ": close command has no preceding subpath");
            continue;
        }
        if (command.type == BakedPathCommandType::MoveTo) hasCurrentSubpath = true;
        if (!finite(command.point))
            throw std::invalid_argument(std::string(function) + ": path coordinates must be finite");
        if (command.type == BakedPathCommandType::CubicTo &&
            (!finite(command.control1) || !finite(command.control2)))
            throw std::invalid_argument(std::string(function) + ": cubic controls must be finite");
    }
}

void validatePathPair(const BakedPath& a, const BakedPath& b) {
    validateBakedPath(a, "path morph");
    validateBakedPath(b, "path morph");
    if (a.size() != b.size())
        throw std::invalid_argument("path morph requires identical non-empty command topology");
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (a[i].type != b[i].type)
            throw std::invalid_argument("path morph requires matching command types");
    }
}

} // namespace

BakedVerletChain bakeVerletChain(const VerletChainSpec& spec, int firstFrame, int lastFrame) {
    validateFrameRange(firstFrame, lastFrame);
    if (spec.joints.size() < 2 || spec.joints.size() > 65536 || !std::isfinite(spec.fps) || spec.fps <= 0.f ||
        !std::isfinite(spec.damping) || spec.damping < 0.f || spec.damping >= 1.f ||
        !finite(spec.gravity) || spec.iterations == 0 || spec.iterations > 256)
        throw std::invalid_argument("bakeVerletChain: invalid chain or simulation parameters");
    const std::size_t requestedFrames = static_cast<std::size_t>(lastFrame - firstFrame) + 1;
    if (spec.joints.size() > kMaximumBakedFrames / requestedFrames ||
        spec.iterations > kMaximumBakedWork / requestedFrames / spec.joints.size())
        throw std::invalid_argument("bakeVerletChain: requested samples or solver work exceed the offline budget");
    validateFrameTimes(spec.fps, firstFrame, lastFrame, "bakeVerletChain");
    for (const auto& point : spec.joints)
        if (!finite(point)) throw std::invalid_argument("bakeVerletChain: joints must be finite");
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
    if (!std::isfinite(dt) || !std::isfinite(dt * dt))
        throw std::invalid_argument("bakeVerletChain: fps is too small for a finite fixed step");
    for (std::size_t frame = 0; frame < frames; ++frame) {
        result.samples.push_back(current);
        const int frameNumber = firstFrame + static_cast<int>(frame);
        for (std::size_t joint = 0; joint < current.size(); ++joint) {
            if (!finite(current[joint]))
                throw std::invalid_argument("bakeVerletChain: simulation state overflowed finite range");
            addVectorKey(result.jointPositions[joint], frameNumber, spec.fps, current[joint]);
        }
        for (std::size_t joint = 1; joint < current.size(); ++joint) {
            const        Vector2 position = current[joint];
            Vector2 velocity = (current[joint] - previous[joint]) * (1.f - spec.damping);
            current[joint] = current[joint] + velocity + spec.gravity * (dt * dt);
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
        for (const Vector2& point : current)
            if (!finite(point))
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
    if (!std::isfinite(world.fps) || world.fps <= 0.f || !finite(world.gravity) ||
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
        if (!ids.insert(body.id).second || !finite(body.position) || !finite(body.velocity) ||
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
            if (!finite(body.position) || !finite(body.velocity))
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
                if (!finite(body.position) || !finite(body.velocity))
                    throw std::invalid_argument("bakeRigidBodies2D: simulation state overflowed finite range");
            }
        }
    }
    return result;
}

IK2Result solveTwoSegmentIK2D(const Vector2& root, const Vector2& target,
                               float upperLength, float lowerLength,
                               float parentRotation, bool bendPositive) {
    if (!finite(root) || !finite(target) || !std::isfinite(upperLength) ||
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
    if (!finite(result.elbow) || !finite(result.end) || !std::isfinite(result.shoulderRotation) ||
        !std::isfinite(result.elbowRotation))
        throw std::invalid_argument("solveTwoSegmentIK2D: solved pose exceeds finite float range");
    return result;
}

float AudioFeatureSample::value(AudioFeature feature) const noexcept {
    switch (feature) {
        case AudioFeature::Rms: return rms;
        case AudioFeature::Bass: return bass;
        case AudioFeature::Mid: return mid;
        case AudioFeature::Treble: return treble;
        case AudioFeature::Onset: return onset;
    }
    return 0.f;
}

std::vector<AudioFeatureSample> analyzePcmAudio(std::span<const float> pcm,
                                                const AudioAnalysisSpec& spec) {
    if (spec.sampleRate == 0 || spec.channels == 0 || spec.windowSize < 8 ||
        spec.windowSize > 8192 || spec.hopSize == 0 || spec.hopSize > spec.windowSize ||
        !std::isfinite(spec.bassBelowHz) || !std::isfinite(spec.midBelowHz) ||
        spec.bassBelowHz <= 0.f || spec.midBelowHz <= spec.bassBelowHz ||
        static_cast<double>(spec.midBelowHz) >= static_cast<double>(spec.sampleRate) * 0.5 ||
        spec.channels > 64 ||
        !std::isfinite(spec.onsetThreshold) || spec.onsetThreshold < 0.f ||
        !std::isfinite(spec.rmsThreshold) || spec.rmsThreshold < 0.f || spec.rmsThreshold > 1.f ||
        pcm.size() % spec.channels != 0)
        throw std::invalid_argument("analyzePcmAudio: invalid PCM format or analysis parameters");
    for (float sample : pcm)
        if (!std::isfinite(sample) || sample < -1.f || sample > 1.f)
            throw std::invalid_argument("analyzePcmAudio: normalized PCM samples must be finite and in [-1, 1]");
    const std::size_t frameCount = pcm.size() / spec.channels;
    if (frameCount == 0) return {};
    const std::size_t windowCount = 1 + (frameCount - 1) / spec.hopSize;
    if (windowCount > kMaximumBakedFrames)
        throw std::invalid_argument("analyzePcmAudio: PCM exceeds the offline window limit");
    const std::size_t windowCost = spec.windowSize * spec.windowSize;
    const std::size_t dftBinCount = spec.windowSize / 2;
    if (windowCost == 0 || windowCount > kMaximumBakedWork / windowCost ||
        (dftBinCount != 0 && windowCount > kMaximumBakedWork / spec.windowSize / dftBinCount))
        throw std::invalid_argument("analyzePcmAudio: direct DFT exceeds the deterministic offline operation budget");
    std::vector<AudioFeatureSample> output;
    output.reserve(windowCount);
    float previousRms = 0.f;
    for (std::size_t start = 0; start < frameCount; start += spec.hopSize) {
        double squareSum = 0.0;
        const std::size_t count = std::min(spec.windowSize, frameCount - start);
        std::vector<float> mono(count);
        for (std::size_t i = 0; i < count; ++i) {
            double value = 0.0;
            for (std::size_t channel = 0; channel < spec.channels; ++channel)
                value += pcm[(start + i) * spec.channels + channel];
            mono[i] = static_cast<float>(value / spec.channels);
            squareSum += static_cast<double>(mono[i]) * mono[i];
        }
        const float rms = static_cast<float>(std::sqrt(squareSum / count));
        double bandPower[3]{0.0, 0.0, 0.0};
        std::size_t bandBins[3]{0, 0, 0};
        for (std::size_t bin = 1; bin <= count / 2; ++bin) {
            const float frequency = static_cast<float>(bin) * static_cast<float>(spec.sampleRate) /
                                    static_cast<float>(count);
            unsigned band = frequency < spec.bassBelowHz ? 0u : frequency < spec.midBelowHz ? 1u : 2u;
            double real = 0.0, imaginary = 0.0;
            for (std::size_t sample = 0; sample < count; ++sample) {
                const float window = count > 1 ? 0.5f - 0.5f * std::cos(math::TWO_PI *
                    static_cast<float>(sample) / static_cast<float>(count - 1)) : 1.f;
                const double phase = math::TWO_PI * static_cast<double>(bin * sample) / static_cast<double>(count);
                const double weighted = static_cast<double>(mono[sample] * window);
                real += weighted * std::cos(phase);
                imaginary -= weighted * std::sin(phase);
            }
            bandPower[band] += real * real + imaginary * imaginary;
            ++bandBins[band];
        }
        AudioFeatureSample feature;
        feature.seconds = static_cast<float>(start) / static_cast<float>(spec.sampleRate);
        feature.rms = rms;
        float* bands[] = {&feature.bass, &feature.mid, &feature.treble};
        for (unsigned band = 0; band < 3; ++band) {
            const double denominator = static_cast<double>(count) * static_cast<double>(count) *
                                       static_cast<double>(std::max<std::size_t>(1, bandBins[band]));
            *bands[band] = static_cast<float>(std::sqrt(bandPower[band] / denominator) * 2.0);
        }
        const float rise = rms / std::max(previousRms, 1e-5f);
        feature.onset = previousRms <= spec.rmsThreshold && rms > spec.rmsThreshold &&
                        rise >= spec.onsetThreshold ? rms - previousRms : 0.f;
        if (!std::isfinite(feature.seconds) || !std::isfinite(feature.rms) ||
            !std::isfinite(feature.bass) || !std::isfinite(feature.mid) ||
            !std::isfinite(feature.treble) || !std::isfinite(feature.onset))
            throw std::invalid_argument("analyzePcmAudio: analysis overflowed finite output range");
        output.push_back(feature);
        previousRms = rms;
        if (start + spec.hopSize >= frameCount) break;
    }
    return output;
}

Track<float> bakeAudioDriverTrack(std::span<const AudioFeatureSample> features,
                                   const AudioDriverSpec& driver, float fps,
                                   int firstFrame, int lastFrame) {
    validateFrameRange(firstFrame, lastFrame);
    if (features.empty() || !std::isfinite(fps) || fps <= 0.f ||
        !std::isfinite(static_cast<float>(lastFrame) / fps) || !std::isfinite(1.f / fps) ||
        static_cast<unsigned>(driver.feature) > static_cast<unsigned>(AudioFeature::Onset) ||
        !std::isfinite(driver.multiplier) || !std::isfinite(driver.bias) ||
        !std::isfinite(driver.smoothing) || driver.smoothing < 0.f)
        throw std::invalid_argument("bakeAudioDriverTrack: features, fps and driver parameters must be valid");
    if (features.size() > kMaximumBakedFrames)
        throw std::invalid_argument("bakeAudioDriverTrack: feature count exceeds the offline sample budget");
    for (std::size_t i = 0; i < features.size(); ++i) {
        if (!std::isfinite(features[i].seconds) || features[i].seconds < 0.f ||
            (i && features[i].seconds <= features[i - 1].seconds) ||
            !std::isfinite(features[i].rms) || features[i].rms < 0.f ||
            !std::isfinite(features[i].bass) || features[i].bass < 0.f ||
            !std::isfinite(features[i].mid) || features[i].mid < 0.f ||
            !std::isfinite(features[i].treble) || features[i].treble < 0.f ||
            !std::isfinite(features[i].onset) || features[i].onset < 0.f)
            throw std::invalid_argument("bakeAudioDriverTrack: feature samples must be finite and strictly ordered");
    }
    Track<float> track;
    float smoothed = features.front().value(driver.feature);
    std::size_t upper = 0;
    const float dt = 1.f / fps;
    const std::size_t frameCount = static_cast<std::size_t>(lastFrame - firstFrame) + 1;
    validateFrameTimes(fps, firstFrame, lastFrame, "bakeAudioDriverTrack");
    for (std::size_t frameIndex = 0; frameIndex < frameCount; ++frameIndex) {
        const int frame = firstFrame + static_cast<int>(frameIndex);
        const float time = static_cast<float>(frame) / fps;
        while (upper + 1 < features.size() && features[upper + 1].seconds < time) ++upper;
        float value = features[upper].value(driver.feature);
        if (upper + 1 < features.size()) {
            const auto& a = features[upper];
            const auto& b = features[upper + 1];
            const float amount = std::clamp((time - a.seconds) / (b.seconds - a.seconds), 0.f, 1.f);
            value = a.value(driver.feature) + (b.value(driver.feature) - a.value(driver.feature)) * amount;
        }
        if (driver.smoothing > 0.f) {
            const float alpha = 1.f - std::exp(-dt / driver.smoothing);
            smoothed += (value - smoothed) * alpha;
            value = smoothed;
        } else {
            smoothed = value;
        }
        const float driven = driver.bias + driver.multiplier * value;
        if (!std::isfinite(time) || !std::isfinite(value) || !std::isfinite(smoothed) || !std::isfinite(driven))
            throw std::invalid_argument("bakeAudioDriverTrack: output key is non-finite");
        track.add(time, driven);
    }
    return track;
}

void bakeAudioDriverToLayer(Layer& layer, std::span<const AudioFeatureSample> features,
                            const AudioDriverSpec& driver, AudioDriverProperty property,
                            const Vector3& axis, float fps, int firstFrame, int lastFrame) {
    if (!std::isfinite(axis.x) || !std::isfinite(axis.y) || !std::isfinite(axis.z))
        throw std::invalid_argument("bakeAudioDriverToLayer: axis must be finite");
    const auto scalar = bakeAudioDriverTrack(features, driver, fps, firstFrame, lastFrame);
    Track<Vector3>* vectorTrack = nullptr;
    Track<float>* opacityTrack = nullptr;
    switch (property) {
        case AudioDriverProperty::Position: vectorTrack = &layer.tracks.position; break;
        case AudioDriverProperty::Rotation: vectorTrack = &layer.tracks.rotation; break;
        case AudioDriverProperty::Scale: vectorTrack = &layer.tracks.scale; break;
        case AudioDriverProperty::Opacity: opacityTrack = &layer.tracks.opacity; break;
        default: throw std::invalid_argument("bakeAudioDriverToLayer: unsupported target property");
    }
    if (vectorTrack) {
        Track<Vector3> baked;
        for (const auto& key : scalar.keys()) {
            Vector3 base = property == AudioDriverProperty::Position ? layer.transform.position :
                           property == AudioDriverProperty::Rotation ? layer.transform.rotation : layer.transform.scale;
            if (!vectorTrack->empty()) base = vectorTrack->sample(key.time);
            Vector3 value;
            if (property == AudioDriverProperty::Scale) {
                value.set(base.x * (1.f + axis.x * key.value),
                          base.y * (1.f + axis.y * key.value),
                          base.z * (1.f + axis.z * key.value));
            } else {
                value.set(base.x + axis.x * key.value,
                          base.y + axis.y * key.value,
                          base.z + axis.z * key.value);
            }
            if (!std::isfinite(value.x) || !std::isfinite(value.y) || !std::isfinite(value.z))
                throw std::invalid_argument("bakeAudioDriverToLayer: vector output is non-finite");
            baked.add(key.time, value);
        }
        *vectorTrack = std::move(baked);
    } else if (opacityTrack) {
        Track<float> baked;
        for (const auto& key : scalar.keys()) {
            const float base = opacityTrack->empty() ? layer.transform.opacity : opacityTrack->sample(key.time);
            const float value = base + key.value;
            if (!std::isfinite(base) || !std::isfinite(value))
                throw std::invalid_argument("bakeAudioDriverToLayer: opacity output is non-finite");
            baked.add(key.time, std::clamp(value, 0.f, 1.f));
        }
        *opacityTrack = std::move(baked);
    }
}

FlowResult solveFlowLayout(const FlowSpec& spec, std::span<const FlowItem> items,
                           const Vector2& origin) {
    if (static_cast<unsigned>(spec.axis) > static_cast<unsigned>(FlowAxis::Column) ||
        static_cast<unsigned>(spec.mainAlignment) > static_cast<unsigned>(FlowMainAlignment::SpaceBetween) ||
        static_cast<unsigned>(spec.crossAlignment) > static_cast<unsigned>(FlowCrossAlignment::Stretch))
        throw std::invalid_argument("solveFlowLayout: invalid axis or alignment");
    const float padding[] = {spec.padding.top, spec.padding.right, spec.padding.bottom, spec.padding.left};
    if (!finite(origin) || !finite(spec.containerSize) || !std::isfinite(spec.gap) || spec.gap < 0.f)
        throw std::invalid_argument("solveFlowLayout: origin, container and gap must be finite; gap non-negative");
    for (float value : padding)
        if (!std::isfinite(value) || value < 0.f) throw std::invalid_argument("solveFlowLayout: padding must be finite and non-negative");
    std::unordered_set<std::uint32_t> ids;
    if (items.size() > 65536)
        throw std::invalid_argument("solveFlowLayout: item count exceeds 65536");
    float naturalMain = 0.f, naturalCross = 0.f;
    for (const auto& item : items) {
        if (!ids.insert(item.id).second || !finite(item.measuredSize) || item.measuredSize.x < 0.f || item.measuredSize.y < 0.f ||
            !finite(item.anchor) || item.anchor.x < 0.f || item.anchor.x > 1.f || item.anchor.y < 0.f || item.anchor.y > 1.f)
            throw std::invalid_argument("solveFlowLayout: duplicate ids or invalid measured item");
        const float main = spec.axis == FlowAxis::Row ? item.measuredSize.x : item.measuredSize.y;
        const float cross = spec.axis == FlowAxis::Row ? item.measuredSize.y : item.measuredSize.x;
        naturalMain += main;
        naturalCross = std::max(naturalCross, cross);
        if (!std::isfinite(naturalMain) || !std::isfinite(naturalCross))
            throw std::invalid_argument("solveFlowLayout: intrinsic item extent overflowed");
    }
    const float mainLeading = spec.axis == FlowAxis::Row ? spec.padding.left : spec.padding.top;
    const float mainTrailing = spec.axis == FlowAxis::Row ? spec.padding.right : spec.padding.bottom;
    const float crossLeading = spec.axis == FlowAxis::Row ? spec.padding.top : spec.padding.left;
    const float crossTrailing = spec.axis == FlowAxis::Row ? spec.padding.bottom : spec.padding.right;
    const float explicitMain = spec.axis == FlowAxis::Row ? spec.containerSize.x : spec.containerSize.y;
    const float explicitCross = spec.axis == FlowAxis::Row ? spec.containerSize.y : spec.containerSize.x;
    if (explicitMain < 0.f || explicitCross < 0.f)
        throw std::invalid_argument("solveFlowLayout: container dimensions must be non-negative");
    const float mainExtent = explicitMain > 0.f ? explicitMain : mainLeading + naturalMain + mainTrailing +
        (items.empty() ? 0.f : spec.gap * static_cast<float>(items.size() - 1));
    const float crossExtent = explicitCross > 0.f ? explicitCross : crossLeading + naturalCross + crossTrailing;
    if (!std::isfinite(mainExtent) || !std::isfinite(crossExtent))
        throw std::invalid_argument("solveFlowLayout: container extent overflowed");
    const float innerMain = std::max(0.f, mainExtent - mainLeading - mainTrailing);
    const float innerCross = std::max(0.f, crossExtent - crossLeading - crossTrailing);
    const float gaps = items.empty() ? 0.f : spec.gap * static_cast<float>(items.size() - 1);
    const float occupied = naturalMain + gaps;
    if (!std::isfinite(innerMain) || !std::isfinite(innerCross) ||
        !std::isfinite(gaps) || !std::isfinite(occupied))
        throw std::invalid_argument("solveFlowLayout: resolved inner extent or item spacing overflowed");
    float cursor = mainLeading;
    float effectiveGap = spec.gap;
    if (spec.mainAlignment == FlowMainAlignment::Center) cursor += (innerMain - occupied) * 0.5f;
    else if (spec.mainAlignment == FlowMainAlignment::End) cursor += innerMain - occupied;
    else if (spec.mainAlignment == FlowMainAlignment::SpaceBetween && items.size() > 1)
        effectiveGap += std::max(0.f, innerMain - naturalMain - gaps) / static_cast<float>(items.size() - 1);
    FlowResult result;
    result.items.reserve(items.size());
    for (const auto& item : items) {
        const float width = item.measuredSize.x, height = item.measuredSize.y;
        const float itemMain = spec.axis == FlowAxis::Row ? width : height;
        float itemCross = spec.axis == FlowAxis::Row ? height : width;
        float crossPosition = crossLeading;
        if (spec.crossAlignment == FlowCrossAlignment::Center) crossPosition += (innerCross - itemCross) * 0.5f;
        else if (spec.crossAlignment == FlowCrossAlignment::End) crossPosition += innerCross - itemCross;
        else if (spec.crossAlignment == FlowCrossAlignment::Stretch) itemCross = innerCross;
        const float x = origin.x + (spec.axis == FlowAxis::Row ? cursor : crossPosition);
        const float y = origin.y + (spec.axis == FlowAxis::Row ? crossPosition : cursor);
        if (spec.crossAlignment == FlowCrossAlignment::Stretch && itemCross < 0.f)
            throw std::invalid_argument("solveFlowLayout: invalid stretched cross extent");
        const float resolvedWidth = spec.axis == FlowAxis::Column && spec.crossAlignment == FlowCrossAlignment::Stretch ? itemCross : width;
        const float resolvedHeight = spec.axis == FlowAxis::Row && spec.crossAlignment == FlowCrossAlignment::Stretch ? itemCross : height;
        FlowItemResult itemResult;
        itemResult.id = item.id;
        itemResult.anchor = item.anchor;
        itemResult.size = Vector2(resolvedWidth, resolvedHeight);
        itemResult.position = Vector2(x + item.anchor.x * resolvedWidth, y + item.anchor.y * resolvedHeight);
        itemResult.bounds.set(Vector2(x, y), Vector2(x + resolvedWidth, y + resolvedHeight));
        if (!finite(itemResult.position) || !finite(itemResult.size) || !finite(Vector2(x + resolvedWidth, y + resolvedHeight)))
            throw std::invalid_argument("solveFlowLayout: resolved item overflows finite coordinates");
        result.items.push_back(itemResult);
        cursor += itemMain + effectiveGap;
    }
    const Vector2 maximum(origin.x + (spec.axis == FlowAxis::Row ? mainExtent : crossExtent),
                          origin.y + (spec.axis == FlowAxis::Row ? crossExtent : mainExtent));
    if (!finite(maximum)) throw std::invalid_argument("solveFlowLayout: resolved container bounds overflow");
    result.bounds.set(origin, maximum);
    return result;
}

FlowResult refreshFlowLayout(const FlowSpec& spec, std::span<const FlowItem> items,
                             MotionScene& scene, const Vector2& origin) {
    FlowResult result = solveFlowLayout(spec, items, origin);
    std::vector<Layer*> layers;
    layers.reserve(result.items.size());
    std::unordered_map<std::uint32_t, const FlowItem*> sources;
    sources.reserve(items.size());
    for (const auto& source : items) sources.emplace(source.id, &source);
    for (const auto& item : result.items) {
        Layer* layer = scene.findLayer(item.id);
        if (!layer || layer->isNull())
            throw std::invalid_argument("refreshFlowLayout: every item id must name a content layer in the scene");
        layers.push_back(layer);
    }
    // Validate every target before committing any layout changes: a late
    // animated layer must not leave earlier layers partially refreshed.
    for (const Layer* layer : layers) {
        if (!layer->tracks.position.empty() || !layer->tracks.anchor.empty())
            throw std::invalid_argument("refreshFlowLayout: layout cannot overwrite animated position or anchor tracks");
    }
    for (std::size_t index = 0; index < result.items.size(); ++index) {
        const auto& item = result.items[index];
        const FlowItem& source = *sources.at(item.id);
        Layer* layer = layers[index];
        layer->content.metrics.fingerprint = source.measurementFingerprint;
        layer->content.metrics.naturalSize = source.measuredSize;
        layer->content.metrics.anchor = item.anchor;
        layer->transform.position.x = item.position.x;
        layer->transform.position.y = item.position.y;
        layer->transform.anchor.set((item.anchor.x - 0.5f) * item.size.x,
                                    (item.anchor.y - 0.5f) * item.size.y,
                                    layer->transform.anchor.z);
    }
    return result;
}

BakedPath parseSvgPath(std::string_view pathData) {
    return SvgParser(pathData).parse();
}

BakedPath interpolateBakedPaths(const BakedPath& from, const BakedPath& to, float amount) {
    validatePathPair(from, to);
    if (!std::isfinite(amount)) throw std::invalid_argument("interpolateBakedPaths: amount must be finite");
    amount = std::clamp(amount, 0.f, 1.f);
    BakedPath result = from;
    for (std::size_t i = 0; i < result.size(); ++i) {
        result[i].point.lerp(to[i].point, amount);
        result[i].control1.lerp(to[i].control1, amount);
        result[i].control2.lerp(to[i].control2, amount);
        if (!finite(result[i].point) || !finite(result[i].control1) || !finite(result[i].control2))
            throw std::invalid_argument("interpolateBakedPaths: commands must have finite coordinates");
    }
    return result;
}

std::vector<BakedPath> bakeSvgPathMorph(std::string_view from, std::string_view to,
                                         int firstFrame, int lastFrame) {
    validateFrameRange(firstFrame, lastFrame);
    const BakedPath start = parseSvgPath(from), end = parseSvgPath(to);
    validatePathPair(start, end);
    std::vector<BakedPath> frames;
    const std::size_t count = static_cast<std::size_t>(lastFrame - firstFrame) + 1;
    if (start.size() > kMaximumBakedPathCommands / count)
        throw std::invalid_argument("bakeSvgPathMorph: requested geometry exceeds the offline command budget");
    frames.reserve(count);
    for (std::size_t frame = 0; frame < count; ++frame) {
        const float t = count <= 1 ? 0.f : static_cast<float>(frame) / static_cast<float>(count - 1);
        frames.push_back(interpolateBakedPaths(start, end, t));
    }
    return frames;
}

std::vector<BakedPath> expandPathRepeater(const BakedPath& path,
                                           const RepeaterSpec& repeater,
                                           std::uint32_t maximumCopies) {
    validateBakedPath(path, "expandPathRepeater");
    if (path.empty() || path.size() > 65536 || path.front().type != BakedPathCommandType::MoveTo ||
        repeater.copies == 0 || repeater.copies > maximumCopies || maximumCopies > 100000 ||
        repeater.copies > kMaximumBakedPathCommands / path.size() ||
        !finite(repeater.position) || !finite(repeater.scale) || !finite(repeater.anchor) ||
        !std::isfinite(repeater.rotationRadians))
        throw std::invalid_argument("expandPathRepeater: invalid path or repeater settings");
    std::vector<BakedPath> result;
    result.reserve(repeater.copies);
    for (std::uint32_t copy = 0; copy < repeater.copies; ++copy) {
        const float power = static_cast<float>(copy);
        const Vector2 scale(std::pow(repeater.scale.x, power), std::pow(repeater.scale.y, power));
        if (!finite(scale)) throw std::invalid_argument("expandPathRepeater: scale overflow");
        const float angle = repeater.rotationRadians * power;
        const Vector2 translation = repeater.position * power;
        BakedPath transformedPath = path;
        for (auto& command : transformedPath) {
            if (command.type == BakedPathCommandType::Close) continue;
            command.point = transformed(command.point, repeater.anchor, scale, angle, translation);
            if (command.type == BakedPathCommandType::CubicTo) {
                command.control1 = transformed(command.control1, repeater.anchor, scale, angle, translation);
                command.control2 = transformed(command.control2, repeater.anchor, scale, angle, translation);
            }
            if (!finite(command.point) ||
                (command.type == BakedPathCommandType::CubicTo &&
                 (!finite(command.control1) || !finite(command.control2))))
                throw std::invalid_argument("expandPathRepeater: transformed command overflows finite coordinates");
        }
        result.push_back(std::move(transformedPath));
    }
    return result;
}

} // namespace chrononmotion::motion