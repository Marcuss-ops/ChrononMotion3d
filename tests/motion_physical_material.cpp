// Physical material extension: glass/coating values stay data, validate before
// they reach a renderer, keep neutral documents byte-identical and survive the
// scene round-trip.

#include "chrononmotion/motion/CompiledScene.hpp"
#include "chrononmotion/motion/MotionScene.hpp"
#include "chrononmotion/motion/SceneExport.hpp"
#include "chrononmotion/motion/SceneImport.hpp"

#include "motion_check.hpp"

#include <functional>
#include <limits>
#include <stdexcept>
#include <string>

using namespace chrononmotion;
using namespace chrononmotion::motion;
using namespace chrononmotion_test;

namespace {

    bool throwsInvalid(const std::function<void()>& call) {
        try {
            call();
        } catch (const std::invalid_argument&) {
            return true;
        }
        return false;
    }

    MaterialDescriptor glass() {
        MaterialDescriptor material = MaterialDescriptor::pbr(11, Color(0xffffff), 0.05f, 0.f);
        material.transmission = 1.f;
        material.ior = 1.5f;
        material.thickness = 0.4f;
        material.dispersion = 0.02f;
        material.clearcoat = 1.f;
        return material;
    }

    MeshAttachment attachmentWith(const MaterialDescriptor& material) {
        MeshAttachment attachment;
        attachment.assetId = "glass-hero.glb";
        attachment.material = material;
        return attachment;
    }

}// namespace

int main() {
    chrononmotion_test::section("neutral defaults");

    const MaterialDescriptor neutral = MaterialDescriptor::pbr(1, Color(0x808080), 0.4f, 0.1f);
    check(!neutral.hasPhysicalProperties(),
          "an ordinary PBR descriptor stays on the non-physical path");
    neutral.validate();

    chrononmotion_test::section("physical range contract");

    MaterialDescriptor physical = glass();
    check(physical.hasPhysicalProperties(), "glass values select the physical path");
    physical.validate();

    MaterialDescriptor tooTransmissive = physical;
    tooTransmissive.transmission = 1.5f;
    check(throwsInvalid([&] { tooTransmissive.validate(); }),
          "transmission above 1 is rejected");

    MaterialDescriptor badIor = physical;
    badIor.ior = 0.9f;
    check(throwsInvalid([&] { badIor.validate(); }), "IOR below 1 is rejected");
    badIor.ior = 2.6f;
    check(throwsInvalid([&] { badIor.validate(); }), "IOR above 2.5 is rejected");

    MaterialDescriptor badThickness = physical;
    badThickness.thickness = -0.1f;
    check(throwsInvalid([&] { badThickness.validate(); }), "negative thickness is rejected");
    badThickness.thickness = 1001.f;
    check(throwsInvalid([&] { badThickness.validate(); }), "thickness above 1000 is rejected");

    MaterialDescriptor badDispersion = physical;
    badDispersion.dispersion = 1.1f;
    check(throwsInvalid([&] { badDispersion.validate(); }), "dispersion above 1 is rejected");

    MaterialDescriptor badClearcoat = physical;
    badClearcoat.clearcoat = -0.01f;
    check(throwsInvalid([&] { badClearcoat.validate(); }), "negative clearcoat is rejected");

    MaterialDescriptor notFinite = physical;
    notFinite.ior = std::numeric_limits<float>::infinity();
    check(throwsInvalid([&] { notFinite.validate(); }), "non-finite physical values are rejected");

    chrononmotion_test::section("compile handoff");

    MotionScene scene("physical material", 30.f);
    scene.addMesh(30, "glass-hero", attachmentWith(physical)).alive(0, 30);
    scene.evaluate(12);
    const CompiledScene compiled = scene.compile();
    check(compiled.meshes.size() == 1, "the glass layer compiles");
    if (compiled.meshes.size() == 1) {
        const MaterialDescriptor& carried = compiled.meshes.at(0).material;
        checkNear(carried.transmission, 1.f, 1e-6f, "compile keeps transmission");
        checkNear(carried.ior, 1.5f, 1e-6f, "compile keeps IOR");
        checkNear(carried.thickness, 0.4f, 1e-6f, "compile keeps thickness");
        checkNear(carried.dispersion, 0.02f, 1e-6f, "compile keeps dispersion");
        checkNear(carried.clearcoat, 1.f, 1e-6f, "compile keeps clearcoat");
    }

    chrononmotion_test::section("scene round-trip");

    const std::string json = sceneToJson(scene);
    check(json.find("\"physical\"") != std::string::npos,
          "a non-neutral material writes the physical block");
    const MotionScene imported = sceneFromJson(json);
    check(sceneToJson(imported) == json,
          "export -> import -> export is byte-identical with physical values");
    if (!imported.layers().empty()) {
        const MaterialDescriptor& restored = imported.layers().front().mesh.material;
        checkNear(restored.transmission, 1.f, 1e-6f, "import restores transmission");
        checkNear(restored.clearcoat, 1.f, 1e-6f, "import restores clearcoat");
        check(restored.hasPhysicalProperties(), "import keeps the physical path");
    }

    MotionScene neutralScene("neutral material", 30.f);
    neutralScene.addMesh(31, "plain", attachmentWith(neutral));
    check(sceneToJson(neutralScene).find("\"physical\"") == std::string::npos,
          "a neutral material keeps the previous scene bytes");

    return chrononmotion_test::report();
}
