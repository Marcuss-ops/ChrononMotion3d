// ChrononMotion — scene project-file tests: export -> import -> identical
// bytes and identical evaluated state, plus fail-closed rejection of every
// non-authoring shape and malformed document.

#include "motion_check.hpp"

#include "chrononmotion/motion/Motion.hpp"

#include <cmath>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>

namespace {

    using namespace chrononmotion;
    using namespace chrononmotion::motion;
    using chrononmotion_test::check;
    using chrononmotion_test::checkNear;

    bool throwsInvalid(const std::string& json) {
        try {
            (void)sceneFromJson(json);
        } catch (const std::invalid_argument&) {
            return true;
        } catch (...) {
            return false;
        }
        return false;
    }

    MotionScene richScene() {
        MotionScene scene("roundtrip", 30.f);

        Layer& root = scene.addNull(1, "Root");
        root.transform.position = Vector3(1.f, 2.f, 3.f);

        Layer& title = scene.addContent(2, "Title",
                                        ContentRef::measured("title", ContentKind::Text, "fp1",
                                                             Vector2(800.f, 120.f)),
                                        1);
        title.alive(0, 60);
        title.flat();
        title.castsShadow = true;
        title.shadowOpacity = 0.5f;
        title.tracks.position.add(0.f, Vector3(0.f, 0.f, 0.f), Easing::easeInOut());
        title.tracks.position.add(1.f, Vector3(6.f, 0.f, 0.f), Easing::overshoot());
        title.tracks.position.add(2.f, Vector3(6.f, 0.f, 0.f));
        title.tracks.position.loop(0.f, 2.f);
        title.tracks.opacity.addDiscrete(0.f, 0.f);
        title.tracks.opacity.add(1.f, 1.f, Easing::steps(4));
        title.tracks.opacity.posterize(12.f);
        title.tracks.anchor.add(0.f, Vector3(0.5f, 0.5f, 0.f));
        title.tracks.anchor.delay(0.25f);
        title.tracks.scale.add(0.f, Vector3(1.f, 1.f, 1.f));
        title.tracks.scale.window(0.f, 2.f);
        title.tracks.rotation.add(0.f, Vector3(0.f, 0.f, 0.f));
        title.tracks.rotation.influence(0.5f, 0.f);
        title.tracks.orientation.add(0.f, Quaternion(0.f, 0.f, 0.f, 1.f));
        title.tracks.orientation.smooth(0.1f, 3);
        title.tracks.position.link(1, TrackChannel::Position, 0.f);
        title.timeRemap.offsetFrames = 2.f;
        title.timeRemap.stretch = 0.5f;
        title.timeRemap.pingPong = true;
        title.timeRemapTrack.add(0.f, 0.f);
        title.timeRemapTrack.add(2.f, 4.f);
        scene.addNull(5, "Rig", 1);
        title.spaceSwitches.push_back(SpaceSwitchPoint{1.f, 5});

        // Aiming at the own parent is degenerate (the relative target is the
        // origin, i.e. the layer itself), so the constraints aim at the prop.
        ConstraintSpec aim;
        aim.kind = ConstraintKind::Aim;
        aim.targetId = 3;
        title.constraints.push_back(aim);

        ConstraintSpec limit;
        limit.kind = ConstraintKind::LimitPosition;
        limit.minimum = Vector3(-10.f, -10.f, -10.f);
        limit.maximum = Vector3(10.f, 10.f, 10.f);
        title.constraints.push_back(limit);

        ConstraintSpec damped;
        damped.kind = ConstraintKind::DampedAim;
        damped.targetId = 3;
        damped.damping = 4.f;
        damped.amountTrack = std::make_shared<Track<float>>();
        damped.amountTrack->add(0.f, 1.f);
        title.constraints.push_back(damped);

        DriverSpec wiggle;
        wiggle.kind = DriverKind::Wiggle;
        wiggle.property = DriverProperty::Position;
        wiggle.targetId = 3;
        wiggle.amplitude = 0.5f;
        wiggle.frequency = 2.f;
        title.drivers.push_back(wiggle);

        MeshAttachment mesh;
        mesh.assetId = "room";
        mesh.material.name = "clay";
        mesh.material.kind = MaterialKind::Lambert;
        mesh.material.baseColor = Color(0.2f, 0.2f, 0.2f);
        mesh.material.roughness = 0.9f;
        Layer& prop = scene.addMesh(3, "Prop", mesh, 1);
        prop.transform.position = Vector3(0.f, 0.f, -4.f);

        auto nested = std::make_shared<MotionScene>("nested", 30.f);
        nested->addNull(11, "NestedNull");
        Layer& holder = scene.addNull(4, "Holder", 1);
        holder.precomp.scene = nested;
        holder.precomp.localFps = 24.f;

        DirectionalLight& sun = scene.addDirectionalLight(21, "Sun");
        sun.targetId = 1;
        sun.color = Color(1.f, 0.95f, 0.9f);
        sun.intensity = 2.f;
        sun.transform.position = Vector3(5.f, 8.f, 6.f);
        sun.intensityTrack.add(0.f, 2.f);
        sun.intensityTrack.add(2.f, 3.f);

        PointLight& lamp = scene.addPointLight(22, "Lamp", 1);
        lamp.range = 12.f;
        lamp.decay = 2.f;
        lamp.rangeTrack.add(0.f, 12.f);
        lamp.decayTrack.add(0.f, 2.f);
        lamp.colorTrack.add(0.f, Color(1.f, 1.f, 1.f));

        SpotLight& spot = scene.addSpotLight(23, "Spot");
        spot.targetId = 2;
        spot.angle = 0.5f;
        spot.penumbra = 0.3f;
        spot.angleTrack.add(0.f, 0.5f);
        spot.penumbraTrack.add(0.f, 0.3f);
        spot.castsShadow = true;

        (void)scene.addAmbientLight(24, "Fill");
        return scene;
    }

