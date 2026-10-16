#include "chrononmotion/chrononmotion.hpp"

#include "motion_check.hpp"

#include <cmath>
#include <stdexcept>

using namespace chrononmotion;
using namespace chrononmotion::motion;
using chrononmotion_test::check;
using chrononmotion_test::checkNear;
using chrononmotion_test::section;

namespace {

    void spatialPrimitives() {
        section("minimum plus one spatial primitives");

        Ray ray(Vector3(0.f, 0.f, 5.f), Vector3(0.f, 0.f, -1.f));
        Vector3 point;
        ray.at(2.f, point);
        checkNear(point, Vector3(0.f, 0.f, 3.f), 1e-5f, "ray at computes a point along the ray");

        Sphere sphere(Vector3(0.f, 0.f, 0.f), 1.f);
        check(ray.intersectsSphere(sphere), "ray intersects a sphere");
        ray.intersectSphere(sphere, point);
        checkNear(point, Vector3(0.f, 0.f, 1.f), 1e-5f, "ray returns the nearest sphere hit");

        Plane plane(Vector3(0.f, 0.f, 1.f), 0.f);
        check(ray.intersectsPlane(plane), "ray intersects a plane");
        ray.intersectPlane(plane, point);
        checkNear(point, Vector3(0.f, 0.f, 0.f), 1e-5f, "ray returns the plane hit");

        Box3 box(Vector3(-1.f, -1.f, -1.f), Vector3(1.f, 1.f, 1.f));
        check(ray.intersectsBox(box), "ray intersects an AABB");
        ray.intersectBox(box, point);
        checkNear(point, Vector3(0.f, 0.f, 1.f), 1e-5f, "ray returns the nearest AABB hit");

        const auto triangleHit = ray.intersectTriangle(Vector3(-1.f, -1.f, 0.f),
                                                       Vector3(1.f, -1.f, 0.f),
                                                       Vector3(0.f, 1.f, 0.f), false);
        check(triangleHit.has_value(), "ray intersects a triangle");
        checkNear(*triangleHit, Vector3(0.f, 0.f, 0.f), 1e-5f, "ray returns the triangle hit");

        Frustum frustum(
                Plane(Vector3(1.f, 0.f, 0.f), 1.f),
                Plane(Vector3(-1.f, 0.f, 0.f), 1.f),
                Plane(Vector3(0.f, 1.f, 0.f), 1.f),
                Plane(Vector3(0.f, -1.f, 0.f), 1.f),
                Plane(Vector3(0.f, 0.f, 1.f), 1.f),
                Plane(Vector3(0.f, 0.f, -1.f), 1.f));
        check(frustum.containsPoint(Vector3(0.f, 0.f, 0.f)), "frustum contains its center");
        check(!frustum.containsPoint(Vector3(2.f, 0.f, 0.f)), "frustum rejects an outside point");
        check(frustum.intersectsSphere(Sphere(Vector3(0.f, 0.f, 0.f), 0.5f)), "frustum intersects an inner sphere");
        check(!frustum.intersectsBox(Box3(Vector3(2.f, 2.f, 2.f), Vector3(3.f, 3.f, 3.f))),
              "frustum rejects an outside box");
    }

    void lightsEvaluateThroughMotionScene() {
        section("data-only light evaluation");

        MotionScene scene("lit", 30.f);
        scene.addNull(10, "Null_Main");
        Layer& target = scene.addContent(11, "Target", ContentRef::image("target", Vector2(1.f, 1.f)), 10);
        target.transform.position.set(0.f, 0.f, 0.f);

        DirectionalLight& directional = scene.addDirectionalLight(20, "Key", 10);
        directional.transform.position.set(0.f, 0.f, 5.f);
        directional.targetId = 11;
        directional.intensityTrack.add(0.f, 1.f);
        directional.intensityTrack.add(1.f, 3.f);

        PointLight& point = scene.addPointLight(21, "Fill", 10);
        point.range = 12.f;
        point.decay = 2.f;
        point.transform.position.set(2.f, 0.f, 1.f);
        point.rangeTrack.add(0.f, 12.f);
        point.rangeTrack.add(1.f, 20.f);

        AmbientLight& ambient = scene.addAmbientLight(22, "Ambient");
        ambient.color.setRGB(0.2f, 0.3f, 0.4f, ColorSpace::Linear);
        ambient.intensity = 0.25f;

        DirectionalLight& trackedDirection = scene.addDirectionalLight(23, "AnimatedDirection");
        trackedDirection.directionTrack.add(0.f, Vector3(0.f, -1.f, 0.f));
        trackedDirection.directionTrack.add(1.f, Vector3(1.f, 0.f, 0.f));

        scene.evaluate(15);
        const LightState& key = scene.lightState(20);
        check(key.evaluated && key.visible, "directional light is evaluated");
        checkNear(key.position, Vector3(0.f, 0.f, 5.f), 1e-5f, "light inherits its parent transform");
        checkNear(key.sample.intensity, 2.f, 1e-5f, "light intensity follows its track");
        directional.colorTrack.add(0.f, Color(0x0000ff));
        directional.colorTrack.add(1.f, Color(0xff0000));
        directional.positionTrack.add(0.f, Vector3(0.f, 0.f, 5.f));
        directional.positionTrack.add(1.f, Vector3(0.f, 2.f, 5.f));
        directional.rotationTrack.add(0.f, Vector3(0.f, 0.f, 0.f));
        directional.rotationTrack.add(1.f, Vector3(0.f, math::degToRad(30.f), 0.f));
        directional.castsShadow = true;
        directional.shadowBias = 0.01f;
        directional.shadowNormalBiasTrack.add(0.f, 0.f);
        directional.shadowNormalBiasTrack.add(1.f, 0.2f);
        directional.shadowRadiusTrack.add(0.f, 1.f);
        directional.shadowRadiusTrack.add(1.f, 3.f);
        scene.evaluate(15);
        checkNear(key.sample.color.r, 0.5f, 0.1f, "light color follows its track");
        checkNear(key.sample.local.position.y, 1.f, 1e-5f, "light position follows its track");
        checkNear(key.sample.local.rotation.y, math::degToRad(15.f), 1e-5f,
                  "light rotation follows its track");
        check(key.sample.castsShadow && key.sample.shadowBias == 0.01f,
              "shadow intent is carried as renderer-independent light state");
        checkNear(key.sample.shadowNormalBias, 0.1f, 1e-5f,
                  "shadow normal bias follows its track");
        checkNear(key.sample.shadowRadius, 2.f, 1e-5f,
                  "shadow radius follows its track");
        checkNear(key.direction, Vector3(0.f, -1.f, -5.f).normalize(), 1e-5f,
                  "directional target resolves through the layer graph");

        const LightState& animatedDirection = scene.lightState(23);
        checkNear(animatedDirection.direction, Vector3(1.f, -1.f, 0.f).normalize(), 1e-5f,
                  "root directional light follows its authored direction track");

        const LightState& fill = scene.lightState(21);
        checkNear(fill.sample.range, 16.f, 1e-5f, "point light range follows its track");
        checkNear(fill.sample.decay, 2.f, 1e-5f, "point light decay is preserved");
        check(scene.lightCount() == 4, "scene owns all light descriptors");

        const CompiledScene compiled = scene.compile();
        check(compiled.transforms.size() == 2, "compiled scene contains evaluated transforms");
        check(compiled.lights.size() == 4, "compiled scene contains evaluated lights");
        check(compiled.frame == 15 && compiled.fps == 30.f, "compiled scene preserves the time authority");
    }

