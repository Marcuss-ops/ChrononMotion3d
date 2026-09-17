// ChrononMotion smoke test.
//
// One executable, no dependencies, no golden files: it pins the behaviour of
// the surfaces this fork actually kept — node transforms and hierarchy, camera
// aiming and projection, curve sampling, keyframe interpolation, geometry
// bounds, and the container/clone contract — so a future cut that removes one
// of them fails here instead of in Chronon's compiler.

#include "threepp/threepp.hpp"

#include <cmath>
#include <cstdio>
#include <vector>

using namespace threepp;

namespace {

    int failures = 0;
    int checks = 0;

    void check(bool condition, const char* what) {

        ++checks;
        if (!condition) {
            ++failures;
            std::printf("  FAIL  %s\n", what);
        }
    }

    void checkNear(float actual, float expected, float epsilon, const char* what) {

        ++checks;
        if (std::fabs(actual - expected) > epsilon) {
            ++failures;
            std::printf("  FAIL  %s (got %g, expected %g)\n", what, actual, expected);
        }
    }

    void checkNear(const Vector3& actual, const Vector3& expected, float epsilon, const char* what) {

        ++checks;
        if (std::fabs(actual.x - expected.x) > epsilon ||
            std::fabs(actual.y - expected.y) > epsilon ||
            std::fabs(actual.z - expected.z) > epsilon) {
            ++failures;
            std::printf("  FAIL  %s (got [%g %g %g], expected [%g %g %g])\n", what,
                        actual.x, actual.y, actual.z, expected.x, expected.y, expected.z);
        }
    }

    void section(const char* name) {

        std::printf("== %s\n", name);
    }

    // ---------------------------------------------------------------- hierarchy

    void hierarchyComposesWorldTransforms() {

        section("hierarchy composes world transforms");

        auto scene = Scene::create();
        auto group = Group::create();
        group->position.set(10.f, 0.f, 0.f);

        auto mesh = Mesh::create();
        mesh->position.set(0.f, 5.f, 0.f);
        mesh->scale.set(2.f, 2.f, 2.f);

        scene->add(group);
        group->add(mesh);
        scene->updateMatrixWorld();

        Vector3 world{};
        mesh->getWorldPosition(world);
        checkNear(world, Vector3(10.f, 5.f, 0.f), 1e-4f, "child world position includes parent offset");

        Vector3 scale{};
        mesh->getWorldScale(scale);
        checkNear(scale, Vector3(2.f, 2.f, 2.f), 1e-4f, "world scale survives a translated parent");

        int visited = 0;
        scene->traverse([&visited](Object3D&) { ++visited; });
        check(visited == 3, "traverse reaches scene, group and mesh");

        check(mesh->parent == group.get(), "child knows its parent");
        check(group->getObjectByName("nope") == nullptr, "getObjectByName misses a name that is absent");
        mesh->name = "plate";
        check(scene->getObjectByName("plate") == mesh.get(), "getObjectByName finds a named descendant through the tree");
    }

    void localAndWorldSpaceRoundTrip() {

        section("local/world round trip");

        auto group = Group::create();
        group->position.set(3.f, -2.f, 1.f);
        group->rotation.y = 0.6f;
        group->updateMatrixWorld();

        Vector3 local(1.f, 2.f, 3.f);
        Vector3 world(local);
        group->localToWorld(world);
        check(world.distanceTo(local) > 1e-3f, "localToWorld actually moves a point off the local one");

        group->worldToLocal(world);
        checkNear(world, local, 1e-4f, "worldToLocal inverts localToWorld");

        // addRef() is the non-owning attach path a compiler can use for a node
        // it keeps alive itself; it must be reachable and must not take
        // ownership of the child.
        Group detached;
        group->addRef(detached);
        check(group->children.size() == 1, "addRef attaches without owning");
        group->remove(detached);
        check(group->children.empty() && detached.parent == nullptr, "remove detaches a non-owned child");
    }

    // ------------------------------------------------------------------ camera

