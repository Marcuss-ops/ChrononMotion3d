// InstanceBatch contract: one layer, many placements, no second animation
// authority. Pins the bounded validation, the compile handoff, the
// layer-world * local-transform sampling rule and the scene round-trip.

#include "chrononmotion/geometries/BoxGeometry.hpp"
#include "chrononmotion/motion/CompiledScene.hpp"
#include "chrononmotion/motion/MotionScene.hpp"
#include "chrononmotion/motion/SceneExport.hpp"
#include "chrononmotion/motion/SceneImport.hpp"

#include "motion_check.hpp"

#include <functional>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

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

    Matrix4 translated(float x, float y, float z) {
        Matrix4 matrix;
        matrix.makeTranslation(x, y, z);
        return matrix;
    }

    MeshAttachment crowdAttachment() {
        MeshAttachment attachment;
        attachment.assetId = "crowd.glb";
        attachment.material = MaterialDescriptor::lambert(7, Color(0x8899aa));
        InstanceBatch batch;
        batch.localTransforms = {
                Matrix4{},
                translated(2.f, 0.f, 0.f),
                translated(0.f, 0.f, 4.f)};
        attachment.instances = std::move(batch);
        return attachment;
    }

    void contractValidation() {
        chrononmotion_test::section("instance batch validation");

        InstanceBatch valid;
        valid.localTransforms = {Matrix4{}, translated(1.f, 2.f, 3.f)};
        check(!valid.empty(), "a populated batch is not empty");
        bool validated = false;
        try {
            valid.validate();
            validated = true;
        } catch (const std::invalid_argument&) {
        }
        check(validated, "finite non-singular placements are accepted");

        InstanceBatch empty;
        check(throwsInvalid([&] { empty.validate(); }),
              "an empty batch is rejected instead of silently drawing one instance");

        InstanceBatch notFinite;
        notFinite.localTransforms = {Matrix4{}};
        notFinite.localTransforms[0].elements[12] = std::numeric_limits<float>::quiet_NaN();
        check(throwsInvalid([&] { notFinite.validate(); }),
              "a non-finite placement is rejected");

        InstanceBatch singular;
        singular.localTransforms = {Matrix4{}};
        singular.localTransforms[0].elements[0] = 0.f;
        check(throwsInvalid([&] { singular.validate(); }),
              "a singular placement is rejected");

        InstanceBatch mirrored;
        Matrix4 flip;
        flip.makeScale(-1.f, 1.f, 1.f);
        mirrored.localTransforms = {flip};
        bool mirroredAccepted = false;
        try {
            mirrored.validate();
            mirroredAccepted = true;
        } catch (const std::invalid_argument&) {
        }
        check(mirroredAccepted, "a mirrored (negative determinant) placement is legal");

        InstanceBatch unbounded;
        unbounded.localTransforms.assign(InstanceBatch::kMaxInstances + 1, Matrix4{});
        check(throwsInvalid([&] { unbounded.validate(); }),
              "a batch above the instance ceiling is rejected");
    }

    void compileAndSample() {
        chrononmotion_test::section("compile and random-access placement");

        MotionScene scene("instance batch", 30.f);
        MeshAttachment attachment;
        attachment.geometry = BoxGeometry::create(0.2f, 1.f, 0.2f);
        attachment.material = MaterialDescriptor::lambert(9, Color(0x336699));
        InstanceBatch batch;
        batch.localTransforms = {translated(2.f, 0.f, 0.f), translated(-2.f, 0.f, 0.f)};
        attachment.instances = std::move(batch);
        Layer& layer = scene.addMesh(10, "crowd", std::move(attachment));
        layer.transform.position = Vector3(0.f, 1.f, 0.f);
        layer.alive(0, 10);

        scene.evaluate(3);
        const CompiledScene compiled = scene.compile();
        check(compiled.meshes.size() == 1, "the mesh layer compiles to one mesh descriptor");
        const MeshDescriptor& mesh = compiled.meshes.at(0);
        check(mesh.instances.has_value() && mesh.instances->localTransforms.size() == 2,
              "the compiled descriptor carries the authored batch");

        const CompiledTransform* transform = nullptr;
        for (const auto& candidate : compiled.transforms) {
            if (candidate.id == 10) transform = &candidate;
        }
        check(transform != nullptr, "the batch root has a resolved world transform");
        if (transform && mesh.instances) {
            Matrix4 world;
            world.multiplyMatrices(transform->world, mesh.instances->localTransforms[0]);
            checkNear(world.elements[12], 2.f, 1e-5f, "instance 0 keeps its local x placement");
            checkNear(world.elements[13], 1.f, 1e-5f, "instance 0 inherits the layer height");
            world.multiplyMatrices(transform->world, mesh.instances->localTransforms[1]);
            checkNear(world.elements[12], -2.f, 1e-5f, "instance 1 keeps its own local x placement");
        }

        scene.evaluate(3);
        const std::string first = sceneToJson(scene);
        scene.evaluate(3);
        check(first == sceneToJson(scene), "the exported batch is deterministic for the same frame");
        check(first.find("\"instances\"") != std::string::npos,
              "the scene record carries the instance batch");
    }

    void batchBounds() {
        section("instance batch bounds");

        const Box3 unit(Vector3(-1.f, -1.f, -1.f), Vector3(1.f, 1.f, 1.f));
        InstanceBatch batch;
        batch.localTransforms = {Matrix4{}, translated(10.f, 0.f, 0.f)};
        const Box3 bounds = batch.bounds(unit);
        checkNear(bounds.min(), Vector3(-1.f, -1.f, -1.f), 1e-5f,
                  "bounds include the untransformed instance");
        checkNear(bounds.max(), Vector3(11.f, 1.f, 1.f), 1e-5f,
                  "bounds include the translated instance");
        check(InstanceBatch{}.bounds(unit).isEmpty(), "an empty batch has empty bounds");
    }

    void roundTrip() {
        chrononmotion_test::section("scene round-trip");

        MotionScene scene("instance round trip", 24.f);
        scene.addMesh(20, "crowd", crowdAttachment()).alive(0, 20);

        const std::string json = sceneToJson(scene);
        const MotionScene imported = sceneFromJson(json);
        check(sceneToJson(imported) == json,
              "export -> import -> export is byte-identical with an instance batch");

        MotionScene plain("no instances", 24.f);
        MeshAttachment single;
        single.assetId = "single.glb";
        single.material = MaterialDescriptor::lambert(8, Color(0xffffff));
        plain.addMesh(21, "single", std::move(single));
        check(sceneToJson(plain).find("\"instances\"") == std::string::npos,
              "a mesh without a batch keeps the previous scene bytes");

        check(throwsInvalid([&] {
                  MotionScene broken("broken", 24.f);
                  MeshAttachment attachment = crowdAttachment();
                  attachment.instances->localTransforms.clear();
                  broken.addMesh(22, "broken", std::move(attachment));
                  broken.evaluate(0);
                  (void) broken.compile();
              }),
              "a scene carrying an empty batch fails closed at compile");
    }

}// namespace

int main() {
    contractValidation();
    compileAndSample();
    batchBounds();
    roundTrip();
    return chrononmotion_test::report();
}