    void nestedCompositionsCompileWithLocalTime() {
        section("nested composition");

        auto nested = std::make_shared<MotionScene>("nested", 24.f);
        Layer& child = nested->addContent(101, "NestedTitle", ContentRef::text("nested", {10.f, 10.f}));
        child.tracks.position.add(0.f, Vector3(0.f, 0.f, 0.f));
        child.tracks.position.add(1.f, Vector3(10.f, 0.f, 0.f));

        MotionScene outer("outer", 24.f);
        Layer& precomp = outer.addLayer(50, "Precomp");
        precomp.precomp = CompositionRef{nested, 24.f};
        precomp.transform.position.set(5.f, 0.f, 0.f);
        precomp.timeRemap.stretch = 2.f;
        outer.evaluate(12);
        const CompiledScene compiled = outer.compile();
        check(compiled.precomps.size() == 1, "nested composition is emitted as a compiled child");
        check(compiled.precomps.front().scene && compiled.precomps.front().scene->frame == 6,
              "nested composition receives deterministic local time");
        Vector3 nestedPosition;
        nestedPosition.setFromMatrixPosition(nested->worldMatrix(101));
        checkNear(nestedPosition, Vector3(2.5f, 0.f, 0.f), 1e-4f,
                  "nested composition evaluates its own timeline");
    }

    void materialDescriptorsAreRendererIndependent() {
        section("material descriptors");

        const MaterialDescriptor unlit = MaterialDescriptor::unlit(1, Color(0xff0000));
        check(unlit.kind == MaterialKind::Unlit, "unlit descriptor identifies its material kind");

        const MaterialDescriptor lambert = MaterialDescriptor::lambert(2, Color(0x00ff00));
        check(lambert.kind == MaterialKind::Lambert, "lambert descriptor identifies its material kind");

        const MaterialDescriptor pbr = MaterialDescriptor::pbr(3, Color(0x808080), 0.2f, 0.9f);
        check(pbr.kind == MaterialKind::PBR, "PBR descriptor identifies its material kind");
        checkNear(pbr.roughness, 0.2f, 1e-6f, "PBR roughness is preserved");
        checkNear(pbr.metalness, 0.9f, 1e-6f, "PBR metalness is preserved");

        const MaterialDescriptor emissive = MaterialDescriptor::emissive(4, Color(0x00ffff), 4.f);
        check(emissive.kind == MaterialKind::Emissive, "emissive descriptor identifies its material kind");
        checkNear(emissive.emissiveStrength, 4.f, 1e-6f, "emissive strength is preserved");

        bool rejected = false;
        try {
            MaterialDescriptor invalid = MaterialDescriptor::emissive(5, Color(0xffffff), -1.f);
            (void) invalid;
        } catch (const std::invalid_argument&) {
            rejected = true;
        }
        check(rejected, "negative emissive strength is rejected");

        auto geometry = BufferGeometry::create();
        CompiledScene compiled;
        compiled.addMaterial(emissive);
        compiled.addMesh(MeshDescriptor{1, "Glow", geometry, emissive});
        check(compiled.materials.size() == 1 && compiled.meshes.size() == 1,
              "compiled scene accepts data-only material and mesh descriptors");
    }

}// namespace

int main() {
    spatialPrimitives();
    lightsEvaluateThroughMotionScene();
    nestedCompositionsCompileWithLocalTime();
    materialDescriptorsAreRendererIndependent();
    return chrononmotion_test::report();
}