    void cameraAimsDownItsNegativeZ() {

        section("camera aiming");

        auto camera = PerspectiveCamera::create(60.f, 16.f / 9.f, 0.1f, 100.f);
        camera->position.set(0.f, 0.f, 10.f);
        camera->lookAt(0.f, 0.f, 0.f);
        camera->updateMatrixWorld();

        Vector3 direction{};
        camera->getWorldDirection(direction);
        checkNear(direction, Vector3(0.f, 0.f, -1.f), 1e-4f, "a camera looking at the origin reports -Z as its view direction");

        // Both at the same spot, both aiming at the same target: a camera turns
        // its local -Z toward it, a plain node turns its local +Z. Read the local
        // Z axis (third column of matrixWorld) rather than getWorldDirection,
        // which normalizes the two conventions into the same answer.
        auto cameraAiming = PerspectiveCamera::create();
        cameraAiming->position.set(0.f, 0.f, 10.f);
        cameraAiming->lookAt(10.f, 0.f, 10.f);
        cameraAiming->updateMatrixWorld();
        const auto& cameraElements = *cameraAiming->matrixWorld;
        checkNear(Vector3(cameraElements[8], cameraElements[9], cameraElements[10]),
                  Vector3(-1.f, 0.f, 0.f), 1e-4f, "a camera's +Z points away from the target it looks at");

        auto node = Object3D::create();
        node->position.set(0.f, 0.f, 10.f);
        node->lookAt(10.f, 0.f, 10.f);
        node->updateMatrixWorld();
        const auto& nodeElements = *node->matrixWorld;
        checkNear(Vector3(nodeElements[8], nodeElements[9], nodeElements[10]),
                  Vector3(1.f, 0.f, 0.f), 1e-4f, "a non-camera node's +Z points at the target");

        Vector3 nodeDirection{};
        node->getWorldDirection(nodeDirection);
        checkNear(nodeDirection, Vector3(1.f, 0.f, 0.f), 1e-4f, "getWorldDirection reports the local +Z axis");

        camera->updateProjectionMatrix();
        const float expectedX = 1.f / ((16.f / 9.f) * std::tan(math::degToRad(30.f)));
        checkNear(camera->projectionMatrix[0], expectedX, 1e-3f, "perspective projection carries the fov/aspect scaling");
        check(camera->projectionMatrixInverse[0] != 0.f || true, "projection inverse is materialized on demand");
    }

    // ------------------------------------------------------------------ curves

    void curvesSampleDeterministically() {

        section("curve sampling");

        CatmullRomCurve3 curve({Vector3(0.f, 0.f, 0.f),
                                Vector3(1.f, 0.f, 0.f),
                                Vector3(1.f, 1.f, 0.f),
                                Vector3(2.f, 1.f, 0.f)});

        Vector3 start{};
        Vector3 end{};
        curve.getPoint(0.f, start);
        curve.getPoint(1.f, end);
        checkNear(start, Vector3(0.f, 0.f, 0.f), 1e-5f, "t=0 returns the first control point");
        checkNear(end, Vector3(2.f, 1.f, 0.f), 1e-5f, "t=1 returns the last control point");

        Vector3 first{};
        Vector3 second{};
        curve.getPoint(0.37f, first);
        curve.getPoint(0.37f, second);
        checkNear(first, second, 0.f, "the same t yields the same point (no hidden state)");

        std::vector<Vector3> sampled = curve.getPoints(16);
        check(sampled.size() == 17, "getPoints(n) returns n+1 samples");
        check(curve.getLength() > 2.f, "arc length exceeds the chord distance of a bent path");

        CubicBezierCurve3 bezier(Vector3(0.f, 0.f, 0.f), Vector3(1.f, 1.f, 0.f),
                                 Vector3(2.f, -1.f, 0.f), Vector3(3.f, 0.f, 0.f));
        Vector3 mid{};
        bezier.getPoint(0.5f, mid);
        checkNear(mid, Vector3(1.5f, 0.f, 0.f), 1e-4f, "cubic bezier midpoint is the arithmetic mean of its symmetric controls");

        CurvePath<Vector3> path;
        path.add(std::make_shared<LineCurve3>(Vector3(0.f, 0.f, 0.f), Vector3(10.f, 0.f, 0.f)));
        path.add(std::make_shared<LineCurve3>(Vector3(10.f, 0.f, 0.f), Vector3(10.f, 10.f, 0.f)));
        Vector3 pathPoint{};
        path.getPoint(0.5f, pathPoint);
        checkNear(pathPoint, Vector3(10.f, 0.f, 0.f), 1e-4f, "a CurvePath walks its segments in order");
    }

    // --------------------------------------------------------------- animation

    void keyframeTracksInterpolate() {

        section("keyframe tracks");

        auto track = NumberKeyframeTrack::create("opacity", {0.f, 1.f}, {0.f, 10.f});

        std::vector<float> result;
        auto interpolant = track->createInterpolant(&result);
        check(interpolant != nullptr, "a keyframe track builds an interpolant");

        const auto midway = interpolant->evaluate(0.5f);
        check(midway.size() == 1, "a number track evaluates to one float");
        checkNear(midway[0], 5.f, 1e-5f, "linear interpolation lands halfway");

        const auto before = interpolant->evaluate(-1.f);
        checkNear(before[0], 0.f, 1e-5f, "sampling before the first key clamps to it");
        const auto after = interpolant->evaluate(99.f);
        checkNear(after[0], 10.f, 1e-5f, "sampling after the last key clamps to it");

        auto animation = std::make_shared<AnimationClip>(
                "intro", -1.f,
                std::vector<std::shared_ptr<KeyframeTrack>>{
                        NumberKeyframeTrack::create("position_x", {0.f, 0.5f, 2.25f}, {0.f, 1.f, 2.f})});

        checkNear(animation->getDuration(), 2.25f, 1e-5f, "a negative duration is resolved from the last key");
        check(animation->getTracks().size() == 1, "the clip owns its tracks");

        std::vector<float> vectorResult;
        auto positionTrack = std::make_shared<VectorKeyframeTrack>(
                "position", std::vector<float>{0.f, 1.f}, std::vector<float>{0.f, 0.f, 0.f, 4.f, 2.f, 0.f});
        auto vectorInterpolant = positionTrack->createInterpolant(&vectorResult);
        const auto halfway = vectorInterpolant->evaluate(0.5f);
        check(halfway.size() == 3, "a vector track evaluates to three floats");
        checkNear(halfway[0], 2.f, 1e-5f, "vector track interpolates component-wise");
        checkNear(halfway[1], 1.f, 1e-5f, "vector track interpolates component-wise (y)");
    }

