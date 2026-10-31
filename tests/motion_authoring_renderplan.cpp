#include "chrononmotion/chrononmotion.hpp"
#include "chrononmotion/motion/RenderPlanEmission.hpp"
#include "chrononmotion/motion/RenderPlanLowering.hpp"
#include "chrononmotion/motion/LightRigRenderPlan.hpp"
#include "chrononmotion/motion/HdriRenderPlan.hpp"
#include "chrononmotion/motion/RigBaking.hpp"
#include "gltf_motion_fixture.hpp"
#include "hdri_fixture.hpp"
#include "motion_check.hpp"
#include <chronon3d/render_plan/render_plan.hpp>
#include <nlohmann/json.hpp>
#include <array>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>
using namespace chrononmotion;
using namespace chrononmotion::motion;
using chrononmotion_test::check;
using chrononmotion_test::checkNear;
using chrononmotion_test::section;
namespace {
template<class Function>
bool throwsInvalid(Function&& function) {
    try { function(); }
    catch (const std::invalid_argument&) { return true; }
    return false;
}
bool sameLoweredPlan(const chronon3d::render_plan::RenderPlan& left,
                     const chronon3d::render_plan::RenderPlan& right) {
    using namespace chronon3d::render_plan;
    if (left.schema != right.schema || left.job_id != right.job_id ||
        left.canvas.width != right.canvas.width || left.canvas.height != right.canvas.height ||
        left.canvas.fps != right.canvas.fps || left.canvas.duration != right.canvas.duration ||
        left.output.path != right.output.path || left.layers.size() != right.layers.size())
        return false;
    for (std::size_t layerIndex = 0; layerIndex < left.layers.size(); ++layerIndex) {
        const auto& a = left.layers[layerIndex];
        const auto& b = right.layers[layerIndex];
        if (a.id != b.id || a.type != b.type || a.asset != b.asset || a.source != b.source ||
            a.position != b.position || a.scale != b.scale || a.rotation != b.rotation ||
            a.enable_3d != b.enable_3d || a.opacity != b.opacity ||
            a.animation.has_value() != b.animation.has_value())
            return false;
        if (!a.animation) continue;
        if (a.animation->tracks.size() != b.animation->tracks.size()) return false;
        for (std::size_t trackIndex = 0; trackIndex < a.animation->tracks.size(); ++trackIndex) {
            const auto& trackA = a.animation->tracks[trackIndex];
            const auto& trackB = b.animation->tracks[trackIndex];
            if (trackA.property != trackB.property || trackA.easing != trackB.easing ||
                trackA.keyframes.size() != trackB.keyframes.size())
                return false;
            for (std::size_t keyIndex = 0; keyIndex < trackA.keyframes.size(); ++keyIndex)
                if (trackA.keyframes[keyIndex].frame != trackB.keyframes[keyIndex].frame ||
                    trackA.keyframes[keyIndex].value != trackB.keyframes[keyIndex].value)
                    return false;
        }
    }
    return true;
}

} // namespace
void testTypedChronon3dLowering() {
    using namespace chronon3d::render_plan;
    section("typed Chronon3D v2/v3 RenderPlan lowering");
    MotionScene scene("plan", 30.f);
    scene.addNull(10, "Controller");
    auto& layer = scene.addContent(11, "Promo", ContentRef::image("opaque-promo", Vector2(100.f, 50.f)), 10);
    layer.transform.position.set(10.f, 20.f, 0.f);
    layer.tracks.position.add(0.f, Vector3(10.f, 20.f, 0.f));
    layer.tracks.position.add(1.f, Vector3(40.f, 50.f, 0.f));
    scene.evaluate(0);
    RenderPlanLoweringOptions options;
    options.width = 640;
    options.height = 360;
    options.schema = chronon3d::render_plan::kRenderPlanSchemaV2;
    chronon3d::render_plan::LayerPlan content;
    content.type = chronon3d::render_plan::LayerType::Image;
    content.asset = "opaque-promo";
    content.color = {0.f, 0.f, 0.f, 0.f};
    content.size = {100.f, 50.f};
    content.size_dimensions = 2;
    options.contentLayers.emplace("11", content);
    const auto priorPose = scene.state(11).local.position;
    auto plan = lowerToRenderPlan(scene, options);
    check(scene.lastEvaluatedFrame() == 0 && scene.state(11).local.position == priorPose,
          "lowering restores the original frame after dense offline sampling");
    check(plan.layers.size() == 2 && plan.canvas.duration.integral() == 31,
          "typed lowering emits controller/content and inclusive composition duration");
    check(plan.schema == chronon3d::render_plan::kRenderPlanSchemaV2,
          "typed lowering selects the canonical V2 schema");
    check(!plan.layers[1].parent.has_value(), "world composition is flattened exactly once");
    check(plan.layers[1].animation && plan.layers[1].animation->tracks.size() == 4,
          "world motion is baked into v2 transform and opacity tracks");
    check(plan.layers[1].animation->tracks[0].property == "position" &&
          plan.layers[1].animation->tracks[0].keyframes[30].value[0] == 40.f,
          "the final baked keyframe contains the evaluated world position");
    check(plan.layers[1].animation->tracks.front().keyframes.size() == 31,
          "dense lowering emits a frame-resolved key for every frame");
    checkNear(plan.layers[1].position[0], 10.f, 1e-5f,
              "image plan position preserves authored world coordinates");
    check(plan.layers[1].enable_3d,
          "lowering explicitly opts three-component transform vectors into 3D projection");
    checkNear(plan.layers[1].position[0], 10.f, 1e-6f,
              "fixed lowering golden: initial world X position");
    checkNear(plan.layers[1].position[1], 20.f, 1e-6f,
              "fixed lowering golden: initial world Y position");
    check(plan.layers[1].animation->tracks[0].keyframes.front().value ==
              std::vector<float>{10.f, 20.f, 0.f},
          "fixed lowering golden: first baked position key");
    check(plan.layers[1].animation->tracks[0].keyframes.back().value ==
              std::vector<float>{40.f, 50.f, 0.f},
          "fixed lowering golden: final baked position key");
    const auto repeatedV2 = lowerToRenderPlan(scene, options);
    check(sameLoweredPlan(plan, repeatedV2),
          "typed V2 lowering repeats every emitted transform and keyframe bit-exactly");

    options.schema = chronon3d::render_plan::kRenderPlanSchemaV3;
    auto v3 = lowerToRenderPlan(scene, options);
    check(v3.schema == chronon3d::render_plan::kRenderPlanSchemaV3,
          "typed lowering supports Chronon3D V3");
    const auto repeatedV3 = lowerToRenderPlan(scene, options);
    check(sameLoweredPlan(v3, repeatedV3),
          "typed V3 lowering repeats every emitted transform and keyframe bit-exactly");

    MotionScene reflectedImageScene("reflected image plane", 30.f);
    auto& reflectedImage = reflectedImageScene.addContent(
        81, "wall image", ContentRef::image("wall-photo", Vector2(100.f, 60.f)));
    reflectedImage.scaled(Vector3(-1.f, 1.f, 1.f)).alive(0, 0);
    RenderPlanLoweringOptions reflectedImageOptions = options;
    reflectedImageOptions.jobId = "reflected_image_plane";
    reflectedImageOptions.contentLayers.clear();
    LayerPlan reflectedImageContent;
    reflectedImageContent.type = LayerType::Image;
    reflectedImageContent.asset = "wall-photo";
    reflectedImageContent.color = {0.f, 0.f, 0.f, 0.f};
    reflectedImageContent.size = {100.f, 60.f};
    reflectedImageContent.size_dimensions = 2;
    reflectedImageOptions.contentLayers.emplace("81", reflectedImageContent);
    const auto reflectedImagePlan = lowerToRenderPlan(reflectedImageScene, reflectedImageOptions);
    check(reflectedImagePlan.layers.size() == 1 && reflectedImagePlan.layers[0].scale[0] < 0.f,
          "typed lowering preserves a reflected image plane using signed X scale");

    chronon3d::render_plan::MotionBlurPlan scenePlanBlur;
    scenePlanBlur.shutter_angle = 180.f;
    scenePlanBlur.samples = 8;
    RenderPlanLoweringOptions blurOptions = options;
    blurOptions.temporalMotionBlur = scenePlanBlur;
    const auto blurred = lowerToRenderPlan(scene, blurOptions);
    check(blurred.temporal_motion_blur.has_value() &&
              blurred.temporal_motion_blur->shutter_angle == 180.f &&
              blurred.temporal_motion_blur->samples == 8,
          "lowering passes an authored V3 temporal motion blur through to the RenderPlan");
    check(!v3.temporal_motion_blur.has_value(),
          "lowering without an authored blur emits no temporal motion blur");
    check(throwsInvalid([&] {
              RenderPlanLoweringOptions v2Blur = options;
              v2Blur.schema = chronon3d::render_plan::kRenderPlanSchemaV2;
              v2Blur.temporalMotionBlur = scenePlanBlur;
              (void)lowerToRenderPlan(scene, v2Blur);
          }),
          "temporal motion blur requires RenderPlan V3");
    check(throwsInvalid([&] {
              RenderPlanLoweringOptions badSamples = blurOptions;
              badSamples.temporalMotionBlur->samples = 1;
              (void)lowerToRenderPlan(scene, badSamples);
          }),
          "temporal motion blur rejects fewer than two samples");
    check(throwsInvalid([&] {
              RenderPlanLoweringOptions badAngle = blurOptions;
              badAngle.temporalMotionBlur->shutter_angle = 400.f;
              (void)lowerToRenderPlan(scene, badAngle);
          }),
          "temporal motion blur rejects a shutter angle above 360 degrees");

    // Scene-authored shutter (docs/BOUNDARY_C3D.md C6): the scene is the
    // single authored unit; lowering maps it 1:1 onto temporal_motion_blur.
    scene.setShutter({180.f, 8});
    const auto sceneShuttered = lowerToRenderPlan(scene, options);
    check(sceneShuttered.temporal_motion_blur.has_value() &&
              sceneShuttered.temporal_motion_blur->shutter_angle == 180.f &&
              sceneShuttered.temporal_motion_blur->samples == 8,
          "scene-authored shutter lowers to the V3 temporal motion blur");
    check(throwsInvalid([&] {
              RenderPlanLoweringOptions conflict = blurOptions;
              (void)lowerToRenderPlan(scene, conflict);
          }),
          "temporal motion blur authored both in the scene and the options is rejected");
    check(throwsInvalid([&] {
              RenderPlanLoweringOptions v2SceneBlur = options;
              v2SceneBlur.schema = chronon3d::render_plan::kRenderPlanSchemaV2;
              (void)lowerToRenderPlan(scene, v2SceneBlur);
          }),
          "scene-authored temporal motion blur requires RenderPlan V3");
    check(throwsInvalid([&] { scene.setShutter({0.f, 8}); }),
          "scene shutter rejects a zero shutter angle");
    check(throwsInvalid([&] { scene.setShutter({180.f, 1}); }),
          "scene shutter rejects fewer than two samples");
    scene.clearShutter();
    check(!lowerToRenderPlan(scene, options).temporal_motion_blur.has_value(),
          "clearing the scene shutter removes the temporal motion blur");

    MotionScene videoScene("time remap", 30.f);
    auto& video = videoScene.addContent(31, "video", ContentRef::video("source-video", {640.f, 360.f}));
    video.lifetime = FrameRange::between(0, 3);
    video.timeRemap.stretch = 2.f;
    RenderPlanLoweringOptions videoOptions;
    videoOptions.width = 640;
    videoOptions.height = 360;
    videoOptions.schema = chronon3d::render_plan::kRenderPlanSchemaV3;
    chronon3d::render_plan::LayerPlan videoPlan;
    videoPlan.type = chronon3d::render_plan::LayerType::Video;
    videoPlan.source = "source-video";
    videoOptions.contentLayers.emplace("31", videoPlan);
    const auto slowedVideo = lowerToRenderPlan(videoScene, videoOptions);
    check(slowedVideo.layers[0].time_remap && slowedVideo.layers[0].time_remap->speed == 0.5f,
          "static Motion stretch lowers to the equivalent positive RenderPlan playback speed");
    const std::string slowedJson = renderPlanToJson(slowedVideo);
    const auto parsedSlowedJson = nlohmann::json::parse(slowedJson);
    check(parsedSlowedJson.at("layers").at(0).at("time_remap").at("speed") == 0.5f,
          "V3 emitter writes static video time-remap speed");

    auto inlineMeshPlan = slowedVideo;
    auto& meshLayer = inlineMeshPlan.layers[0];
    meshLayer.id = "procedural-triangle";
    meshLayer.type = chronon3d::render_plan::LayerType::Mesh;
    meshLayer.source.clear();
    meshLayer.time_remap.reset();
    meshLayer.animation.reset();
    chronon3d::render_plan::InlineMeshPlan triangle;
    triangle.vertices = {
        {{{-1.f, -1.f, 0.f}}, {{0.f, 0.f, 1.f}}, {{0.f, 0.f}}},
        {{{ 1.f, -1.f, 0.f}}, {{0.f, 0.f, 1.f}}, {{1.f, 0.f}}},
        {{{ 0.f,  1.f, 0.f}}, {{0.f, 0.f, 1.f}}, {{0.5f, 1.f}}},
    };
    triangle.indices = {0, 1, 2};
    triangle.metallic_roughness_texture_asset = "textures/orm.png";
    triangle.material = chronon3d::render_plan::InlineMeshMaterialPlan{};
    triangle.material->base_color = std::array<float, 4>{0.1f, 0.6f, 0.3f, 1.f};
    meshLayer.inline_mesh = triangle;
    check(renderPlanEmissionProblem(inlineMeshPlan).empty(),
          "the producer accepts bounded indexed inline mesh geometry with a typed material");
    const std::string inlineMeshJson = renderPlanToJson(inlineMeshPlan);
    const auto parsedInlineMesh = nlohmann::json::parse(inlineMeshJson);
    check(parsedInlineMesh.at("layers").at(0).at("inline_mesh").at("vertices").size() == 3 &&
          parsedInlineMesh.at("layers").at(0).at("inline_mesh").at("indices") == nlohmann::json{0, 1, 2},
          "the emitter preserves procedural mesh vertices and triangle indices");
    check(parsedInlineMesh.at("layers").at(0).at("inline_mesh").at("metallic_roughness_texture") == "textures/orm.png",
          "the emitter preserves the inline mesh metallic-roughness image reference");
    auto multiLightPlan = inlineMeshPlan;
    auto editorialRig = lightRigPresets::make("editorial_key_rim_fill");
    lightRigPresets::animateRotation(editorialRig, 0.f, 3.f / 30.f, {}, {0.f, .2f, .1f});
    appendLightRigToRenderPlan(multiLightPlan, editorialRig, 30.f, 4);
    check(multiLightPlan.layers.size() == inlineMeshPlan.layers.size() + 3 &&
          renderPlanEmissionProblem(multiLightPlan).empty(),
          "editorial light rig lowers to three valid V3 light layers");
    const auto multiLightPayload = renderPlanToJson(multiLightPlan);
    const auto multiLightJson = nlohmann::json::parse(multiLightPayload);
    check(multiLightJson.at("layers").size() == multiLightPlan.layers.size() &&
          multiLightJson.at("layers").at(1).at("light").at("kind") == "spot" &&
          multiLightJson.at("layers").at(1).at("animation").at("tracks").size() > 0 &&
          renderPlanToJson(multiLightPlan) == multiLightPayload,
          "animated multi-light payload emits deterministically through the V3 producer");
    const auto& emittedBaseColor = parsedInlineMesh.at("layers").at(0).at("inline_mesh").at("material").at("base_color");
    check(emittedBaseColor.size() == 4,
          "the emitter preserves all four inline mesh base color components");
    if (emittedBaseColor.size() == 4) {
        checkNear(emittedBaseColor.at(0).get<float>(), 0.1f, 1e-6f,
                  "the emitter preserves inline mesh base color red");
        checkNear(emittedBaseColor.at(1).get<float>(), 0.6f, 1e-6f,
                  "the emitter preserves inline mesh base color green");
        checkNear(emittedBaseColor.at(2).get<float>(), 0.3f, 1e-6f,
                  "the emitter preserves inline mesh base color blue");
        checkNear(emittedBaseColor.at(3).get<float>(), 1.f, 1e-6f,
                  "the emitter preserves inline mesh base color alpha");
    }
    check(renderPlanToJson(inlineMeshPlan) == inlineMeshJson,
          "inline mesh emission is byte deterministic");
    auto invalidMeshPlan = inlineMeshPlan;
    invalidMeshPlan.layers[0].inline_mesh->indices[2] = 3;
    check(!renderPlanEmissionProblem(invalidMeshPlan).empty() &&
          throwsInvalid([&] { (void)renderPlanToJson(invalidMeshPlan); }),
          "out-of-range inline mesh indices fail closed before emission");
    invalidMeshPlan = inlineMeshPlan;
    invalidMeshPlan.layers[0].inline_mesh->metallic_roughness_texture_asset = "../unsafe.png";
    check(!renderPlanEmissionProblem(invalidMeshPlan).empty(),
          "inline mesh metallic-roughness texture rejects mounted-root traversal");

    MotionScene proceduralMeshScene("procedural mesh canary", 30.f);
    auto proceduralTriangle = BufferGeometry::create();
    proceduralTriangle->setAttribute("position", FloatBufferAttribute::create(
        {-1.f, -1.f, 0.f, 1.f, -1.f, 0.f, 0.f, 1.f, 0.f}, 3));
    proceduralTriangle->setAttribute("normal", FloatBufferAttribute::create(
        {0.f, 0.f, 1.f, 0.f, 0.f, 1.f, 0.f, 0.f, 1.f}, 3));
    proceduralTriangle->setAttribute("uv", FloatBufferAttribute::create(
        {0.f, 0.f, 1.f, 0.f, 0.5f, 1.f}, 2));
    proceduralTriangle->setIndex(std::vector<unsigned int>{0, 1, 2});
    const MaterialDescriptor proceduralMaterial = MaterialDescriptor::lambert(9, Color(0x336699));
    proceduralMeshScene.addMesh(71, "triangle", MeshAttachment{
        .geometry = proceduralTriangle, .material = proceduralMaterial})
        .alive(0, 1);
    RenderPlanLoweringOptions proceduralMeshOptions;
    proceduralMeshOptions.width = 64;
    proceduralMeshOptions.height = 64;
    proceduralMeshOptions.schema = chronon3d::render_plan::kRenderPlanSchemaV3;
    chronon3d::render_plan::LayerPlan proceduralMeshContent;
    proceduralMeshContent.type = chronon3d::render_plan::LayerType::Mesh;
    proceduralMeshOptions.contentLayers.emplace("71", proceduralMeshContent);
    const auto loweredProceduralMesh = lowerToRenderPlan(proceduralMeshScene, proceduralMeshOptions);
    check(loweredProceduralMesh.layers.size() == 1 && loweredProceduralMesh.layers[0].inline_mesh &&
          loweredProceduralMesh.layers[0].inline_mesh->vertices.size() == 3 &&
          loweredProceduralMesh.layers[0].inline_mesh->material.has_value(),
          "MotionScene geometry and Lambert material lower into a native RenderPlan inline mesh");
    Color expectedPlanMaterialColor = proceduralMaterial.baseColor;
    expectedPlanMaterialColor.convertLinearToSRGB();
    checkNear(loweredProceduralMesh.layers[0].inline_mesh->material->base_color->at(0),
              expectedPlanMaterialColor.r, 1e-6f,
              "mesh lowering converts the native material color to the RenderPlan sRGB contract");
    checkNear(loweredProceduralMesh.layers[0].inline_mesh->material->base_color->at(3),
              proceduralMaterial.opacity, 1e-6f,
              "mesh lowering preserves the native material opacity");
    const auto emittedProceduralMesh = nlohmann::json::parse(renderPlanToJson(loweredProceduralMesh));
    check(emittedProceduralMesh.at("layers").at(0).at("inline_mesh").at("indices") ==
              nlohmann::json{0, 1, 2},
          "procedural MotionScene geometry reaches the emitted RenderPlan without a second adapter");
    MotionScene pbrMeshScene("procedural pbr mesh contract", 30.f);
    MaterialDescriptor pbrDescriptor = MaterialDescriptor::pbr(10, Color(0x8090a0), 0.25f, 0.8f);
    pbrDescriptor.doubleSided = true;
    pbrDescriptor.alphaCutoff = 0.25f;
    pbrDescriptor.validate();
    check(pbrDescriptor.doubleSided && pbrDescriptor.roughness == 0.25f &&
          pbrDescriptor.metalness == 0.8f,
          "PBR descriptor preserves the agreed roughness/metalness and double-sided state");
    auto invalidPbrDescriptor = pbrDescriptor;
    invalidPbrDescriptor.roughness = std::numeric_limits<float>::infinity();
    check(throwsInvalid([&] { invalidPbrDescriptor.validate(); }),
          "PBR descriptor rejects non-finite roughness");
    MeshAttachment pbrAttachment{.geometry = proceduralTriangle, .material = pbrDescriptor};
    pbrAttachment.baseColorTextureAsset = "textures/paint.png";
    pbrMeshScene.addMesh(73, "pbr triangle", std::move(pbrAttachment)).alive(0, 1);
    proceduralMeshOptions.contentLayers.clear();
    proceduralMeshOptions.contentLayers.emplace("73", proceduralMeshContent);
    const auto loweredPbrMesh = lowerToRenderPlan(pbrMeshScene, proceduralMeshOptions);
    check(loweredPbrMesh.layers.size() == 1 && loweredPbrMesh.layers[0].inline_mesh &&
          loweredPbrMesh.layers[0].inline_mesh->material.has_value(),
          "PBR MotionScene meshes remain expressible in the compact RenderPlan material contract");
    if (loweredPbrMesh.layers.size() == 1 && loweredPbrMesh.layers[0].inline_mesh &&
        loweredPbrMesh.layers[0].inline_mesh->material) {
        check(loweredPbrMesh.layers[0].inline_mesh->material->metallic == pbrDescriptor.metalness &&
              loweredPbrMesh.layers[0].inline_mesh->material->roughness == pbrDescriptor.roughness &&
              loweredPbrMesh.layers[0].inline_mesh->material->double_sided == true &&
              loweredPbrMesh.layers[0].inline_mesh->material->alpha_cutoff == pbrDescriptor.alphaCutoff,
              "PBR lowering preserves exact factors and alpha cutoff in RenderPlan");
        checkNear(loweredPbrMesh.layers[0].inline_mesh->material->shininess,
                  1.f + (1.f - pbrDescriptor.roughness) * 1023.f, 1e-3f,
                  "PBR lowering maps roughness into RenderPlan shininess approximation");
        const auto pbrJson = nlohmann::json::parse(renderPlanToJson(loweredPbrMesh));
        check(renderPlanToJson(loweredPbrMesh) == renderPlanToJson(loweredPbrMesh),
              "PBR RenderPlan serialization is byte-stable across repeated writes");
        const auto& emittedLayer = pbrJson.at("layers").at(0);
        check(emittedLayer.at("asset") == "textures/paint.png" &&
              (!emittedLayer.contains("input_color_space") ||
               emittedLayer.at("input_color_space") == "srgb"),
              "PBR base-color texture identity reaches the V3 mesh asset slot");
        const auto& emittedMaterial = pbrJson.at("layers").at(0).at("inline_mesh").at("material");
        check(std::abs(emittedMaterial.at("metallic").get<float>() - pbrDescriptor.metalness) < 1e-6f &&
              std::abs(emittedMaterial.at("roughness").get<float>() - pbrDescriptor.roughness) < 1e-6f &&
              emittedMaterial.at("double_sided") == true &&
              std::abs(emittedMaterial.at("alpha_cutoff").get<float>() - pbrDescriptor.alphaCutoff) < 1e-6f,
              "PBR exact properties survive deterministic RenderPlan serialization");
    }
    MotionScene animatedMeshScene("animated mesh contract", 30.f);
    Layer& animatedMesh = animatedMeshScene.addMesh(
        72, "animated triangle", MeshAttachment{.geometry = proceduralTriangle});
    animatedMesh.alive(0, 1);
    animatedMesh.tracks.position.add(0.f, Vector3(0.f, 0.f, 0.f));
    animatedMesh.tracks.position.add(1.f / 30.f, Vector3(1.f, 0.f, 0.f));
    proceduralMeshOptions.jobId = "animated_mesh_contract";
    proceduralMeshOptions.contentLayers.clear();
    proceduralMeshOptions.contentLayers.emplace("72", proceduralMeshContent);
    check(throwsInvalid([&] { (void)lowerToRenderPlan(animatedMeshScene, proceduralMeshOptions); }),
          "animated inline mesh transforms fail closed instead of emitting decoder-invalid tracks");

    auto chalkPathPlan = slowedVideo;
    auto& chalkLayer = chalkPathPlan.layers[0];
    chalkLayer.id = "chalk-stroke";
    chalkLayer.type = chronon3d::render_plan::LayerType::Shape;
    chalkLayer.source.clear();
    chalkLayer.time_remap.reset();
    chalkLayer.animation.reset();
    chalkLayer.size = {640.f, 360.f};
    chalkLayer.size_dimensions = 2;
    chronon3d::render_plan::ShapeContentPlan chalkPath;
    chalkPath.type = chronon3d::render_plan::ShapeContentTypePlan::Path;
    chalkPath.path = {
        {chronon3d::render_plan::ShapePathCommandTypePlan::MoveTo, {20.f, 30.f}},
        {chronon3d::render_plan::ShapePathCommandTypePlan::CubicTo, {120.f, 80.f},
            {40.f, 5.f}, {90.f, 110.f}},
    };
    chronon3d::render_plan::StrokeStyle chalk;
    chalk.color = "#F6F1D5";
    chalk.width = 8.f;
    chalk.cap = "round";
    chalk.join = "round";
    chalkPath.stroke = chalk;
    chronon3d::render_plan::PathOperatorPlan trim;
    trim.kind = chronon3d::render_plan::PathOperatorKindPlan::Trim;
    trim.trim_animation = chronon3d::render_plan::AnimationTrackPlan{};
    trim.trim_animation->property = "trim";
    trim.trim_animation->keyframes = {
        {chronon3d::Frame{0}, {0.f}}, {chronon3d::Frame{12}, {1.f}}};
    chalkPath.operators.push_back(trim);
    chalkLayer.shape = chalkPath;
    check(renderPlanEmissionProblem(chalkPathPlan).empty(),
          "a cubic chalk path with animated trim is accepted by the shared RenderPlan boundary");
    const std::string chalkJson = renderPlanToJson(chalkPathPlan);
    const auto emittedChalk = nlohmann::json::parse(chalkJson);
    check(emittedChalk.at("layers").at(0).at("shape").at("path").at(1).at("type") == "cubic_to" &&
          emittedChalk.at("layers").at(0).at("shape").at("path").at(1).at("control2") ==
              nlohmann::json{90.f, 110.f},
          "the shape writer preserves cubic endpoints and both Bézier controls");
    check(emittedChalk.at("layers").at(0).at("shape").at("stroke").at("width") == 8.f &&
          emittedChalk.at("layers").at(0).at("shape").at("operators").at(0).at("params")
              .at("animation").at("keyframes").size() == 2,
          "the shape writer preserves chalk stroke style and the trim reveal track");
    auto unsupportedShapePlan = chalkPathPlan;
    unsupportedShapePlan.layers[0].shape->operators[0].kind =
        chronon3d::render_plan::PathOperatorKindPlan::Repeater;
    check(!renderPlanEmissionProblem(unsupportedShapePlan).empty(),
          "unsupported path operators are refused instead of silently discarded");
    const auto checkProceduralShapeEmission = [&](chronon3d::render_plan::ShapeContentPlan shape,
                                                   const char* expectedType) {
        auto plan = chalkPathPlan;
        plan.layers[0].shape = std::move(shape);
        const std::string problem = renderPlanEmissionProblem(plan);
        check(problem.empty(), problem.empty() ? "procedural shape is accepted" : problem.c_str());
        if (problem.empty()) {
            const auto json = nlohmann::json::parse(renderPlanToJson(plan));
            check(json.at("layers").at(0).at("shape").at("type") == expectedType,
                  "procedural shape kind survives typed-plan JSON emission");
        }
    };
    chronon3d::render_plan::ShapeContentPlan proceduralShape;
    proceduralShape.type = chronon3d::render_plan::ShapeContentTypePlan::Line;
    proceduralShape.path = chalkPath.path;
    proceduralShape.stroke = chalk;
    checkProceduralShapeEmission(proceduralShape, "line");
    proceduralShape = {};
    proceduralShape.type = chronon3d::render_plan::ShapeContentTypePlan::Rect;
    proceduralShape.fill_color = std::array<float, 4>{0.1f, 0.2f, 0.3f, 1.f};
    checkProceduralShapeEmission(proceduralShape, "rect");
    proceduralShape = {};
    proceduralShape.type = chronon3d::render_plan::ShapeContentTypePlan::RoundedRect;
    proceduralShape.corner_radius = std::array<float, 4>{8.f, 12.f, 8.f, 12.f};
    proceduralShape.fill_color = std::array<float, 4>{0.1f, 0.2f, 0.3f, 1.f};
    checkProceduralShapeEmission(proceduralShape, "rounded_rect");
    proceduralShape = {};
    proceduralShape.type = chronon3d::render_plan::ShapeContentTypePlan::Ellipse;
    checkProceduralShapeEmission(proceduralShape, "ellipse");
    proceduralShape = {};
    proceduralShape.type = chronon3d::render_plan::ShapeContentTypePlan::Star;
    proceduralShape.points = 7;
    checkProceduralShapeEmission(proceduralShape, "star");
    proceduralShape = {};
    proceduralShape.type = chronon3d::render_plan::ShapeContentTypePlan::Polygon;
    proceduralShape.points = 6;
    checkProceduralShapeEmission(proceduralShape, "polygon");
    proceduralShape = {};
    proceduralShape.type = chronon3d::render_plan::ShapeContentTypePlan::Arrow;
    checkProceduralShapeEmission(proceduralShape, "arrow");
    proceduralShape.arrow_direction = "left";
    auto unrepresentableArrow = chalkPathPlan;
    unrepresentableArrow.layers[0].shape = proceduralShape;
    check(!renderPlanEmissionProblem(unrepresentableArrow).empty(),
          "arrow direction is refused because the consumer schema has no matching field");
    proceduralShape = {};
    proceduralShape.type = chronon3d::render_plan::ShapeContentTypePlan::Arc;
    proceduralShape.stroke = chalk;
    checkProceduralShapeEmission(proceduralShape, "arc");
    proceduralShape = {};
    proceduralShape.type = chronon3d::render_plan::ShapeContentTypePlan::Grid;
    proceduralShape.stroke = chalk;
    checkProceduralShapeEmission(proceduralShape, "grid");
    proceduralShape = {};
    proceduralShape.type = chronon3d::render_plan::ShapeContentTypePlan::DotGrid;
    proceduralShape.fill_color = std::array<float, 4>{1.f, 1.f, 1.f, 1.f};
    checkProceduralShapeEmission(proceduralShape, "dot_grid");
    proceduralShape = {};
    proceduralShape.type = chronon3d::render_plan::ShapeContentTypePlan::DeviceFrame;
    proceduralShape.fill_color = std::array<float, 4>{0.08f, 0.09f, 0.1f, 1.f};
    proceduralShape.device_frame_kind = chronon3d::render_plan::DeviceFrameKindPlan::Browser;
    checkProceduralShapeEmission(proceduralShape, "device_frame");

    using FieldPlan = chronon3d::render_plan::ShapeContentPlan;
    using FieldType = chronon3d::render_plan::ShapeContentTypePlan;
    chronon3d::render_plan::ShapeContentPlan fieldShape;
    fieldShape.type = FieldType::Rect;
    fieldShape.field = chronon3d::graphics::Field2D{
        .generator = chronon3d::graphics::Field2DGenerator::Fractal,
        .seed = 73, .frequency = 4.f, .octaves = 5};
    fieldShape.field_ramp = chronon3d::graphics::GradientDefinition::linear(
        {0.f, 0.5f}, {1.f, 0.5f}, {{0.f, {0.1f, 0.05f, 0.2f, 0.2f}},
                                  {0.5f, {0.6f, 0.2f, 0.8f, 0.8f}},
                                  {1.f, {0.9f, 0.8f, 1.f, 1.f}}});
    fieldShape.field_render_scale = 2;
    fieldShape.field_drift = glm::vec2{0.02f, -0.01f};
    auto fieldPlan = chalkPathPlan;
    fieldPlan.layers[0].shape = fieldShape;
    const std::string fieldProblem = renderPlanEmissionProblem(fieldPlan);
    check(fieldProblem.empty(), fieldProblem.empty() ? "native Field2D fill is accepted" : fieldProblem.c_str());
    if (fieldProblem.empty()) {
        const auto json = nlohmann::json::parse(renderPlanToJson(fieldPlan));
        const auto& fill = json.at("layers").at(0).at("shape").at("fill");
        check(fill.at("type") == "field", "Field2D type stays inside the closed fill payload");
        check(fill.at("field").at("seed") == 73, "Field2D source seed stays inside the closed fill payload");
        check(fill.at("field_ramp").at("color_stops").size() == 3, "Field2D ramp stays inside the closed fill payload");
        check(fill.at("field_render_scale") == 2, "Field2D render scale stays inside the closed fill payload");
        const auto& drift = fill.at("field_drift");
        check(drift.is_array() && drift.size() == 2 &&
              std::abs(drift.at(0).get<float>() - 0.02f) < 1e-6f &&
              std::abs(drift.at(1).get<float>() + 0.01f) < 1e-6f,
              "Field2D drift stays inside the closed fill payload");
    }
    chronon3d::render_plan::ShapeContentPlan meshShape;
    meshShape.type = FieldType::Rect;
    meshShape.gradient_mesh = chronon3d::graphics::GradientMesh::four_corner(
        {0.1f, 0.1f, 0.2f, 1.f}, {0.8f, 0.1f, 0.3f, 1.f},
        {0.1f, 0.7f, 0.8f, 1.f}, {0.9f, 0.8f, 1.f, 1.f});
    auto meshFillPlan = chalkPathPlan;
    meshFillPlan.layers[0].shape = meshShape;
    const std::string meshProblem = renderPlanEmissionProblem(meshFillPlan);
    check(meshProblem.empty(), meshProblem.empty() ? "native GradientMesh fill is accepted" : meshProblem.c_str());
    if (meshProblem.empty()) {
        const auto json = nlohmann::json::parse(renderPlanToJson(meshFillPlan));
        check(json.at("layers").at(0).at("shape").at("fill").at("mesh").at("nodes").size() == 4,
              "GradientMesh nodes are preserved in the renderer's nested fill envelope");
    }

    auto chalkDustPlan = chalkPathPlan;
    chalkDustPlan.canvas.duration = chronon3d::Frame{13};
    chalkDustPlan.layers[0].duration_frames = chronon3d::Frame{13};
    auto dustLayer = slowedVideo.layers[0];
    dustLayer.id = "chalk-dust";
    dustLayer.type = chronon3d::render_plan::LayerType::Particles;
    dustLayer.source.clear();
    dustLayer.asset.clear();
    dustLayer.time_remap.reset();
    dustLayer.animation.reset();
    dustLayer.duration_frames = chronon3d::Frame{13};
    chronon3d::ParticleEmitterParams dust;
    dust.count = 48;
    dust.seed = 0xC4A1U;
    dust.emitter_size = {12.f, 4.f};
    dust.velocity_min = {-2.f, -15.f};
    dust.velocity_max = {2.f, -4.f};
    dust.lifetime_min = 0.2f;
    dust.lifetime_max = 0.8f;
    dust.size_min = 0.5f;
    dust.size_max = 2.5f;
    dust.gravity = 8.f;
    dust.turbulence = 0.15f;
    dust.shape = chronon3d::EmitterShape::Path;
    dust.path_id = "chalk-stroke";
    dust.path_points = {{0.f, 0.f}, {24.f, 6.f}, {48.f, 0.f}};
    dust.sprite_asset = "assets/chalk_dust.png";
    dust.sprite_scale_min = 0.8f;
    dust.sprite_scale_max = 1.2f;
    dust.rotation_min = -0.5f;
    dust.rotation_max = 0.5f;
    dust.angular_velocity_min = -1.f;
    dust.angular_velocity_max = 1.f;
    dust.blend_mode = chronon3d::BlendMode::Add;
    dust.color_ramp = chronon3d::graphics::GradientDefinition::linear(
        {0.f, 0.f}, {1.f, 0.f}, {{0.f, {1.f, 0.95f, 0.75f, 0.85f}},
                                {1.f, {0.8f, 0.75f, 0.6f, 0.f}}});
    dustLayer.particle_emitter = dust;
    chalkDustPlan.layers.push_back(dustLayer);
    check(renderPlanEmissionProblem(chalkDustPlan).empty(),
          "the deterministic chalk-particle payload is accepted by RenderPlan v3");
    const std::string dustJson = renderPlanToJson(chalkDustPlan);
    if (const char* canaryPath = std::getenv("CHRONONMOTION_RENDER_PLAN_CANARY")) {
        auto canaryPlan = chalkDustPlan;
        canaryPlan.layers[1].particle_emitter->sprite_asset.clear();
        std::ofstream canaryFile(canaryPath, std::ios::binary);
        if (!canaryFile || !(canaryFile << renderPlanToJson(canaryPlan)))
            throw std::runtime_error("cannot write requested RenderPlan consumer canary");
    }
    const auto emittedDust = nlohmann::json::parse(dustJson).at("layers").at(1).at("particle_emitter");
    check(emittedDust.at("count") == 48 && emittedDust.at("seed") == 0xC4A1U &&
          emittedDust.at("shape") == "path" && emittedDust.at("path_id") == "chalk-stroke" &&
          emittedDust.at("path_points").size() == 3,
          "particle count, seed, path and resolved points reach the renderer payload");
    check(emittedDust.at("color_ramp").size() == 2 &&
          emittedDust.at("sprite_asset") == "assets/chalk_dust.png" &&
          emittedDust.at("angular_velocity") == nlohmann::json{-1.f, 1.f},
          "particle color, sprite and rotation ranges survive emission");
    check(renderPlanToJson(chalkDustPlan) == dustJson,
          "seeded particle emission is byte deterministic");
    auto legacyDustPlan = slowedVideo;
    legacyDustPlan.schema = chronon3d::render_plan::kRenderPlanSchemaV2;
    legacyDustPlan.layers[0].type = chronon3d::render_plan::LayerType::Particles;
    legacyDustPlan.layers[0].source.clear();
    legacyDustPlan.layers[0].time_remap.reset();
    legacyDustPlan.layers[0].animation.reset();
    legacyDustPlan.layers[0].particle_emitter = dust;
    check(!renderPlanEmissionProblem(legacyDustPlan).empty(),
          "V3-only particle path and sprite fields are rejected under the V2 schema");

    auto lightPlan = slowedVideo;
    lightPlan.layers[0].id = "chalk-light";
    lightPlan.layers[0].type = chronon3d::render_plan::LayerType::Light;
    lightPlan.layers[0].source.clear();
    lightPlan.layers[0].time_remap.reset();
    lightPlan.layers[0].animation.reset();
    lightPlan.layers[0].light = chronon3d::render_plan::LightLayerPlan{};
    check(renderPlanEmissionProblem(lightPlan).empty(),
          "a typed scene light is accepted by the common RenderPlan emitter");
    const auto emittedLight = nlohmann::json::parse(renderPlanToJson(lightPlan))
                                  .at("layers").at(0).at("light");
    check(emittedLight.at("radius") == 256.f && emittedLight.at("intensity") == 1.f &&
          emittedLight.at("color") == "#FFFFFF",
          "light radius, intensity and RGB color are preserved in the consumer's hex contract");
    auto invalidLightPlan = lightPlan;
    invalidLightPlan.layers[0].light->color[0] = 0.123f;
    check(!renderPlanEmissionProblem(invalidLightPlan).empty(),
          "light colors that the hexadecimal consumer contract would quantize are refused");

    auto hdriPlan = slowedVideo;
    const auto probe = decodeHdriRadiance(chrononmotion_test::constantRadianceHdr());
    attachHdriEnvironmentToRenderPlan(hdriPlan, probe, "assets/studio.hdri-env");
    check(renderPlanEmissionProblem(hdriPlan).empty(),
          "HDRI reader's diffuse SH coefficients can be attached to V3 RenderPlan");
    const auto emittedEnvironment = nlohmann::json::parse(renderPlanToJson(hdriPlan)).at("environment_light");
    check(emittedEnvironment.at("probe_asset") == "assets/studio.hdri-env" &&
          emittedEnvironment.at("irradiance_sh").size() == 27,
          "HDRI asset reference and all diffuse SH coefficients survive producer emission");
    if (const char* canaryPath = std::getenv("CHRONONMOTION_HDRI_RENDER_CANARY")) {
        auto renderPlan = hdriPlan;
        // Make the runtime canary visually sensitive on every RGB channel;
        // the source fixture is intentionally red-only and can hide lighting
        // regressions when rendered against a green mesh.
        auto renderProbe = probe;
        renderProbe.irradianceSH.fill(0.f);
        for (std::size_t channel = 0; channel < 3; ++channel)
            renderProbe.irradianceSH[channel] = 11.428123f;
        renderPlan.environment_light->irradiance_sh = renderProbe.irradianceSH;
        renderPlan.job_id = "motion-hdri-runtime-canary";
        renderPlan.canvas.width = 64;
        renderPlan.canvas.height = 64;
        auto& layer = renderPlan.layers.at(0);
        layer.id = "hdri-lit-triangle";
        layer.type = chronon3d::render_plan::LayerType::Mesh;
        layer.source.clear();
        layer.animation.reset();
        layer.time_remap.reset();
        layer.enable_3d = true;
        layer.color = std::array<float, 4>{0.8f, 0.7f, 0.6f, 1.f};
        auto visibleTriangle = triangle;
        visibleTriangle.metallic_roughness_texture_asset.clear();
        layer.inline_mesh = std::move(visibleTriangle);
        const std::filesystem::path planPath(canaryPath);
        const auto assetRoot = planPath.parent_path() / "motion-hdri-assets";
        std::filesystem::create_directories(assetRoot / "assets");
        std::ofstream planFile(planPath, std::ios::binary);
        if (!planFile || !(planFile << renderPlanToJson(renderPlan)))
            throw std::runtime_error("cannot write requested HDRI RenderPlan canary");
        const auto payload = renderProbe.serialize();
        std::ofstream assetFile(assetRoot / "assets/studio.hdri-env", std::ios::binary);
        if (!assetFile || !assetFile.write(
                reinterpret_cast<const char*>(payload.data()),
                static_cast<std::streamsize>(payload.size())))
            throw std::runtime_error("cannot write requested HDRI binary asset canary");
    }
    auto unsafeHdriPlan = hdriPlan;
    unsafeHdriPlan.environment_light->probe_asset = "../outside.hdri-env";
    check(!renderPlanEmissionProblem(unsafeHdriPlan).empty(),
          "HDRI probe paths reject traversal before writing RenderPlan JSON");

    auto safeAreaPlan = slowedVideo;
    safeAreaPlan.layers[0].safe_insets = std::array<float, 4>{80.f, 60.f, 220.f, 60.f};
    check(renderPlanEmissionProblem(safeAreaPlan).empty(),
          "V3 safe-area insets are a supported typed adapter field");
    const auto safeAreaJson = nlohmann::json::parse(renderPlanToJson(safeAreaPlan));
    check(safeAreaJson.at("layers").at(0).at("safe_insets") ==
              nlohmann::json{{"top", 80.f}, {"right", 60.f}, {"bottom", 220.f}, {"left", 60.f}},
          "V3 emitter preserves all four safe-area insets by name");
    auto invalidSafeAreaPlan = safeAreaPlan;
    invalidSafeAreaPlan.layers[0].safe_insets->at(0) = -1.f;
    check(!renderPlanEmissionProblem(invalidSafeAreaPlan).empty(),
          "negative safe-area insets fail closed at the emission boundary");
    auto legacySafeAreaPlan = safeAreaPlan;
    legacySafeAreaPlan.schema = chronon3d::render_plan::kRenderPlanSchemaV2;
    check(!renderPlanEmissionProblem(legacySafeAreaPlan).empty(),
          "safe-area insets fail closed when attached to the V2 schema");

    chronon3d::render_plan::LayerPlan semanticText;
    semanticText.id = "semantic";
    semanticText.type = chronon3d::render_plan::LayerType::Text;
    semanticText.text = "Hi future";
    semanticText.style = chronon3d::render_plan::LayerStylePlan{};
    semanticText.style->font = "fonts/roboto"; // content identity only; no extension to load
    semanticText.style->font_size = 32.f;
    semanticText.style->fill = "#FFFFFF";
    chronon3d::render_plan::TextSelectorPlan selector;
    selector.unit = "word";
    selector.shape = "square";
    selector.amount.keyframes = {{chronon3d::Frame{0}, {0.f}},
                                 {chronon3d::Frame{8}, {100.f}}};
    chronon3d::render_plan::TextAnimatorPlan animator;
    animator.id = "word-highlight";
    animator.selectors.push_back(selector);
    chronon3d::render_plan::AnimationTrackPlan textOpacity;
    textOpacity.property = "opacity";
    textOpacity.keyframes = {{chronon3d::Frame{0}, {0.f}},
                             {chronon3d::Frame{8}, {1.f}}};
    chronon3d::render_plan::AnimationTrackPlan textColor;
    textColor.property = "fill_color";
    textColor.keyframes = {{chronon3d::Frame{0}, {1.f, .7f, 0.f, 1.f}}};
    animator.properties = {textOpacity, textColor};
    semanticText.text_animators.push_back(animator);
    auto semanticPlan = slowedVideo;
    semanticPlan.schema = chronon3d::render_plan::kRenderPlanSchemaV3;
    semanticPlan.layers = {semanticText};
    check(renderPlanEmissionProblem(semanticPlan).empty(),
          "V3 semantic text selectors and property tracks are emit-able");
    const auto semanticJson = nlohmann::json::parse(renderPlanToJson(semanticPlan));
    check(semanticJson.at("layers").at(0).at("text_animators").at(0)
              .at("selectors").at(0).at("unit") == "word" &&
          semanticJson.at("layers").at(0).at("text_animators").at(0)
              .at("properties").at(1).at("property") == "fill_color",
          "V3 emitter preserves text unit selection and authored property order");
    auto v2TextAnimatorPlan = semanticPlan;
    v2TextAnimatorPlan.schema = chronon3d::render_plan::kRenderPlanSchemaV2;
    check(!renderPlanEmissionProblem(v2TextAnimatorPlan).empty(),
          "text animators fail closed under the frozen V2 contract");
    check(renderPlanToJson(slowedVideo) == slowedJson,
          "time-remap JSON emission is byte-for-byte deterministic");

    auto bridgePlan = slowedVideo;
    auto& bridgeLayer = bridgePlan.layers.front();
    bridgeLayer.time_remap.reset();
    bridgeLayer.effects.clear();
    bridgeLayer.effects.push_back(LayerPlan::EffectPlan{});
    bridgeLayer.effects.back().kind = LayerPlan::EffectKindPlan::Catalog;
    bridgeLayer.effects.back().canonical_effect_id = "color.saturation";
    bridgeLayer.effects.back().canonical_params.push_back({"value", 1.f});
    auto exposureEffect = LayerPlan::EffectPlan{};
    exposureEffect.kind = LayerPlan::EffectKindPlan::Catalog;
    exposureEffect.canonical_effect_id = "color.contrast";
    exposureEffect.canonical_params.push_back({"value", 1.f});
    bridgeLayer.effects.push_back(exposureEffect);
    LayerPlan::PathMaskPlan mask;
    mask.type = "rounded_rect";
    mask.mode = "intersect";
    mask.position = {12.f, 18.f};
    mask.size = {320.f, 180.f};
    mask.radius = 14.f;
    mask.feather = 2.f;
    mask.opacity_track = chronon3d::render_plan::AnimationTrackPlan{};
    mask.opacity_track->property = "mask_opacity";
    mask.opacity_track->easing = "linear";
    mask.opacity_track->keyframes = {{chronon3d::Frame{0}, {0.f}},
                                     {chronon3d::Frame{8}, {1.f}}};
    bridgeLayer.masks.push_back(mask);
    bridgeLayer.track_matte = TrackMattePlan{"matte-layer", TrackMatteTypePlan::AlphaInverted};
    LayerPlan::EffectParamTrackPlan saturationTrack;
    saturationTrack.effect_id = "color.saturation";
    saturationTrack.param = "value";
    saturationTrack.track.property = "opacity";
    saturationTrack.track.easing = "linear";
    saturationTrack.track.keyframes = {{chronon3d::Frame{0}, {0.7f}},
                                        {chronon3d::Frame{8}, {1.2f}}};
    bridgeLayer.effect_param_tracks.push_back(saturationTrack);
    LayerPlan::EffectParamTrackPlan exposureTrack;
    exposureTrack.effect_id = "color.contrast";
    exposureTrack.param = "value";
    exposureTrack.track.property = "opacity";
    exposureTrack.track.easing = "linear";
    exposureTrack.track.keyframes = {{chronon3d::Frame{0}, {0.5f}},
                                     {chronon3d::Frame{8}, {2.f}}};
    bridgeLayer.effect_param_tracks.push_back(exposureTrack);
    check(renderPlanEmissionProblem(bridgePlan).empty(),
          "V3 bridge accepts canonical animated mask, track matte, and effect parameter data");
    const auto bridgeJsonText = renderPlanToJson(bridgePlan);
    const auto bridgeJson = nlohmann::json::parse(bridgeJsonText);
    const auto& bridgeJsonLayer = bridgeJson.at("layers").at(0);
    check(bridgeJsonLayer.at("masks").at(0).at("type") == "rounded_rect",
          "V3 JSON preserves mask geometry");
    check(bridgeJsonLayer.at("masks").at(0).at("opacity_track").at("keyframes").size() == 2,
          "V3 JSON preserves animated mask parameters");
    check(bridgeJsonLayer.at("track_matte").at("type") == "alpha_inverted",
          "V3 JSON preserves track matte type");
    check(bridgeJsonLayer.at("effect_param_tracks").at(0).at("effect_id") == "color.saturation" &&
          std::abs(bridgeJsonLayer.at("effect_param_tracks").at(0).at("keyframes").at(1).at("value").get<float>() - 1.2f) < 1e-6f,
          "V3 JSON preserves effect parameter tracks");
    check(renderPlanToJson(bridgePlan) == bridgeJsonText &&
          bridgeJsonText.find("\"effect_param_tracks\"") ==
              bridgeJsonText.rfind("\"effect_param_tracks\""),
          "mask, matte and effect-track emission is byte deterministic with one effect-track field");
    auto invalidBridgePlan = bridgePlan;
    invalidBridgePlan.layers[0].effect_param_tracks[0].param = "not_animatable";
    check(!renderPlanEmissionProblem(invalidBridgePlan).empty() &&
          throwsInvalid([&] { (void)renderPlanToJson(invalidBridgePlan); }),
          "non-animatable effect parameter tracks fail closed before JSON emission");
    invalidBridgePlan = bridgePlan;
    invalidBridgePlan.layers[0].masks[0].size[0] = -1.f;
    check(!renderPlanEmissionProblem(invalidBridgePlan).empty() &&
          throwsInvalid([&] { (void)renderPlanToJson(invalidBridgePlan); }),
          "malformed mask geometry fails closed before JSON emission");
    invalidBridgePlan = bridgePlan;
    invalidBridgePlan.layers[0].track_matte->source.clear();
    check(!renderPlanEmissionProblem(invalidBridgePlan).empty() &&
          throwsInvalid([&] { (void)renderPlanToJson(invalidBridgePlan); }),
          "track mattes without a source layer id fail closed before JSON emission");
    invalidBridgePlan = bridgePlan;
    invalidBridgePlan.layers[0].effect_param_tracks[0].effect_id = "color.brightness";
    check(!renderPlanEmissionProblem(invalidBridgePlan).empty() &&
          throwsInvalid([&] { (void)renderPlanToJson(invalidBridgePlan); }),
          "effect parameter tracks without a matching layer effect fail closed");
    invalidBridgePlan = bridgePlan;
    invalidBridgePlan.layers[0].effect_param_tracks.push_back(
        invalidBridgePlan.layers[0].effect_param_tracks.front());
    check(!renderPlanEmissionProblem(invalidBridgePlan).empty() &&
          throwsInvalid([&] { (void)renderPlanToJson(invalidBridgePlan); }),
          "duplicate effect parameter tracks fail closed before JSON emission");
    invalidBridgePlan = bridgePlan;
    invalidBridgePlan.layers[0].masks[0].opacity_track->keyframes[1].value[0] = 1.5f;
    check(!renderPlanEmissionProblem(invalidBridgePlan).empty() &&
          throwsInvalid([&] { (void)renderPlanToJson(invalidBridgePlan); }),
          "out-of-range animated mask opacity fails closed before JSON emission");
    invalidBridgePlan = bridgePlan;
    invalidBridgePlan.layers[0].masks[0].type = "unknown_geometry";
    check(!renderPlanEmissionProblem(invalidBridgePlan).empty() &&
          throwsInvalid([&] { (void)renderPlanToJson(invalidBridgePlan); }),
          "unsupported V3 mask geometry fails closed before JSON emission");
    invalidBridgePlan = bridgePlan;
    invalidBridgePlan.layers[0].effect_param_tracks[1].track.keyframes[1].value[0] = 25.0f;
    check(!renderPlanEmissionProblem(invalidBridgePlan).empty() &&
          throwsInvalid([&] { (void)renderPlanToJson(invalidBridgePlan); }),
          "out-of-range animated effect parameters fail closed before JSON emission");
    invalidBridgePlan = bridgePlan;
    invalidBridgePlan.layers[0].effect_param_tracks[0].track.keyframes[1].frame =
        invalidBridgePlan.layers[0].effect_param_tracks[0].track.keyframes[0].frame;
    check(!renderPlanEmissionProblem(invalidBridgePlan).empty() &&
          throwsInvalid([&] { (void)renderPlanToJson(invalidBridgePlan); }),
          "duplicate effect keyframe frames fail closed before JSON emission");
    invalidBridgePlan = bridgePlan;
    invalidBridgePlan.schema = chronon3d::render_plan::kRenderPlanSchemaV2;
    check(!renderPlanEmissionProblem(invalidBridgePlan).empty(),
          "V3 mask payloads fail closed when V2 is selected");
    invalidBridgePlan = bridgePlan;
    invalidBridgePlan.layers[0].transition_in = chronon3d::render_plan::TransitionPlan{};
    check(!renderPlanEmissionProblem(invalidBridgePlan).empty() &&
          throwsInvalid([&] { (void)renderPlanToJson(invalidBridgePlan); }),
          "transition fields stay fail-closed while the Chronon3D schema consumer is misaligned");
    check(renderPlanEmissionProblem(slowedVideo).empty(),
          "V3 adapter continues to accept canonical video content plans");

    MotionScene remappedVideoScene("animated time remap", 30.f);
    auto& remappedVideo = remappedVideoScene.addContent(
        32, "animated video", ContentRef::video("animated-source-video", {640.f, 360.f}));
    remappedVideo.lifetime = FrameRange::between(0, 3);
    remappedVideo.timeRemapTrack.add(0.f, 0.f).add(1.f, 20.f);
    videoOptions.contentLayers.clear();
    videoPlan.source = "animated-source-video";
    videoOptions.contentLayers.emplace("32", videoPlan);
    const auto animatedRemap = lowerToRenderPlan(remappedVideoScene, videoOptions);
    check(animatedRemap.canvas.duration.integral() == 31,
          "composition duration includes the end of a keyed time-remap curve");
    check(animatedRemap.layers[0].time_remap && animatedRemap.layers[0].time_remap->animation &&
          animatedRemap.layers[0].time_remap->animation->keyframes.size() == 31,
          "keyframed video remap lowers to a bounded deterministic scalar track");
    checkNear(animatedRemap.layers[0].time_remap->animation->keyframes[15].value.front(), 10.f, 1e-5f,
              "keyframed time-remap samples the authored source frame at the midpoint");
    const auto parsedAnimatedRemap = nlohmann::json::parse(renderPlanToJson(animatedRemap));
    check(parsedAnimatedRemap.at("layers").at(0).at("time_remap").at("animation")
              .at("keyframes").at(15).at("value") == 10.f,
          "V3 emitter preserves scalar time-remap keyframes");

    auto cameraDofPlan = slowedVideo;
    cameraDofPlan.camera = chronon3d::render_plan::CameraPlan{};
    cameraDofPlan.camera->dof_enabled = true;
    cameraDofPlan.camera->dof_focus_distance = 425.f;
    cameraDofPlan.camera->dof_aperture = 0.035f;
    cameraDofPlan.camera->dof_max_blur = 18.f;
    check(renderPlanEmissionProblem(cameraDofPlan).empty(),
          "camera depth-of-field fields are part of the typed RenderPlan wire contract");
    const auto cameraDofJson = nlohmann::json::parse(renderPlanToJson(cameraDofPlan));
    check(cameraDofJson.at("camera").at("dof").at("enabled") == true,
          "V2/V3 camera emission preserves the DoF enabled flag");
    checkNear(cameraDofJson.at("camera").at("dof").at("focus_distance").get<float>(),
              425.f, 1e-6f, "V2/V3 camera emission preserves the DoF focus distance");
    checkNear(cameraDofJson.at("camera").at("dof").at("aperture").get<float>(),
              0.035f, 1e-6f, "V2/V3 camera emission preserves the DoF aperture");
    checkNear(cameraDofJson.at("camera").at("dof").at("max_blur").get<float>(),
              18.f, 1e-6f, "V2/V3 camera emission preserves the DoF maximum blur");
    const std::string cameraDofText = renderPlanToJson(cameraDofPlan);
    check(renderPlanToJson(cameraDofPlan) == cameraDofText,
          "camera depth-of-field JSON emission is byte-for-byte deterministic");
    auto cameraBlurPlan = slowedVideo;
    cameraBlurPlan.temporal_motion_blur = chronon3d::render_plan::MotionBlurPlan{180.f, 4};
    check(renderPlanEmissionProblem(cameraBlurPlan).empty(),
          "global temporal motion blur is supported by the V3 plan emitter");
    const auto cameraBlurJson = nlohmann::json::parse(renderPlanToJson(cameraBlurPlan));
    check(cameraBlurJson.at("temporal_motion_blur").at("shutter_angle") == 180.f &&
              cameraBlurJson.at("temporal_motion_blur").at("samples") == 4,
          "V3 emission preserves temporal shutter angle and sample count");
    check(renderPlanToJson(cameraBlurPlan) == renderPlanToJson(cameraBlurPlan),
          "temporal motion blur JSON emission is deterministic");
    auto legacyCameraBlurPlan = cameraBlurPlan;
    legacyCameraBlurPlan.schema = chronon3d::render_plan::kRenderPlanSchemaV2;
    check(!renderPlanEmissionProblem(legacyCameraBlurPlan).empty(),
          "global temporal motion blur is rejected for the V2 schema");
    auto invalidCameraDofPlan = cameraDofPlan;
    invalidCameraDofPlan.camera->dof_aperture = 2.8f;
    check(!renderPlanEmissionProblem(invalidCameraDofPlan).empty() &&
          throwsInvalid([&] { (void)renderPlanToJson(invalidCameraDofPlan); }),
          "camera depth-of-field values outside Chronon3D's contract fail closed");
    auto defaultCameraPlan = slowedVideo;
    defaultCameraPlan.camera = chronon3d::render_plan::CameraPlan{};
    check(!nlohmann::json::parse(renderPlanToJson(defaultCameraPlan)).at("camera").contains("dof"),
          "default camera optics remain omitted to preserve canonical compact JSON");

    auto legacyVideoOptions = videoOptions;
    legacyVideoOptions.contentLayers.clear();
    legacyVideoOptions.contentLayers.emplace("31", videoPlan);
    legacyVideoOptions.schema = chronon3d::render_plan::kRenderPlanSchemaV2;
    check(throwsInvalid([&] { (void)lowerToRenderPlan(videoScene, legacyVideoOptions); }),
          "Motion time remap fails closed when the selected RenderPlan schema is V2");
    auto illegalRemapPlan = slowedVideo;
    illegalRemapPlan.schema = chronon3d::render_plan::kRenderPlanSchemaV2;
    check(!renderPlanEmissionProblem(illegalRemapPlan).empty(),
          "time remap diagnostics reject a V3-only feature on V2");
    check(throwsInvalid([&] { (void)renderPlanToJson(illegalRemapPlan); }),
          "the emitter rejects V3 time remapping attached to a V2 plan");
    auto loopedVideoScene = MotionScene("looped time remap", 30.f);
    auto& loopedVideo = loopedVideoScene.addContent(
        33, "looped video", ContentRef::video("looped-source-video", {640.f, 360.f}));
    loopedVideo.lifetime = FrameRange::between(0, 3);
    loopedVideo.timeRemap.loopEnd = 12;
    auto loopedVideoOptions = videoOptions;
    loopedVideoOptions.contentLayers.clear();
    videoPlan.source = "looped-source-video";
    loopedVideoOptions.contentLayers.emplace("33", videoPlan);
    check(throwsInvalid([&] { (void)lowerToRenderPlan(loopedVideoScene, loopedVideoOptions); }),
          "looping time remap is explicitly rejected instead of silently changing its V3 semantics");
    const auto shapePath = lowerBakedPath(parseSvgPath("M0 0 C1 0 2 1 3 1 Z"));
    check(shapePath.size() == 3 && shapePath[1].type == chronon3d::render_plan::ShapePathCommandTypePlan::CubicTo,
          "baked paths map to Chronon3D's native shape path command type");
    MotionScene cameraScene("camera lower", 30.f);
    auto& cameraContent = cameraScene.addContent(22, "camera content", ContentRef::image("camera-asset", {4.f, 4.f}));
    cameraContent.lifetime = FrameRange::between(0, 2);
    cameraContent.transform.position.set(4.f, 5.f, -12.f);
    cameraScene.evaluate(0);
    CameraRig animatedRig(23, 50.f, 16.f / 9.f, 0.1f, 2000.f);
    animatedRig.setPosition({1.f, 2.f, -8.f});
    animatedRig.positionTrack().add(0.f, Vector3(1.f, 2.f, -8.f));
    animatedRig.positionTrack().add(3.f / 30.f, Vector3(3.f, 2.f, -8.f));
    RenderPlanLoweringOptions cameraOptions;
    cameraOptions.width = 640;
    cameraOptions.height = 360;
    cameraOptions.cameraRig = &animatedRig;
    chronon3d::render_plan::LayerPlan cameraImage;
    cameraImage.type = chronon3d::render_plan::LayerType::Image;
    cameraImage.asset = "camera-asset";
    cameraOptions.contentLayers.emplace("22", cameraImage);
    const auto cameraPlan = lowerToRenderPlan(cameraScene, cameraOptions);
    check(cameraPlan.camera && cameraPlan.camera_samples && cameraPlan.camera_samples->size() == 4,
          "camera rig lowers to a typed base projection and resolved frame samples");
    const auto* cameraPositionZ = &cameraPlan.camera_samples->back().camera;
    const auto& loweredContent = cameraPlan.layers.front();
    const auto& loweredPositionTrack = loweredContent.animation->tracks.front();
    check(cameraPositionZ && cameraPlan.camera->position[2] == 8.f &&
          cameraPositionZ->position[2] == 8.f,
          "camera position and its dense animation track mirror Motion depth into RenderPlan space");
    check(loweredContent.position_dimensions == 3 && loweredContent.position[2] == 12.f &&
          loweredPositionTrack.property == "position" &&
          loweredPositionTrack.keyframes.back().value[2] == 12.f,
          "3D layer anchor and its baked transform use the same mirrored RenderPlan depth");
    // Primitive base transforms and dense animation keys must use the same
    // mirrored depth, regardless of whether a second frame enables a track.
    for (const auto type : {LayerType::Text, LayerType::Shape, LayerType::Particles}) {
        for (int lastFrame : {0, 1}) {
            MotionScene primitiveScene("primitive depth", 30.f);
            auto& primitive = primitiveScene.addContent(
                90, "primitive", type == LayerType::Text
                    ? ContentRef::text("primitive", {20.f, 20.f})
                    : ContentRef::image("primitive", {20.f, 20.f}));
            primitive.transform.position.set(4.f, 5.f, -12.f);
            primitive.lifetime = FrameRange::between(0, lastFrame);
            if (lastFrame != 0) {
                primitive.tracks.position.add(0.f, Vector3(4.f, 5.f, -12.f))
                    .add(1.f / 30.f, Vector3(4.f, 5.f, -14.f));
            }
            RenderPlanLoweringOptions primitiveOptions;
            primitiveOptions.width = 640;
            primitiveOptions.height = 360;
            LayerPlan primitivePlan;
            primitivePlan.type = type;
            primitiveOptions.contentLayers.emplace("90", primitivePlan);
            const auto lowered = lowerToRenderPlan(primitiveScene, primitiveOptions);
            const auto& primitiveLayer = lowered.layers.front();
            check(primitiveLayer.position[2] == 12.f,
                  "primitive base depth mirrors the Motion coordinate system");
            if (lastFrame != 0) {
                check(primitiveLayer.animation &&
                          primitiveLayer.animation->tracks.front().property == "position" &&
                          primitiveLayer.animation->tracks.front().keyframes.front().value[2] ==
                              primitiveLayer.position[2],
                      "primitive animated depth agrees with the static base depth");
                if (primitiveLayer.animation && !primitiveLayer.animation->tracks.empty() &&
                    !primitiveLayer.animation->tracks.front().keyframes.empty()) {
                    checkNear(primitiveLayer.animation->tracks.front().keyframes.back().value[2],
                              14.f, 1e-5f, "primitive animated depth preserves the mirrored Z change");
                }
            }
        }
    }
    CameraRig dofRig(24, 50.f, 16.f / 9.f, 0.1f, 2000.f);
    dofRig.setPosition({1.f, 2.f, -8.f}).setFStop(19.2f).setFocusDistance(425.f);
    auto dofCameraOptions = cameraOptions;
    dofCameraOptions.cameraRig = &dofRig;
    const auto loweredDofCamera = lowerToRenderPlan(cameraScene, dofCameraOptions);
    check(loweredDofCamera.camera && loweredDofCamera.camera->dof_enabled &&
          loweredDofCamera.camera->dof_f_stop == 19.2f &&
          loweredDofCamera.camera->dof_focus_distance == 425.f,
          "CameraRig focus distance lowers to the RenderPlan camera-space focus distance unchanged");
    auto styledImagePlan = cameraPlan;
    auto& styledImage = styledImagePlan.layers.front();
    LayerStylePlan imageStyle;
    BackgroundStyle imageFrame;
    imageFrame.color = "#E8E2D5";
    imageFrame.radius = 18.f;
    imageFrame.padding = {6.f, 6.f};
    imageFrame.padding_dimensions = 2;
    imageStyle.background = imageFrame;
    styledImage.style = imageStyle;
    LayerPlan::EffectPlan saturation;
    saturation.kind = LayerPlan::EffectKindPlan::Catalog;
    saturation.canonical_effect_id = "color.saturation";
    saturation.canonical_params.push_back({"value", 0.78f});
    LayerPlan::EffectPlan contrast;
    contrast.kind = LayerPlan::EffectKindPlan::Catalog;
    contrast.canonical_effect_id = "color.contrast";
    contrast.canonical_params.push_back({"value", 1.08f});
    LayerPlan::EffectPlan grain;
    grain.kind = LayerPlan::EffectKindPlan::Noise;
    grain.amount = 0.02f;
    grain.seed = 1986;
    LayerPlan::EffectPlan vignette;
    vignette.kind = LayerPlan::EffectKindPlan::Vignette;
    vignette.amount = 0.12f;
    vignette.radius = 0.72f;
    vignette.softness = 0.55f;
    styledImage.effects = {saturation, contrast, grain, vignette};
    styledImage.crop = chronon3d::render_plan::ImageCropPlan{
        .enabled = true, .origin = {0.1f, 0.05f}, .size = {0.8f, 0.9f}, .flip_x = true};
    styledImagePlan.schema = chronon3d::render_plan::kRenderPlanSchemaV3;
    const auto styledImageJson = nlohmann::json::parse(renderPlanToJson(styledImagePlan));
    check(styledImageJson["layers"][0]["style"]["background"]["padding"] ==
              nlohmann::json::array({6.f, 6.f}) &&
          styledImageJson["layers"][0]["effects"].size() == 4 &&
          styledImageJson["layers"][0]["effects"][2]["seed"] == 1986 &&
          styledImageJson["layers"][0]["crop"]["flip_x"] == true &&
          static_cast<float>(styledImageJson["layers"][0]["crop"]["origin"][0].get<double>()) == 0.1f &&
          static_cast<float>(styledImageJson["layers"][0]["crop"]["origin"][1].get<double>()) == 0.05f,
          "RenderPlan emission preserves image frame, grade, crop mirror, seeded grain, and vignette");
    auto leakPlan = styledImagePlan;
    chronon3d::render_plan::LayerPlan leakLayer;
    leakLayer.id = "motion-authoring-light-leak";
    leakLayer.type = chronon3d::render_plan::LayerType::Shape;
    leakLayer.size = {1920.f, 1080.f};
    leakLayer.size_dimensions = 2;
    chronon3d::render_plan::ShapeContentPlan radialFill;
    radialFill.type = chronon3d::render_plan::ShapeContentTypePlan::Ellipse;
    chronon3d::graphics::GradientDefinition leakGradient;
    leakGradient.type = chronon3d::graphics::GradientType::Radial;
    leakGradient.color_stops = {
        {0.f, chronon3d::Color{1.f, 0.82f, 0.55f, 1.f}},
        {1.f, chronon3d::Color{0.f, 0.18f, 0.42f, 0.f}}};
    radialFill.gradient = std::move(leakGradient);
    leakLayer.shape = std::move(radialFill);
    leakPlan.layers = {std::move(leakLayer)};
    leakPlan.canvas.duration = chronon3d::Frame{2};
    leakPlan.layers.front().duration_frames = chronon3d::Frame{2};
    leakPlan.layers.front().animation = chronon3d::render_plan::AnimationPlan{};
    chronon3d::render_plan::AnimationTrackPlan leakOpacity;
    leakOpacity.property = "opacity";
    leakOpacity.keyframes = {
        {chronon3d::Frame{0}, {0.f}}, {chronon3d::Frame{1}, {1.f}},
        {chronon3d::Frame{2}, {0.f}}};
    leakPlan.layers.front().animation->tracks.push_back(leakOpacity);
    const auto leakJson = nlohmann::json::parse(renderPlanToJson(leakPlan));
    check(leakJson["layers"][0]["shape"]["fill"]["type"] == "radial" &&
          leakJson["layers"][0]["shape"]["fill"]["color_stops"].size() == 2 &&
          leakJson["layers"][0]["animation"]["tracks"][0]["keyframes"].size() == 3,
          "RenderPlan emission preserves native gradient fills and animated light-leak opacity");
    auto unsupportedEffectPlan = styledImagePlan;
    unsupportedEffectPlan.layers.front().effects.back().kind = LayerPlan::EffectKindPlan::FractalNoise;
    check(!renderPlanEmissionProblem(unsupportedEffectPlan).empty(),
          "RenderPlan emission rejects effect types outside its explicit native subset");
    styledImagePlan.camera_samples.reset();
    CameraPlan documentaryCamera;
    documentaryCamera.position = {0.f, 0.f, 850.f};
    documentaryCamera.rotation_deg = {0.f, 0.f, 0.f};
    documentaryCamera.fov_deg = 50.f;
    documentaryCamera.near = 0.1f;
    documentaryCamera.far = 2000.f;
    styledImagePlan.camera = documentaryCamera;
    styledImagePlan.canvas.duration = chronon3d::Frame{1};
    styledImagePlan.layers.front().asset = "assets/images/card_620x720_left.png";
    styledImagePlan.layers.front().position = {0.f, 0.f, 0.f};
    styledImagePlan.layers.front().position_dimensions = 3;
    styledImagePlan.layers.front().size = {400.f, 560.f};
    styledImagePlan.layers.front().size_dimensions = 2;
    styledImagePlan.layers.front().animation.reset();
    styledImagePlan.layers.front().duration_frames = chronon3d::Frame{1};
    styledImagePlan.output.path = "/tmp/chronon_documentary_snapshot_adapter_smoke.png";
    std::ofstream documentaryAdapterPlan("/tmp/chronon_documentary_snapshot_adapter_smoke.plan.json",
                                         std::ios::binary | std::ios::trunc);
    check(documentaryAdapterPlan && (documentaryAdapterPlan << renderPlanToJson(styledImagePlan)),
          "documentary image look plan can be written for Chronon3D's native decoder");
    CameraRig invalidDofRig(25, 50.f, 16.f / 9.f, 0.1f, 2000.f);
    check(throwsInvalid([&] { invalidDofRig.setFStop(-2.8f); }),
          "the f-stop setter rejects negative apertures");
    check(cameraPlan.canvas.duration.integral() == 4,
          "camera animation extends the composition duration when its keys outlive scene content");

    chronon3d::render_plan::LayerPlan bakedShape;
    bakedShape.type = chronon3d::render_plan::LayerType::Shape;
    chronon3d::render_plan::ShapeContentPlan bakedContent;
    bakedContent.type = chronon3d::render_plan::ShapeContentTypePlan::Path;
    bakedShape.shape = bakedContent;
    const auto copies = expandPathRepeater(parseSvgPath("M0 0 L5 0"), RepeaterSpec{2, {8.f, 0.f}});
    applyBakedPathGeometry(bakedShape, parseSvgPath("M0 0 L5 0"), copies);
    check(bakedShape.shape->operators.size() == 1 &&
          bakedShape.shape->operators.front().kind == chronon3d::render_plan::PathOperatorKindPlan::Merge &&
          bakedShape.shape->operators.front().paths.size() == 1,
          "baked repeater identity copy remains the source path and additional copies lower as a V3 Merge operator");
    const auto morphFrames = bakeSvgPathMorph("M0 0 L10 0 Z", "M0 0 L20 0 Z", 4, 6);
    chronon3d::render_plan::LayerPlan morphShape = bakedShape;
    morphShape.shape->operators.clear();
    applyBakedPathMorph(morphShape, morphFrames, 4);
    check(morphShape.shape->operators.size() == 1 &&
          morphShape.shape->operators.front().kind == chronon3d::render_plan::PathOperatorKindPlan::Morph &&
          morphShape.shape->operators.front().morph_target.size() == 3 &&
          morphShape.shape->operators.front().morph_animation &&
          morphShape.shape->operators.front().morph_animation->keyframes.size() == 3,
          "baked compatible SVG frames lower to native endpoint Morph geometry and deterministic progress keys");
    auto unsupportedMorph = morphFrames;
    unsupportedMorph[1][1].point.x += 0.5f;
    check(throwsInvalid([&] { applyBakedPathMorph(morphShape, unsupportedMorph, 4); }),
          "morph adapter rejects baked intermediate samples not representable by linear endpoint morphing");

    chronon3d::render_plan::LayerPlan riggedMesh;
    riggedMesh.id = "rigged-triangle";
    riggedMesh.type = chronon3d::render_plan::LayerType::Mesh;
    riggedMesh.inline_mesh = triangle;
    RigFrameGeometry rigFrame{1, {{-1.f, -1.f, 0.f}, {1.f, -1.f, 0.f}, {0.f, 1.f, 1.f}}};
    applyRigGeometryFrame(riggedMesh, rigFrame, std::array<std::uint32_t, 3>{0, 1, 2});
    check(riggedMesh.inline_mesh->vertices[2].position == std::array<float, 3>{0.f, 1.f, 1.f},
          "rig geometry bake replaces the target frame's mesh positions");
    check(riggedMesh.inline_mesh->vertices[2].uv == std::array<float, 2>{0.5f, 1.f} &&
          riggedMesh.inline_mesh->material->base_color == triangle.material->base_color,
          "rig geometry bake retains authored texture coordinates and material");
    check(riggedMesh.inline_mesh->vertices[0].normal[2] > 0.f,
          "rig geometry bake regenerates normals from deformed triangles");
    auto riggedFramePlan = inlineMeshPlan;
    riggedFramePlan.layers[0] = riggedMesh;
    check(renderPlanEmissionProblem(riggedFramePlan).empty(),
          "one deformed rig frame is eligible for canonical V3 RenderPlan emission");
    const auto riggedFrameJson = renderPlanToJson(riggedFramePlan);
    check(renderPlanToJson(riggedFramePlan) == riggedFrameJson,
          "deformed rig mesh frame serialization is deterministic");
    auto secondRigFrame = rigFrame;
    secondRigFrame.positions[2].z = 2.f;
    rigFrame.normals = {{0.f, 0.f, 1.f}, {0.f, 0.f, 1.f}, {0.f, 0.f, 1.f}};
    secondRigFrame.normals = {{0.f, 1.f, 0.f}, {0.f, 1.f, 0.f}, {0.f, 1.f, 0.f}};
    applyRigGeometryFrame(riggedMesh, secondRigFrame, std::array<std::uint32_t, 3>{0, 1, 2});
    auto secondRigFramePlan = inlineMeshPlan;
    secondRigFramePlan.layers[0] = riggedMesh;
    check(renderPlanToJson(secondRigFramePlan) != riggedFrameJson,
          "different baked rig geometry produces a distinct frame payload");
    BakedRigProgram geometryBake;
    geometryBake.frames = {
        RigProgramFrame{{}, {rigFrame}},
        RigProgramFrame{{}, {secondRigFrame}}
    };
    auto animatedRigPlan = inlineMeshPlan;
    animatedRigPlan.layers[0] = riggedMesh;
    animatedRigPlan.layers[0].asset = "textures/character-base.png";
    animatedRigPlan.layers[0].input_color_space = "srgb";
    animatedRigPlan.layers[0].inline_mesh->metallic_roughness_texture_asset = "textures/character-orm.png";
    applyRigGeometryBake(animatedRigPlan.layers[0], geometryBake, 1,
                         std::array<std::uint32_t, 3>{0, 1, 2}, 4);
    check(animatedRigPlan.layers[0].inline_mesh->animation.size() == 2 &&
          animatedRigPlan.layers[0].inline_mesh->animation[1].frame == 5 &&
          animatedRigPlan.layers[0].inline_mesh->animation[1].positions[2][2] == 2.f &&
          animatedRigPlan.layers[0].inline_mesh->animation[1].normals[0][1] == 1.f,
          "RigProgram bake lowers frame positions and normals to V3 geometry samples");
    check(animatedRigPlan.layers[0].inline_mesh->vertices[2].uv == triangle.vertices[2].uv &&
          animatedRigPlan.layers[0].inline_mesh->material->base_color == triangle.material->base_color,
          "animated RigProgram lowering keeps authored UV and alpha-bearing material unchanged");
    animatedRigPlan.canvas.duration = chronon3d::Frame{6};
    check(renderPlanEmissionProblem(animatedRigPlan).empty(),
          "animated rig geometry passes producer validation for V3 emission");
    const auto animatedRigJson = nlohmann::json::parse(renderPlanToJson(animatedRigPlan));
    check(animatedRigJson.at("layers").at(0).at("inline_mesh").at("animation").size() == 2 &&
          animatedRigJson.at("layers").at(0).at("inline_mesh").at("animation")[1].at("frame") == 5 &&
          animatedRigJson.at("layers").at(0).at("inline_mesh").at("animation")[1].at("normals")[0][1] == 1.f &&
          animatedRigJson.at("layers").at(0).at("asset") == "textures/character-base.png" &&
          animatedRigJson.at("layers").at(0).at("inline_mesh").at("metallic_roughness_texture") == "textures/character-orm.png",
          "V3 emission keeps animated positions/normals with base and ORM texture references");

    chrononmotion_test::GltfMotionFixture gltfFixture;
    const auto importedGltf = gltfFixture.load(gltfFixture.withAnimatedMorphChannel());
    check(importedGltf.skins.size() == 1 && importedGltf.geometry.size() == 1 &&
          importedGltf.clips.size() == 1 && importedGltf.clips[0].morphs.size() == 1,
          "animated glTF fixture imports skin, clip, morph target and morph-weight channel");
    RigProgram importedProgram;
    importedProgram.graph = importedGltf.graph;
    importedProgram.animation = importedGltf.clips[0].animation;
    importedProgram.geometry = importedGltf.geometry;
    for (const auto& morph : importedGltf.clips[0].morphs)
        importedProgram.geometry.at(morph.geometryId - 1).morphTracks = morph.weights;
    const RigProgramEvaluator importedEvaluator(importedProgram);
    const auto importedBake = importedEvaluator.bake(0, 30, 30.f);
    check(importedBake.signature == importedEvaluator.bake(0, 30, 30.f).signature,
          "skinned and morphed glTF bake repeats deterministically");
    auto importedGltfPlan = inlineMeshPlan;
    importedGltfPlan.canvas.duration = chronon3d::Frame{31};
    applyRigGeometryBake(importedGltfPlan.layers[0], importedBake,
                         importedGltf.geometry[0].id,
                         importedGltf.geometry[0].indices, 0);
    check(importedGltfPlan.layers[0].inline_mesh->animation.size() == 31 &&
          importedGltfPlan.layers[0].inline_mesh->animation[15].positions[0][0] == 1.f &&
          importedGltfPlan.layers[0].inline_mesh->animation[15].positions[1][1] == 0.5f &&
          importedGltfPlan.layers[0].inline_mesh->animation[30].positions[1][1] == 1.f,
          "imported skin and morph samples bake to the V3 inline_mesh.animation contract");
    check(renderPlanEmissionProblem(importedGltfPlan).empty(),
          "animated glTF geometry passes typed RenderPlan producer validation");
    const auto importedGltfJson = renderPlanToJson(importedGltfPlan);
    const auto parsedImportedGltf = nlohmann::json::parse(importedGltfJson);
    check(parsedImportedGltf.at("layers").at(0).at("inline_mesh").at("animation").size() == 31 &&
          renderPlanToJson(importedGltfPlan) == importedGltfJson,
          "glTF skin/morph V3 animation emission round-trips deterministically");

    // Optional concrete producer artifacts for downstream Chronon3D CLI acceptance.
    // They use the fixture and serializer already covered by this test.
    if (const char* path = std::getenv("CHRONONMOTION_TICKET_GLTF_PLAN")) {
        auto visiblePlan = importedGltfPlan;
        auto& visibleMesh = *visiblePlan.layers[0].inline_mesh;
        visiblePlan.layers[0].duration_frames = 31;
        // The synthetic glTF is authored in unit-scale model coordinates,
        // while Chronon3D's fallback 2.5D camera is 1000 units from origin.
        // Place the mesh at the canvas center and enlarge only this CLI
        // acceptance fixture so texture alpha and baked deformation are visible.
        visiblePlan.layers[0].position = {320.0f, 180.0f, 0.0f};
        visiblePlan.layers[0].scale = {70.0f, 70.0f, 70.0f};
        visiblePlan.layers[0].asset = "textures/character-base.png";
        visiblePlan.layers[0].input_color_space = "srgb";
        visibleMesh.material->base_color = std::array<float, 4>{1.f, 1.f, 1.f, 1.f};
        visibleMesh.material->alpha_cutoff = 0.5f;
        visiblePlan.output.format = chronon3d::render_plan::OutputFormat::Mp4;
        visiblePlan.output.path = "animated-gltf.mp4";
        std::filesystem::create_directories(std::filesystem::path(path).parent_path());
        std::ofstream output(path, std::ios::binary);
        if (!output || !(output << renderPlanToJson(visiblePlan)))
            throw std::runtime_error("cannot write requested animated glTF ticket RenderPlan");
    }
    if (const char* path = std::getenv("CHRONONMOTION_TICKET_HDRI_PLAN")) {
        auto visiblePlan = inlineMeshPlan;
        visiblePlan.canvas.duration = chronon3d::Frame{4};
        visiblePlan.layers[0].position = {320.0f, 180.0f, 0.0f};
        visiblePlan.layers[0].scale = {100.0f, 100.0f, 100.0f};
        visiblePlan.output.format = chronon3d::render_plan::OutputFormat::Mp4;
        visiblePlan.output.path = "hdri-probe.mp4";
        auto probe = decodeHdriRadiance(chrononmotion_test::constantRadianceHdr());
        const std::filesystem::path planPath(path);
        const std::string assetPath = "assets/studio.hdri-env";
        attachHdriEnvironmentToRenderPlan(visiblePlan, probe, assetPath);
        const auto assetOutput = planPath.parent_path() / assetPath;
        std::filesystem::create_directories(assetOutput.parent_path());
        const auto bytes = probe.serialize();
        std::ofstream assetFile(assetOutput, std::ios::binary);
        assetFile.write(reinterpret_cast<const char*>(bytes.data()),
                        static_cast<std::streamsize>(bytes.size()));
        std::ofstream planOutput(path, std::ios::binary);
        if (!assetFile || !planOutput || !(planOutput << renderPlanToJson(visiblePlan)))
            throw std::runtime_error("cannot write requested HDRI ticket RenderPlan and asset");
    }

    auto rejectedBake = geometryBake;
    rejectedBake.frames[1].geometry.clear();
    const auto previousSamples = animatedRigPlan.layers[0].inline_mesh->animation;
    check(throwsInvalid([&] { applyRigGeometryBake(animatedRigPlan.layers[0], rejectedBake, 1,
              std::array<std::uint32_t, 3>{0, 1, 2}, 4); }) &&
          animatedRigPlan.layers[0].inline_mesh->animation.size() == previousSamples.size(),
          "failed RigProgram lowering leaves prior RenderPlan samples unchanged");
    applyRigGeometryFrame(riggedMesh, rigFrame, std::array<std::uint32_t, 3>{0, 1, 2});
    auto badRigFrame = rigFrame;
    badRigFrame.positions.pop_back();
    check(throwsInvalid([&] { applyRigGeometryFrame(riggedMesh, badRigFrame,
              std::array<std::uint32_t, 3>{0, 1, 2}); }),
          "rig geometry lowering rejects mismatched baked topology");
    badRigFrame = rigFrame;
    badRigFrame.positions[0].x = std::numeric_limits<float>::infinity();
    check(throwsInvalid([&] { applyRigGeometryFrame(riggedMesh, badRigFrame,
              std::array<std::uint32_t, 3>{0, 1, 2}); }),
          "rig geometry lowering rejects non-finite baked vertices");
    auto animatedRiggedMesh = riggedMesh;
    animatedRiggedMesh.animation = chronon3d::render_plan::AnimationPlan{};
    chronon3d::render_plan::AnimationTrackPlan illegalMeshTrack;
    illegalMeshTrack.property = "opacity";
    illegalMeshTrack.keyframes.push_back({chronon3d::Frame{0}, {1.f}});
    animatedRiggedMesh.animation->tracks.push_back(illegalMeshTrack);
    auto animatedRiggedFramePlan = riggedFramePlan;
    animatedRiggedFramePlan.layers[0] = animatedRiggedMesh;
    check(!renderPlanEmissionProblem(animatedRiggedFramePlan).empty(),
          "mesh transform animation remains rejected instead of being silently ignored");
    bool missingContentRejected = false;
    try { RenderPlanLoweringOptions empty; (void)lowerToRenderPlan(scene, empty); }
    catch (const std::invalid_argument&) { missingContentRejected = true; }
    check(missingContentRejected, "lowering rejects content whose rendering data is not supplied");
    check(throwsInvalid([&] { auto bad = options; bad.width = 0; (void)lowerToRenderPlan(scene, bad); }),
          "lowering rejects invalid canvas dimensions");
    check(throwsInvalid([&] { auto bad = options; bad.contentLayers.emplace("999", content);
        (void)lowerToRenderPlan(scene, bad); }),
        "lowering rejects stale content mappings that name no scene layer");
    check(throwsInvalid([&] { auto bad = options; bad.camera = chronon3d::render_plan::CameraPlan{};
        bad.camera->far = std::numeric_limits<float>::infinity(); (void)lowerToRenderPlan(scene, bad); }),
        "lowering rejects non-finite camera projection data");
    CameraPose cameraPose;
    cameraPose.fov = 50.f;
    cameraPose.position = {1.f, 2.f, -3.f};
    const auto camera = lowerCameraPose(cameraPose, 0.1f, 2000.f);
    checkNear(camera.fov_deg, 50.f, 1e-6f, "camera pose lowering maps typed projection parameters");
    check(camera.position == std::array<float, 3>{1.f, 2.f, 3.f},
          "camera pose lowering maps Motion -Z depth to RenderPlan +Z-forward coordinates");
    check(throwsInvalid([&] { (void)lowerCameraPose(cameraPose, 2.f, 1.f); }),
          "camera lowering rejects an invalid clipping interval");
    cameraPose.fStop = 16.8f;
    cameraPose.focusDistance = 480.f;
    const auto dofCamera = lowerCameraPose(cameraPose, 0.1f, 2000.f);
    check(dofCamera.dof_enabled && dofCamera.dof_f_stop == 16.8f &&
          dofCamera.dof_focus_distance == 480.f,
          "camera pose lowering preserves the optical-axis focus distance in RenderPlan space");
    cameraPose.position = {0.f, 0.f, 480.00006f};
    cameraPose.focusDistance = 480.f;
    const auto nearZeroFocusPlane = lowerCameraPose(cameraPose, 0.1f, 2000.f);
    check(nearZeroFocusPlane.dof_focus_distance == 480.f,
          "camera lowering preserves small focus distances without plane epsilon clamping");
    cameraPose.focusDistance = -5.f;
    check(throwsInvalid([&] { (void)lowerCameraPose(cameraPose, 0.1f, 2000.f); }),
          "camera lowering still rejects negative focus distances outside the RenderPlan contract");
    cameraPose.focusDistance = 0.f;
    check(throwsInvalid([&] { (void)lowerCameraPose(cameraPose, 0.1f, 2000.f); }),
          "camera lowering rejects a zero focus distance while the aperture enables depth of field");
    cameraPose.position = {1.f, 2.f, -3.f};
    cameraPose.focusDistance = 480.f;
    cameraPose.focalLength = 50.f;
    const auto focalLengthCamera = lowerCameraPose(cameraPose, 0.1f, 2000.f);
    check(focalLengthCamera.focal_length_mm == 50.f,
          "camera lowering preserves authored focal length in the RenderPlan lens contract");
    cameraPose.focalLength = 0.f;
    cameraPose.fStop = 2.8f;
    const auto physicalDofCamera = lowerCameraPose(cameraPose, 0.1f, 2000.f);
    check(physicalDofCamera.f_stop == 2.8f && physicalDofCamera.dof_f_stop == 2.8f &&
          physicalDofCamera.dof_enabled,
          "camera lowering preserves physical f-stop and enables the RenderPlan physical DoF contract");
    cameraPose.irisBlades = 6;
    check(throwsInvalid([&] { (void)lowerCameraPose(cameraPose, 0.1f, 2000.f); }),
          "camera lowering rejects unsupported iris blades before emission");
    cameraPose.irisBlades = 0;
    cameraPose.highlightGain = 1.f;
    check(throwsInvalid([&] { (void)lowerCameraPose(cameraPose, 0.1f, 2000.f); }),
          "camera lowering rejects unsupported highlight weighting before emission");
    cameraPose.highlightGain = 0.f;
    for (float fov : {10.f, 120.f}) {
        cameraPose.fov = fov;
        check(lowerCameraPose(cameraPose, 0.1f, 2000.f).fov_deg == fov,
              "shared camera contract accepts both FOV endpoints");
    }
    for (float fov : {9.99f, 120.01f, std::numeric_limits<float>::quiet_NaN()}) {
        cameraPose.fov = fov;
        check(throwsInvalid([&] { (void)lowerCameraPose(cameraPose, 0.1f, 2000.f); }),
              "shared camera contract rejects out-of-range and non-finite FOV");
    }
    cameraPose.fov = 50.f;
    cameraPose.maxBlur = 300.f;
    check(throwsInvalid([&] { (void)lowerCameraPose(cameraPose, 0.1f, 2000.f); }),
          "camera lowering rejects depth-of-field values outside the RenderPlan schema bounds");
    cameraPose.maxBlur = 0.f;
    cameraPose.fStop = 16.8f;
    cameraPose.focusDistance = 480.f;
    cameraPose.focalLength = 0.f;
    const auto loweredCameraPoseDof = lowerCameraPose(cameraPose, 0.1f, 2000.f);
    auto cameraOnlyScene = MotionScene("camera DoF parity", 30.f);
    cameraOnlyScene.evaluate(0);
    CameraRig animatedDofRig(26, 50.f, 16.f / 9.f, 0.1f, 2000.f);
    animatedDofRig.setSensorWidth(32.f).setLensShift({0.03f, -0.02f});
    animatedDofRig.setPosition({0.f, 0.f, -8.f}).setFStop(16.8f).setFocusDistance(480.f);
    animatedDofRig.fStopTrack().add(0.f, 16.8f).add(1.f, 11.2f);
    animatedDofRig.focusDistanceTrack().add(0.f, 480.f).add(1.f, 720.f);
    animatedDofRig.setMaxBlur(12.f).maxBlurTrack().add(0.f, 12.f).add(1.f, 20.f);
    RenderPlanLoweringOptions animatedDofOptions;
    animatedDofOptions.cameraRig = &animatedDofRig;
    const auto animatedDofPlan = lowerToRenderPlan(cameraOnlyScene, animatedDofOptions);
    check(animatedDofPlan.camera && animatedDofPlan.camera->dof_enabled &&
          animatedDofPlan.camera_samples && animatedDofPlan.camera_samples->size() > 1,
          "camera lowering enables and emits resolved depth-of-field samples");
    if (animatedDofPlan.camera_samples && animatedDofPlan.camera_samples->size() > 1) {
        const auto& first = animatedDofPlan.camera_samples->front().camera;
        const auto& last = animatedDofPlan.camera_samples->back().camera;
        check(first.dof_focus_distance == 480.f && last.dof_focus_distance == 720.f &&
              first.dof_f_stop == 16.8f && last.dof_f_stop == 11.2f &&
              first.dof_max_blur == 12.f && last.dof_max_blur == 20.f,
              "resolved camera samples preserve focus, aperture, and blur over time");
    }
    const auto animatedDofJson = nlohmann::json::parse(renderPlanToJson(animatedDofPlan));
    check(animatedDofPlan.camera_samples->front().camera.sensor_width_mm == 32.f &&
          animatedDofPlan.camera_samples->front().camera.lens_shift == std::array<float, 2>{0.03f, -0.02f},
          "resolved camera samples preserve physical sensor width and normalized lens shift");
    check(animatedDofJson.contains("camera_samples") &&
          animatedDofJson["camera_samples"].size() == animatedDofPlan.camera_samples->size() &&
          !animatedDofJson.contains("camera_animation"),
          "RenderPlan JSON emits complete camera samples without legacy property tracks");
    CameraRig dofHandoffRig(27, 50.f, 16.f / 9.f, 0.1f, 2000.f);
    dofHandoffRig.setPosition({0.f, 0.f, -8.f}).setFocusDistance(480.f);
    dofHandoffRig.fStopTrack().add(0.f, 0.7f).add(1.f, 11.2f);
    auto dofHandoffOptions = animatedDofOptions;
    dofHandoffOptions.cameraRig = &dofHandoffRig;
    const auto dofHandoffPlan = lowerToRenderPlan(cameraOnlyScene, dofHandoffOptions);
    check(dofHandoffPlan.camera && dofHandoffPlan.camera->dof_enabled,
          "camera lowering enables DOF in the base plan when aperture becomes active after frame zero");

    // Regression pin: an entirely static lens (no DOF track keys at all) must
    // still lower every authored field into the base plan camera. The values
    // arrive via the frame-zero pose, so a refactor of lowerToRenderPlan that
    // loses them would render plans without the authored lens silently.
    CameraRig staticLensRig(28, 50.f, 16.f / 9.f, 0.1f, 2000.f);
    staticLensRig.setPosition({0.f, 0.f, -8.f});
    staticLensRig.setFocusDistance(480.f).setMaxBlur(30.f);
    staticLensRig.setFStop(2.8f).setIrisBlades(6).setHighlightGain(1.5f);
    auto staticLensOptions = animatedDofOptions;
    staticLensOptions.cameraRig = &staticLensRig;
    check(throwsInvalid([&] { (void)lowerToRenderPlan(cameraOnlyScene, staticLensOptions); }),
          "a static unsupported iris/highlight lens is rejected before renderer admission");
    staticLensRig.setIrisBlades(0).setHighlightGain(0.f);
    const auto staticLensPlan = lowerToRenderPlan(cameraOnlyScene, staticLensOptions);
    check(staticLensPlan.camera && staticLensPlan.camera->dof_enabled,
          "a fully static lens still enables DOF in the lowered base camera");
    check(staticLensPlan.camera && staticLensPlan.camera->dof_f_stop == 2.8f &&
          staticLensPlan.camera->dof_focus_distance == 480.f &&
          staticLensPlan.camera->dof_max_blur == 30.f &&
          staticLensPlan.camera->dof_iris_blades == 0 &&
          staticLensPlan.camera->dof_highlight_gain == 0.f,
          "every statically authored lens field reaches the lowered base camera");
    (void)loweredCameraPoseDof;
    check(throwsInvalid([&] {
        const std::array<BakedPath, 2> missingIdentity{
            parseSvgPath("M8 0 L13 0"), parseSvgPath("M16 0 L21 0")};
        applyBakedPathGeometry(bakedShape, parseSvgPath("M0 0 L5 0"), missingIdentity);
    }), "repeater adapter rejects copies without the unmodified source at index zero");
    check(throwsInvalid([&] {
        MotionScene emptyScene("camera only", 30.f);
        CameraRig tooMany(41);
        tooMany.positionTrack().add(0.f, Vector3{});
        tooMany.positionTrack().add(65536.f / 30.f, Vector3{});
        RenderPlanLoweringOptions tooManyOptions;
        tooManyOptions.cameraRig = &tooMany;
        (void)lowerToRenderPlan(emptyScene, tooManyOptions);
    }), "camera-only output obeys the RenderPlan camera keyframe budget");
}
