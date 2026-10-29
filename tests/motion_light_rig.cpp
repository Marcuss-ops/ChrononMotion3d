#include "chrononmotion/motion/LightRig.hpp"
#include "chrononmotion/motion/LightRigRenderPlan.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace chrononmotion;
using namespace chrononmotion::motion;

namespace {
void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
bool near(float a, float b) { return std::abs(a - b) < 1e-4f; }
}

int main() try {
    check(lightRigPresets::catalog().size() == 3, "three stable editorial presets");
    for (const auto id : lightRigPresets::catalog()) {
        auto rig = lightRigPresets::make(id);
        check(!rig.sources.empty() && rig.sources.size() <= LightRig::kMaxSources,
              "preset within source cap");
        check(rig.serialize() == rig.serialize(), "deterministic contract serialization");
    }

    auto rig = lightRigPresets::make("soft_top");
    const auto initial = rig.sample(0.f);
    check(near(initial[0].local.position.y, 5.f), "source retains local position");
    rig.transform.rotation.z = 1.57079632679f;
    auto rotated = rig.sample(0.f);
    check(near(rotated[0].local.position.x, -5.f) && near(rotated[0].local.position.y, 0.f),
          "rig rotation transforms every source position");
    lightRigPresets::animateRotation(rig, 0.f, 1.f, {0.f, 0.f, 0.f}, {0.f, 0.f, 1.57079632679f});
    rotated = rig.sample(1.f);
    check(near(rotated[0].local.position.x, -5.f), "keyed rig rotation is sampled");

    chronon3d::render_plan::RenderPlan plan;
    appendLightRigToRenderPlan(plan, rig, 30.f, 31);
    check(plan.layers.size() == rig.sources.size(), "one RenderPlan light layer per source");
    check(plan.layers[0].light.has_value() && plan.layers[0].animation.has_value(),
          "animated rig lowers source payload and per-frame tracks");
    check(plan.layers[0].animation->tracks.front().keyframes.size() == 31,
          "lowering produces the requested frame samples");

    auto invalid = rig;
    invalid.sources.resize(LightRig::kMaxSources + 1);
    bool rejected = false;
    try { invalid.validate(); } catch (const std::invalid_argument&) { rejected = true; }
    check(rejected, "source count beyond cap rejected");
    rejected = false;
    try { appendLightRigToRenderPlan(plan, rig, 30.f, 4097); }
    catch (const std::invalid_argument&) { rejected = true; }
    check(rejected, "RenderPlan bake over 4096 frames rejected");
    std::cout << "PASS: 15 LightRig V1 checks\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "motion_light_rig: " << error.what() << '\n';
    return 1;
}
