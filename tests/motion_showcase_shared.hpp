#include "chrononmotion/chrononmotion.hpp"
#include "chrononmotion/motion/OfflineBaking.hpp"
#include "motion_check.hpp"

#if defined(CHRONONMOTION_SHOWCASE_HAS_ADAPTER)
#include "chrononmotion/motion/RenderPlanEmission.hpp"
#include "chrononmotion/motion/RenderPlanLowering.hpp"
#include <nlohmann/json.hpp>
#endif

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace chrononmotion;
using namespace chrononmotion::motion;
using chrononmotion_test::check;
using chrononmotion_test::checkNear;
using chrononmotion_test::section;

namespace chrononmotion_test::showcase {

constexpr float kFps = 30.f;
constexpr float kPi = 3.14159265358979323846f;

struct Checks {
    int count{0};
    int failures{0};

    void that(bool value, const char* message) {
        ++count;
        if (!value) {
            ++failures;
            std::cout << "    ASSERT FAIL: " << message << '\n';
        }
    }

    void near(float actual, float expected, float epsilon, const char* message) {
        const bool matches = std::isfinite(actual) && std::fabs(actual - expected) <= epsilon;
        that(matches, message);
        if (!matches) {
            std::cout << "      actual=" << actual << " expected=" << expected
                      << " tolerance=" << epsilon << '\n';
        }
    }

    void near(const Vector2& actual, const Vector2& expected, float epsilon, const char* message) {
        that(std::isfinite(actual.x) && std::isfinite(actual.y) &&
             actual.distanceTo(expected) <= epsilon, message);
    }

    void near(const Vector3& actual, const Vector3& expected, float epsilon, const char* message) {
        that(std::isfinite(actual.x) && std::isfinite(actual.y) && std::isfinite(actual.z) &&
             actual.distanceTo(expected) <= epsilon, message);
    }

    void near(float actual, float expected, float epsilon, const std::string& message) {
        near(actual, expected, epsilon, message.c_str());
    }
    void near(const Vector2& actual, const Vector2& expected, float epsilon, const std::string& message) {
        near(actual, expected, epsilon, message.c_str());
    }
    void near(const Vector3& actual, const Vector3& expected, float epsilon, const std::string& message) {
        near(actual, expected, epsilon, message.c_str());
    }

    void samePath(const BakedPath& a, const BakedPath& b, const char* message) {
        bool same = a.size() == b.size();
        for (std::size_t i = 0; same && i < a.size(); ++i) {
            same = a[i].type == b[i].type && a[i].point == b[i].point &&
                   a[i].control1 == b[i].control1 && a[i].control2 == b[i].control2;
        }
        that(same, message);
    }

    void samePaths(const std::vector<BakedPath>& a, const std::vector<BakedPath>& b, const char* message) {
        bool same = a.size() == b.size();
        for (std::size_t i = 0; same && i < a.size(); ++i) {
            same = a[i].size() == b[i].size();
            for (std::size_t j = 0; same && j < a[i].size(); ++j) {
                same = a[i][j].type == b[i][j].type && a[i][j].point == b[i][j].point &&
                       a[i][j].control1 == b[i][j].control1 && a[i][j].control2 == b[i][j].control2;
            }
        }
        that(same, message);
    }
};

class Snapshot {
public:
    void text(std::string_view value) {
        data_ += std::to_string(value.size());
        data_ += ':';
        data_.append(value);
        data_ += ';';
    }

    void number(float value) {
        char buffer[64];
        const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value,
                                          std::chars_format::general,
                                          std::numeric_limits<float>::max_digits10);
        if (result.ec != std::errc{}) throw std::runtime_error("cannot format golden float");
        text(std::string_view(buffer, static_cast<std::size_t>(result.ptr - buffer)));
    }

    void integer(std::int64_t value) { text(std::to_string(value)); }
    void vector(const Vector3& value) { number(value.x); number(value.y); number(value.z); }
    void vector(const Vector2& value) { number(value.x); number(value.y); }

    [[nodiscard]] const std::string& data() const { return data_; }

private:
    std::string data_;
};

[[nodiscard]] inline std::string hash64(std::string_view bytes) {
    std::uint64_t value = 14695981039346656037ULL;
    for (const unsigned char byte : bytes) {
        value ^= byte;
        value *= 1099511628211ULL;
    }
    std::ostringstream output;
    output << std::hex << std::setfill('0') << std::setw(16) << value;
    return output.str();
}

[[nodiscard]] inline float secondsAt(int frame, float fps = kFps) {
    return static_cast<float>(frame) / fps;
}

[[nodiscard]] inline Vector3 worldPosition(const MotionScene& scene, Layer::Id id) {
    Vector3 value;
    value.setFromMatrixPosition(scene.worldMatrix(id));
    return value;
}

inline void capture(MotionScene& scene, Snapshot& snapshot, std::initializer_list<float> times) {
    snapshot.text(scene.toJson(0));
    for (const float time : times) {
        scene.evaluatePtsExact(time);
        snapshot.number(time);
        for (const auto& layer : scene.layers()) {
            snapshot.integer(layer.id);
            const LayerState& state = scene.state(layer.id);
            for (const float component : state.world.elements) snapshot.number(component);
            snapshot.number(state.worldOpacity);
            snapshot.integer(state.visible ? 1 : 0);
        }
    }
}

inline Layer& addCard(MotionScene& scene, Layer::Id id, const std::string& name,
               const Vector3& position = Vector3{}) {
    Layer& layer = scene.addContent(id, name, ContentRef::image(name, {32.f, 24.f}));
    layer.transform.position.copy(position);
    return layer;
}

[[nodiscard]] inline std::uint32_t xorshift(std::uint32_t& state) {
    state ^= state << 13U;
    state ^= state >> 17U;
    state ^= state << 5U;
    return state;
}

struct ShowcaseCase {
    unsigned number;
    std::string id;
    std::string description;
    std::string skippedBecause;
    std::function<void(Checks&, Snapshot&)> run;
};


void addTransformCases(std::vector<ShowcaseCase>& cases);
void addKinematicsCases(std::vector<ShowcaseCase>& cases);
void addShapeCases(std::vector<ShowcaseCase>& cases);
void addCameraCases(std::vector<ShowcaseCase>& cases);
void addRuntimeCases(std::vector<ShowcaseCase>& cases);

} // namespace chrononmotion_test::showcase