    bool matricesEqual(const Matrix4& a, const Matrix4& b) {
        for (int i = 0; i < 16; ++i)
            if (a.elements[i] != b.elements[i]) return false;
        return true;
    }

    void roundTrip() {
        chrononmotion_test::section("scene round trip");
        MotionScene scene = richScene();
        const std::string exported = scene.toJson(0);
        MotionScene imported = sceneFromJson(exported);

        check(sceneToJson(imported, 0) == exported, "re-export is byte-identical");

        for (int frame : {0, 15, 30, 60}) {
            scene.evaluate(frame);
            imported.evaluate(frame);
            for (const Layer& layer : scene.layers()) {
                check(matricesEqual(scene.worldMatrix(layer.id), imported.worldMatrix(layer.id)),
                      "world matrix matches after round trip");
                check(scene.worldOpacity(layer.id) == imported.worldOpacity(layer.id),
                      "world opacity matches after round trip");
            }
        }
        check(imported.layerCount() == scene.layerCount(), "layer count survives");
        check(imported.lightCount() == scene.lightCount(), "light count survives");
        check(imported.findLayer(2)->content.measurementState("fp1") == MeasurementState::Current,
              "content fingerprint survives");

        MotionScene shuttered("shuttered", 24.f);
        shuttered.addNull(1, "Root");
        shuttered.setShutter({90.f, 6});
        MotionScene shutteredImported = sceneFromJson(shuttered.toJson(0));
        check(shutteredImported.hasShutter() &&
                  shutteredImported.shutter()->shutterAngleDegrees == 90.f &&
                  shutteredImported.shutter()->samples == 6,
              "scene shutter survives the project-file round trip");
        check(sceneToJson(shutteredImported, 0) == shuttered.toJson(0),
              "shutter re-export is byte-identical");
    }

    void rejections() {
        chrononmotion_test::section("scene rejections");
        const std::string exported = richScene().toJson(0);

        check(throwsInvalid(""), "empty document rejected");
        check(throwsInvalid("{}"), "empty object rejected");
        check(throwsInvalid("[1,2]"), "array document rejected");
        check(throwsInvalid(exported + " {}"), "trailing data rejected");
        check(throwsInvalid("{\"format\":\"chrononmotion.scene\",\"version\":1}"), "missing fields rejected");

        std::string wrongFormat = exported;
        wrongFormat.replace(wrongFormat.find("chrononmotion.scene"), 18, "chrononmotion.rig!");
        check(throwsInvalid(wrongFormat), "wrong format rejected");

        std::string version2 = exported;
        version2.replace(version2.find("\"version\":1"), 11, "\"version\":2");
        check(throwsInvalid(version2), "unsupported version rejected");

        std::string extraField = exported;
        extraField.replace(extraField.find("\"name\":\"roundtrip\""), 17, "\"name\":\"roundtrip\",\"zzz\":1");
        check(throwsInvalid(extraField), "unknown field rejected");

        std::string dupId = exported;
        dupId.replace(dupId.find("\"id\":3,"), 7, "\"id\":2,");
        check(throwsInvalid(dupId), "duplicate layer id rejected");

        std::string geometryMesh = exported;
        geometryMesh.replace(geometryMesh.find("\"geometry\":false"), 17, "\"geometry\":true");
        check(throwsInvalid(geometryMesh), "geometry-bearing mesh rejected");

        std::string nameOnly = exported;
        nameOnly.replace(nameOnly.find("\"inline\":true"), 13, "\"inline\":false");
        check(throwsInvalid(nameOnly), "name-only precomp rejected");

        std::string nullNumber = exported;
        nullNumber.replace(nullNumber.find("\"opacity\":1"), 11, "\"opacity\":null");
        check(throwsInvalid(nullNumber), "null number rejected");

        std::string badEasing = exported;
        badEasing.replace(badEasing.find("\"bezier\""), 8, "\"swoosh\"");
        check(throwsInvalid(badEasing), "unknown easing rejected");

        // A sampled constraint path is a record, not authoring data.
        MotionScene pathed("pathed", 30.f);
        pathed.addNull(1, "A");
        Layer& follower = pathed.addNull(2, "B");
        ConstraintSpec follow;
        follow.kind = ConstraintKind::FollowPath;
        follow.path = std::make_shared<MotionPath>(MotionPath::line(Vector3(0.f, 0.f, 0.f), Vector3(1.f, 0.f, 0.f)));
        follower.constraints.push_back(follow);
        check(throwsInvalid(pathed.toJson(0)), "constraint path rejected");

        check(throwsInvalid("{\"format\":\"chrononmotion.scene\",\"version\":1,\"name\":\"s\",\"fps\":24,"
                            "\"layers\":[],\"lights\":[],\"temporal_motion_blur\":{\"shutter_angle\":400,\"samples\":8}}"),
              "out-of-range scene shutter angle rejected");
        check(throwsInvalid("{\"format\":\"chrononmotion.scene\",\"version\":1,\"name\":\"s\",\"fps\":24,"
                            "\"layers\":[],\"lights\":[],\"temporal_motion_blur\":{\"shutter_angle\":180,\"samples\":1}}"),
              "single-sample scene shutter rejected");
        check(throwsInvalid("{\"format\":\"chrononmotion.scene\",\"version\":1,\"name\":\"s\",\"fps\":24,"
                            "\"layers\":[],\"lights\":[],\"temporal_motion_blur\":{\"shutter_angle\":180,\"samples\":8,\"zzz\":1}}"),
              "unknown scene shutter field rejected");
    }

}// namespace

int main() {
    roundTrip();
    rejections();
    return chrononmotion_test::report();
}
