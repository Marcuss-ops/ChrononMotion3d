#include "chrononmotion/motion/OfflineBaking.hpp"

#include "chrononmotion/math/Finite.hpp"
#include "chrononmotion/math/MathUtils.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

namespace chrononmotion::motion {
namespace {

constexpr float kEpsilon = 1e-6f;

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
            if (value.type != BakedPathCommandType::Close && !math::isFinite(value.point))
                fail("parsed coordinates overflow finite range");
            if (value.type == BakedPathCommandType::CubicTo &&
                (!math::isFinite(value.control1) || !math::isFinite(value.control2)))
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
        if (!math::isFinite(c1) || !math::isFinite(c2))
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
        if (!math::isFinite(c1) || !math::isFinite(c2))
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

} // namespace

BakedPath parseSvgPath(std::string_view pathData) {
    return SvgParser(pathData).parse();
}

} // namespace chrononmotion::motion