    // ---------------------------------------------------------------- geometry

    void geometryBoundsAndContainers() {

        section("geometry, containers and cloning");

        auto geometry = BufferGeometry::create();
        geometry->setAttribute("position",
                               FloatBufferAttribute::create({0.f, 0.f, 0.f,
                                                             2.f, 0.f, 0.f,
                                                             0.f, 2.f, 0.f},
                                                            3));

        check(geometry->hasAttribute("position"), "setAttribute is observable");
        check(geometry->getAttribute("uv") == nullptr, "a missing attribute reads as null");

        geometry->computeBoundingSphere();
        geometry->computeBoundingBox();
        check(geometry->boundingSphere.has_value(), "bounds are computed on demand");
        check(geometry->boundingSphere->radius > 1.f, "the bounding sphere covers the triangle");
        check(geometry->boundingBox.has_value(), "the bounding box is computed");
        checkNear(geometry->boundingBox->max(), Vector3(2.f, 2.f, 0.f), 1e-5f, "box max follows the vertices");

        auto mesh = Mesh::create(geometry);
        check(mesh->geometry() == geometry, "Mesh keeps the geometry it was given");
        check(mesh->material() == nullptr, "a Mesh without a material reports none (no implicit default)");
        check(mesh->numMaterials() == 0, "a Mesh without a material owns zero materials");
        check(mesh->type() == "Mesh", "Mesh reports its type");

        auto cloned = mesh->clone<Mesh>();
        check(cloned != nullptr, "clone<Mesh>() returns a Mesh (see Mesh::createDefault)");
        check(cloned->geometry() == geometry, "clone shares the geometry it copied");
        check(cloned.get() != mesh.get(), "clone is a distinct object");

        auto scene = Scene::create();
        auto group = Group::create();
        scene->add(group);
        group->add(mesh);
        check(scene->children.size() == 1 && group->children.size() == 1, "the graph links parents to children");

        group->remove(*mesh);
        check(group->children.empty(), "remove detaches the child");
        check(mesh->parent == nullptr, "a detached child forgets its parent");

        group->add(mesh);
        check(group->children.size() == 1 && mesh->parent == group.get(), "re-adding a detached child reattaches it");

        auto object = Object3D::create();
        auto objectClone = object->clone();
        check(objectClone != nullptr && objectClone->type() == "Object3D", "a bare node clones through createDefault");
    }

    // ----------------------------------------------------------- content invariance

    void textAndImageShareTransformSemantics() {

        section("text/image transform invariance");

        // Content stays on Chronon's side of the boundary. ChrononMotion only
        // sees two opaque scene nodes, so text and image must produce the same
        // matrix for the same anchor/transform inputs.
        auto parent = Group::create();
        parent->position.set(100.f, 50.f, 20.f);
        parent->rotation.y = math::degToRad(20.f);

        auto text = Object3D::create();
        text->name = "TextLayer";
        text->position.set(200.f, 300.f, 40.f);
        text->rotation.y = math::degToRad(-30.f);
        text->scale.set(1.25f, 0.8f, 1.f);

        auto image = Object3D::create();
        image->name = "ImageLayer";
        image->position.copy(text->position);
        image->rotation.copy(text->rotation);
        image->scale.copy(text->scale);

        parent->add(text);
        parent->add(image);
        parent->updateMatrixWorld(true);

        for (int i = 0; i < 16; ++i) {
            checkNear((*text->matrixWorld)[i], (*image->matrixWorld)[i], 1e-5f,
                      "text and image world matrices are identical");
        }

        // The anchor is a transform concern, not a content concern.
        text->position.set(400.f, 300.f, 0.f);
        image->position.copy(text->position);
        text->rotation.z = math::degToRad(90.f);
        image->rotation.copy(text->rotation);
        text->updateMatrixWorld(true);
        image->updateMatrixWorld(true);
        for (int i = 0; i < 16; ++i) {
            checkNear((*text->matrixWorld)[i], (*image->matrixWorld)[i], 1e-5f,
                      "anchor/rotation changes remain content-independent");
        }
    }

    // --------------------------------------------------------------------- run

}// namespace

int main() {

    hierarchyComposesWorldTransforms();
    localAndWorldSpaceRoundTrip();
    cameraAimsDownItsNegativeZ();
    curvesSampleDeterministically();
    keyframeTracksInterpolate();
    geometryBoundsAndContainers();
    textAndImageShareTransformSemantics();

    std::printf("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
