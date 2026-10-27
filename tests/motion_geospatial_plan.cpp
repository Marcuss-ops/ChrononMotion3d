#include "motion_geospatial_internal.hpp"
#include "chrononmotion/motion/RenderPlanEmission.hpp"
#include "chrononmotion/motion/RenderPlanLowering.hpp"
#include <chronon3d/render_plan/render_plan.hpp>
#include <chronon3d/math/camera_pose.hpp>
namespace motion_geospatial_test {
using namespace chrononmotion;
using namespace chrononmotion::geospatial;
void testPlanPlacement() {

        // The content identity this test stands in for. A concrete typeface
        // asset is the content owner's business and never appears in this tree.
        constexpr const char* kStandInAsset = "font-asset";

        std::cout << "\n=== plan placement (through the canonical emitter) ===\n";
        constexpr int kFrameCount = 120;
        MapFlyoverSpec spec = romeSpec(kFrameCount, 30.f);
        // A stand-in relief plate: the real one is derived from a DEM for the
        // same window as the basemap, which is the whole reason it can be laid
        // over it without any placement of its own.
        spec.reliefAsset = "map/terrain-relief";
        const MapFlyoverBuilder builder(spec);

        MotionScene scene{"map_plan_test", 30.f};
        const MapSceneLayers ids = builder.buildScene(scene);

        // Stand-in content plans: this test pins space and time, and a real
        // typeface asset is the content owner's business.
        std::unordered_map<std::string, chronon3d::render_plan::LayerPlan> content;
        auto key = [](Layer::Id id) { return std::to_string(id); };
        auto text = [](const std::string& words) {
            chronon3d::render_plan::LayerPlan plan;
            plan.type = chronon3d::render_plan::LayerType::Text;
            plan.text = words;
            chronon3d::render_plan::LayerStylePlan style;
            style.font = kStandInAsset;
            style.font_size = 46.f;
            style.fill = "#FFFFFF";
            plan.style = style;
            return plan;
        };
        chronon3d::render_plan::LayerPlan basemap;
        basemap.type = chronon3d::render_plan::LayerType::Image;
        basemap.asset = spec.basemapAsset;
        content.emplace(key(ids.basemap), basemap);
        chronon3d::render_plan::LayerPlan relief;
        relief.type = chronon3d::render_plan::LayerType::Image;
        relief.asset = spec.reliefAsset;
        // A DEM hillshade is a mid-grey luminance plate: soft light turns it
        // into shading on the plate underneath instead of a grey wash over it.
        relief.blend_mode = chronon3d::BlendMode::SoftLight;
        content.emplace(key(ids.relief), relief);
        for (std::size_t i = 0; i < ids.markers.size(); ++i)
            content.emplace(key(ids.markers[i]), text("marker"));
        content.emplace(key(ids.pin), text("pin"));
        content.emplace(key(ids.title), text(spec.poiTitle));
        content.emplace(key(ids.subtitle), text(spec.poiSubtitle));

        motion::RenderPlanLoweringOptions options;
        options.width = 1920;
        options.height = 1080;
        options.jobId = "map_plan_test";
        options.outputPath = "map_plan_test.mp4";
        options.contentLayers = content;

        const chronon3d::render_plan::RenderPlan plan = motion::lowerToRenderPlan(scene, options);
        check(plan.canvas.duration.integral() == kFrameCount + 1,
              "the plan covers the authored clip");
        check(plan.layers.size() == ids.count(), "every authored layer reaches the plan");

        const auto findPlan = [&plan](const std::string& id) -> const chronon3d::render_plan::LayerPlan* {
            for (const auto& layer : plan.layers)
                if (layer.id == id) return &layer;
            return nullptr;
        };

        // A text layer's plan position is the canvas pixel it lands on: the
        // lowering adds the canvas centre, and the decoder takes it back out.
        for (std::size_t i = 0; i < ids.markers.size(); ++i) {
            const auto* marker = findPlan(key(ids.markers[i]));
            check(marker != nullptr, "marker " + std::to_string(i) + " is in the plan");
            if (marker == nullptr) continue;
            check(marker->type == chronon3d::render_plan::LayerType::Text, "a marker stays text");
            const Vector2 canvas = builder.mapPointToCanvas(spec.route[i], 0.f);
            checkNear(marker->position[0], canvas.x, 1e-3,
                      "marker " + std::to_string(i) + " is placed on its canvas column");
            checkNear(marker->position[1], canvas.y, 1e-3,
                      "marker " + std::to_string(i) + " is placed on its canvas row");
        }

        // The raster is not a text layer, so the lowering leaves the centre out
        // of its position; adding it back must recover the canvas pixel.
        const auto* plate = findPlan(key(ids.basemap));
        check(plate != nullptr, "the basemap is in the plan");
        if (plate != nullptr) {
            check(plate->type == chronon3d::render_plan::LayerType::Image, "the basemap stays an image");
            const Vector2 centre = builder.planePointToCanvas(Vector2{0.f, 0.f}, 0.f);
            checkNear(plate->position[0] + 960.f, centre.x, 1e-3,
                      "the raster's plan position recovers its canvas column");
            checkNear(plate->position[1] + 540.f, centre.y, 1e-3,
                      "the raster's plan position recovers its canvas row");
        }

        // The drape is not placed: it inherits the plate's transform channel for
        // channel, so a ridge can only land where the imagery under it has the
        // ridge. Anything else would be a second, disagreeing georeference.
        const auto trackOf = [](const chronon3d::render_plan::LayerPlan& layer,
                                const std::string& property) -> const chronon3d::render_plan::AnimationTrackPlan* {
            if (!layer.animation) return nullptr;
            for (const auto& track : layer.animation->tracks)
                if (track.property == property) return &track;
            return nullptr;
        };
        const auto sameChannel = [&trackOf](const chronon3d::render_plan::LayerPlan& a,
                                            const chronon3d::render_plan::LayerPlan& b,
                                            const std::string& property) {
            const auto* left = trackOf(a, property);
            const auto* right = trackOf(b, property);
            if (left == nullptr || right == nullptr) return false;
            if (left->keyframes.size() != right->keyframes.size()) return false;
            for (std::size_t i = 0; i < left->keyframes.size(); ++i)
                if (left->keyframes[i].frame != right->keyframes[i].frame ||
                    left->keyframes[i].value != right->keyframes[i].value) return false;
            return true;
        };
        const auto* drape = findPlan(key(ids.relief));
        check(drape != nullptr, "the relief drape is in the plan");
        if (drape != nullptr && plate != nullptr) {
            check(drape->type == chronon3d::render_plan::LayerType::Image,
                  "the relief drape stays an image");
            check(drape->asset == spec.reliefAsset, "the drape keeps the relief asset");
            check(sameChannel(*drape, *plate, "position"),
                  "the relief rides the plate's exact position channel");
            check(sameChannel(*drape, *plate, "scale"),
                  "the relief rides the plate's exact scale channel");
            check(drape->blend_mode.has_value() &&
                      *drape->blend_mode == chronon3d::BlendMode::SoftLight,
                  "the drape keeps its soft-light blend through the lowering");
            std::size_t plateIndex = plan.layers.size();
            std::size_t drapeIndex = plan.layers.size();
            for (std::size_t i = 0; i < plan.layers.size(); ++i) {
                if (plan.layers[i].id == key(ids.basemap)) plateIndex = i;
                if (plan.layers[i].id == key(ids.relief)) drapeIndex = i;
            }
            check(drapeIndex == plateIndex + 1,
                  "the drape is composited directly above the plate it was derived from");
        }

        check(motion::renderPlanEmissionProblem(plan).empty(),
              "the map plan is expressible by the canonical writer");
        const std::string json = motion::renderPlanToJson(plan);
        check(json.find("chronon.render-plan.v2") != std::string::npos,
              "the plan is written as a v2 document");
        check(json.find("\"position\"") != std::string::npos,
              "the plan carries the baked position tracks");
        check(json.find("soft_light") != std::string::npos,
              "the emitted plan names the drape's blend so a render cannot silently ignore it");
    }

void testDocumentaryPlan() {

        std::cout << "\n=== documentary layers reach the plan ===\n";
        MapFlyoverSpec spec = romeSpec(120, 30.f);
        spec.enablePinPulse = true;
        spec.areaOverlayAsset = "map/area";
        spec.leaderLineAsset = "map/leader";
        spec.crosshairAsset = "map/reticle";
        const MapFlyoverBuilder builder(spec);

        MotionScene scene{"documentary_plan", 30.f};
        const MapSceneLayers ids = builder.buildScene(scene);

        std::unordered_map<std::string, chronon3d::render_plan::LayerPlan> content;
        auto key = [](Layer::Id id) { return std::to_string(id); };
        auto text = [](const std::string& words) {
            chronon3d::render_plan::LayerPlan plan;
            plan.type = chronon3d::render_plan::LayerType::Text;
            plan.text = words;
            chronon3d::render_plan::LayerStylePlan style;
            style.font = "font-asset";
            style.font_size = 46.f;
            style.fill = "#FFFFFF";
            plan.style = style;
            return plan;
        };
        auto image = [](const std::string& asset) {
            chronon3d::render_plan::LayerPlan plan;
            plan.type = chronon3d::render_plan::LayerType::Image;
            plan.asset = asset;
            return plan;
        };
        content.emplace(key(ids.basemap), image(spec.basemapAsset));
        content.emplace(key(ids.area), image(spec.areaOverlayAsset));
        content.emplace(key(ids.leader_line), image(spec.leaderLineAsset));
        for (const Layer::Id id : ids.markers) content.emplace(key(id), text("marker"));
        content.emplace(key(ids.pin), text("pin"));
        content.emplace(key(ids.crosshair), image(spec.crosshairAsset));
        content.emplace(key(ids.title), text(spec.poiTitle));
        content.emplace(key(ids.subtitle), text(spec.poiSubtitle));

        motion::RenderPlanLoweringOptions options;
        options.width = 1920;
        options.height = 1080;
        options.jobId = "documentary_plan";
        options.outputPath = "documentary_plan.mp4";
        options.contentLayers = content;

        const chronon3d::render_plan::RenderPlan plan = motion::lowerToRenderPlan(scene, options);
        check(plan.layers.size() == ids.count(), "every documentary layer reaches the plan");
        check(motion::renderPlanEmissionProblem(plan).empty(),
              "the documentary plan is expressible by the canonical writer");

        // The ping is baked, not left as a loop the emitter would have to refuse:
        // the pin's scale track carries per-frame keys the renderer can play.
        const auto findPlan = [&plan](const std::string& id) -> const chronon3d::render_plan::LayerPlan* {
            for (const auto& layer : plan.layers)
                if (layer.id == id) return &layer;
            return nullptr;
        };
        const auto* pinPlan = findPlan(key(ids.pin));
        check(pinPlan != nullptr, "the pin is in the plan");
        if (pinPlan != nullptr) {
            bool hasScale = false;
            if (pinPlan->animation) {
                for (const auto& track : pinPlan->animation->tracks) {
                    if (track.property == "scale") hasScale = true;
                }
            }
            check(hasScale, "the pin's ping reaches the plan as a baked scale track");
        }
    }

void testCameraBackedPlatePlan() {

        std::cout << "\n=== camera-backed georeferenced plate ===\n";
        MapFlyoverSpec spec = romeSpec(12, 30.f);
        spec.poi = GeoCoord{41.8910, 12.4950, 50.0}; // deliberately off the plate centre
        spec.route = {spec.poi};
        spec.poiTitle.clear();
        spec.poiSubtitle.clear();
        spec.cameraBackedPlate = true;
        spec.startMapScale = spec.endMapScale = 1.f;
        spec.startMapPan = spec.endMapPan = Vector2{0.f, 0.f};
        const MapFlyoverBuilder builder(spec);
        MotionScene scene{"camera_backed_map", spec.fps};
        const MapSceneLayers ids = builder.buildScene(scene);
        builder.configureCameraBackedPlate(scene, 0.f, 30.f);

        motion::CameraRig rig{1, 55.f, 16.f / 9.f, 1.f, 10000.f};
        builder.rigCameraForPlate(rig, 1800.f, 1200.f);
        const motion::CameraPose arrival = rig.sample(spec.durationSeconds);
        check(!arrival.hasTarget && std::abs(arrival.orientation.w - 1.f) < 1e-6f,
              "the front-facing camera avoids a backend-specific rotated camera pose");
        check(arrival.position.z < 0.f,
              "the perspective camera sits on Chronon's visible negative-Z side of the plate");
        check(scene.findLayer(ids.basemap)->is3D,
              "the basemap is lowered as a perspective surface");
        scene.evaluate(builder.frameCount());
        const Matrix4& markerWorld = scene.state(ids.markers.front()).world;
        checkNear(markerWorld.elements[12], 0.f, 1e-3,
                  "the off-centre geographic POI is translated to the shared surface origin in X");
        checkNear(markerWorld.elements[13], 0.f, 1e-3,
                  "the off-centre geographic POI is translated to the shared surface origin in Y");

        motion::RenderPlanLoweringOptions options;
        options.width = 1920;
        options.height = 1080;
        options.jobId = "camera_backed_map";
        options.outputPath = "camera_backed_map.mp4";
        for (const Layer& layer : scene.layers()) {
            if (layer.isNull()) continue;
            chronon3d::render_plan::LayerPlan content;
            if (layer.content.kind == motion::ContentKind::Image) {
                content.type = chronon3d::render_plan::LayerType::Image;
                content.asset = layer.content.id;
            } else if (layer.content.kind == motion::ContentKind::Text) {
                content.type = chronon3d::render_plan::LayerType::Text;
                content.text = layer.content.id;
                chronon3d::render_plan::LayerStylePlan style;
                style.font = "test-font";
                style.font_size = 32.f;
                style.fill = "#FFFFFF";
                content.style = style;
            }
            options.contentLayers.emplace(std::to_string(layer.id), content);
        }
        options.cameraRig = &rig;
        const auto plan = motion::lowerToRenderPlan(scene, options);
        check(plan.camera.has_value(), "the emitted map plan carries a perspective camera");
        if (plan.camera) {
            const chronon3d::Vec3 angles{plan.camera->rotation_deg[0],
                                         plan.camera->rotation_deg[1],
                                         plan.camera->rotation_deg[2]};
            const auto loweredOrientation = chronon3d::math::camera_rotation_quat(angles);
            const motion::CameraPose startPose = rig.sample(0.f);
            const float orientationDot = loweredOrientation.x * startPose.orientation.x +
                loweredOrientation.y * startPose.orientation.y +
                loweredOrientation.z * startPose.orientation.z +
                loweredOrientation.w * startPose.orientation.w;
            checkNear(std::abs(orientationDot), 1.0, 1e-4,
                      "RenderPlan Euler angles preserve the rig's camera orientation");
        }
        check(plan.camera_samples.has_value() && !plan.camera_samples->empty(),
              "the emitted map plan carries resolved orbit and dolly camera samples");
        const auto plateLayer = std::find_if(plan.layers.begin(), plan.layers.end(), [&](const auto& layer) {
            return layer.id == std::to_string(ids.basemap);
        });
        check(plateLayer != plan.layers.end() && plateLayer->enable_3d,
              "the georeferenced raster reaches the plan as a camera-visible 3D layer");
        if (plateLayer != plan.layers.end() && plateLayer->animation) {
            const auto tilt = std::find_if(plateLayer->animation->tracks.begin(),
                                           plateLayer->animation->tracks.end(), [](const auto& track) {
                return track.property == "rotation";
            });
            check(tilt != plateLayer->animation->tracks.end() && !tilt->keyframes.empty() &&
                      std::abs(tilt->keyframes.back().value[0]) > 1.f,
                  "the map plate carries the authored camera-angle tilt through lowering");
        } else {
            check(false, "the map plate carries the authored camera-angle tilt through lowering");
        }
        check(motion::renderPlanEmissionProblem(plan).empty(),
              "the camera-backed map stays on the canonical render-plan path");

        motion::CameraRig oblique{2, 55.f, 16.f / 9.f, 1.f, 10000.f};
        oblique.setPosition(Vector3{-140.f, 530.f, -1800.f}).setTarget(Vector3{0.f, 0.f, 0.f});
        motion::CameraPose resolved = oblique.sample(0.f);
        resolved.hasTarget = false; // RenderPlan carries the resolved orientation, not target metadata.
        const auto obliquePlan = motion::lowerCameraPose(resolved, 1.f, 10000.f);
        const auto obliqueRendererRotation = chronon3d::math::camera_rotation_quat(
            chronon3d::Vec3{obliquePlan.rotation_deg[0], obliquePlan.rotation_deg[1],
                            obliquePlan.rotation_deg[2]});
        // RenderPlan mirrors depth and therefore conjugates the orientation
        // by S=diag(1,1,-1); this is not a plain inverse for oblique poses.
        Quaternion expectedOrientation = resolved.orientation;
        expectedOrientation.set(-expectedOrientation.x, -expectedOrientation.y,
                                expectedOrientation.z, expectedOrientation.w);
        const float obliqueDot = obliqueRendererRotation.x * expectedOrientation.x +
            obliqueRendererRotation.y * expectedOrientation.y +
            obliqueRendererRotation.z * expectedOrientation.z +
            obliqueRendererRotation.w * expectedOrientation.w;
        checkNear(std::abs(obliqueDot), 1.0, 1e-4,
                  "oblique look-at Euler conversion matches Chronon3D camera convention");
    }
} // namespace motion_geospatial_test
