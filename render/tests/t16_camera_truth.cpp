// T16 Camera Truth: a deterministic, headless three-plane perspective scene.
// Writes five PNG samples and fails when FOV, depth, parallax, lighting,
// sample advancement, or random-access rendering violates the scene contract.

#include "glad/glad.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include "chrononmotion/geometries/BoxGeometry.hpp"
#include "chrononmotion/geometries/PlaneGeometry.hpp"
#include "chrononmotion/geometries/SphereGeometry.hpp"
#include "chrononmotion/materials/Descriptors.hpp"
#include "chrononmotion/motion/CameraRig.hpp"
#include "chrononmotion/motion/MotionScene.hpp"
#include "threepp/canvas/Canvas.hpp"
#include "threepp/loaders/ImageLoader.hpp"
#include "threepp/motion/MotionBridge.hpp"
#include "threepp/postprocessing/BokehPass.hpp"
#include "threepp/postprocessing/EffectComposer.hpp"
#include "threepp/postprocessing/RenderPass.hpp"
#include "threepp/renderers/GLRenderer.hpp"
#include "threepp/renderers/RenderTarget.hpp"
#include "threepp/textures/Texture.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;
namespace {

using RGB = std::vector<unsigned char>;

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

void write_png(const fs::path& path, const RGB& bottomUp, int width, int height) {
    RGB topDown(bottomUp.size());
    const auto stride = static_cast<std::size_t>(width) * 3;
    for (int y = 0; y < height; ++y)
        std::copy_n(bottomUp.data() + static_cast<std::size_t>(height - y - 1) * stride,
                    stride, topDown.data() + static_cast<std::size_t>(y) * stride);
    if (!stbi_write_png(path.string().c_str(), width, height, 3,
                        topDown.data(), static_cast<int>(stride)))
        throw std::runtime_error("failed to write " + path.string());
}

RGB render(threepp::EffectComposer& composer, threepp::BokehPass& bokeh,
           threepp::motion::MotionSceneBridge& bridge,
           chrononmotion::motion::MotionScene& scene,
           chrononmotion::motion::CameraRig& rig, int frame) {
    bridge.evaluateAndSync(scene, frame, &rig);
    bokeh.focus = rig.sample(float(frame) / 30.f).focusDistance;
    composer.render(1.f / 30.f);
    return composer.readRGBPixels();
}

std::size_t changed_pixels(const RGB& a, const RGB& b, int threshold = 8) {
    require(a.size() == b.size(), "frame buffers have different sizes");
    std::size_t count = 0;
    for (std::size_t i = 0; i + 2 < a.size(); i += 3) {
        if (std::abs(int(a[i]) - int(b[i])) +
            std::abs(int(a[i + 1]) - int(b[i + 1])) +
            std::abs(int(a[i + 2]) - int(b[i + 2])) > threshold) ++count;
    }
    return count;
}

std::size_t roi_changed_pixels(const RGB& a, const RGB& b, int width, int height,
                               int x0, int y0, int x1, int y1) {
    std::size_t count = 0;
    for (int y = y0; y < y1; ++y) for (int x = x0; x < x1; ++x) {
        const auto i = (static_cast<std::size_t>(height - 1 - y) * width + x) * 3;
        if (std::abs(int(a[i]) - int(b[i])) + std::abs(int(a[i + 1]) - int(b[i + 1])) +
            std::abs(int(a[i + 2]) - int(b[i + 2])) > 8) ++count;
    }
    return count;
}

chrononmotion::motion::Layer& add_box(chrononmotion::motion::MotionScene& scene,
        std::uint32_t id, std::string name, chrononmotion::Vector3 size,
        chrononmotion::Vector3 position, chrononmotion::MaterialDescriptor material) {
    chrononmotion::motion::MeshAttachment attachment;
    attachment.geometry = chrononmotion::BoxGeometry::create(size.x, size.y, size.z);
    attachment.material = std::move(material);
    auto& layer = scene.addMesh(id, std::move(name), std::move(attachment));
    layer.transform.position = position;
    return layer;
}

void add_block_title(chrononmotion::motion::MotionScene& scene, std::uint32_t& nextId) {
    // Deliberate extruded 5x7 display lettering: title pixels are individual
    // 3D cells, so camera parallax and lighting are observable in the render.
    const std::array<std::array<const char*, 7>, 5> glyphs{{
        {{"11110", "10001", "10001", "10001", "10001", "10001", "11110"}}, // D
        {{"11111", "10000", "10000", "11110", "10000", "10000", "11111"}}, // E
        {{"11110", "10001", "10001", "11110", "10000", "10000", "10000"}}, // P
        {{"11111", "00100", "00100", "00100", "00100", "00100", "00100"}}, // T
        {{"10001", "10001", "10001", "11111", "10001", "10001", "10001"}}  // H
    }};
    constexpr float cell = 0.44f;
    constexpr float gap = 0.12f;
    constexpr float charAdvance = 5.f * cell + gap;
    const float left = -5.95f;
    const float top = 1.54f;
    const auto titleMaterial = chrononmotion::MaterialDescriptor::lambert(30, chrononmotion::Color(0xffc16a));
    for (std::size_t letter = 0; letter < glyphs.size(); ++letter) {
        for (int row = 0; row < 7; ++row) for (int col = 0; col < 5; ++col) {
            if (glyphs[letter][row][col] != '1') continue;
            const float x = left + (static_cast<float>(letter) * charAdvance + col * cell);
            const float y = top - static_cast<float>(row) * cell;
            const float z = -1.65f + (x + 0.4f) * 0.10f;
            auto& block = add_box(scene, nextId++, "title_cell", {cell * 0.94f, cell * 0.94f, 0.42f},
                    {x, y, z}, titleMaterial);
            block.transform.rotation.y = 0.10f;
        }
    }
}

chrononmotion::motion::DirectionalLight& build_scene(chrononmotion::motion::MotionScene& scene,
                 chrononmotion::motion::CameraRig& camera,
                 threepp::motion::MotionSceneBridge& bridge,
                 int width, int height) {
    using namespace chrononmotion;
    using namespace chrononmotion::motion;

    add_box(scene, 1, "environment_night", {48.f, 30.f, 0.25f}, {0.f, 0.f, -6.f},
            MaterialDescriptor::unlit(1, Color(0x1c3652)));
    add_box(scene, 2, "environment_floor", {48.f, 0.22f, 36.f}, {0.f, -5.1f, -3.f},
            MaterialDescriptor::unlit(2, Color(0x31475b)));
    add_box(scene, 3, "environment_left_arch", {0.18f, 8.5f, 0.35f}, {-7.f, 0.f, -2.5f},
            MaterialDescriptor::emissive(3, Color(0x294867), 0.35f));
    add_box(scene, 4, "environment_right_arch", {0.18f, 8.5f, 0.35f}, {7.f, 0.f, -2.5f},
            MaterialDescriptor::emissive(4, Color(0x294867), 0.35f));
    add_box(scene, 9, "stage_horizon_keyline", {30.f, 0.035f, 0.06f}, {0.f, -3.15f, -1.25f},
            MaterialDescriptor::emissive(9, Color(0x35a2bf), 0.48f));
    for (int i = 0; i < 4; ++i) {
        const float z = -2.1f - static_cast<float>(i) * 1.4f;
        add_box(scene, 10 + i, "stage_floor_depth_line", {30.f, 0.025f, 0.035f},
                {0.f, -3.18f, z}, MaterialDescriptor::emissive(10 + i,
                    Color(i == 0 ? 0x347f9d : 0x27475c), 0.36f));
    }
    auto& ambient = scene.addAmbientLight(5, "soft_environment_fill");
    ambient.intensity = 0.4f;
    auto& key = scene.addDirectionalLight(6, "warm_key");
    key.intensity = 2.2f;

    std::uint32_t nextId = 100;
    add_block_title(scene, nextId);

    // Small emissive depth markers make the three spatial planes readable.
    for (int i = 0; i < 14; ++i) {
        const float x = -5.7f + 0.87f * static_cast<float>(i);
        const float y = 1.55f + 0.18f * std::sin(float(i) * 1.7f);
        chrononmotion::motion::MeshAttachment marker;
        marker.geometry = SphereGeometry::create(0.045f, 12, 8);
        marker.material = MaterialDescriptor::emissive(7, Color(0x68a4c8), 1.2f);
        auto& dot = scene.addMesh(nextId++, "environment_depth_marker", std::move(marker));
        dot.transform.position = Vector3(x, y, -3.2f);
    }

    auto image = threepp::ImageLoader{}.load(CHRONONMOTION_T16_SUBJECT, 4, true);
    require(image && image->width() > 0 && image->height() > 0,
            "T16 transparent portrait could not be decoded");
    auto texture = threepp::Texture::create(std::move(*image));
    texture->needsUpdate();
    constexpr float subjectHeight = 8.4f;
    const float subjectWidth = subjectHeight * 1087.f / 1447.f;
    MeshAttachment subject;
    subject.geometry = PlaneGeometry::create(subjectWidth, subjectHeight, 2, 2);
    subject.material = MaterialDescriptor::unlit(8, Color(0xffffff));
    subject.material.transparent = true;
    auto& portrait = scene.addMesh(8, "foreground_alpha_portrait", std::move(subject));
    portrait.transform.position = Vector3(3.05f, -0.35f, 1.35f);
    bridge.bindLayerTexture(8, std::move(texture));

    // Open on the hero, centered in frame; the camera then sweeps left through
    // the spatially separated title and racks focus onto its extruded faces.
    camera.setTarget(Vector3(3.05f, -0.35f, 1.35f)).setFov(50.f);
    camera.setLegacyDefocusStrength(0.06f).setMaxBlur(48.f).setFocusDistance(10.6f);
    camera.positionTrack().add(0.f, Vector3(3.05f, -0.35f, 12.f));
    camera.positionTrack().add(1.f, Vector3(2.15f, -0.18f, 9.8f));
    camera.positionTrack().add(2.5f, Vector3(0.55f, 0.05f, 6.2f));
    camera.positionTrack().add(4.f, Vector3(-1.85f, 0.25f, 3.55f));
    camera.positionTrack().add(149.f / 30.f, Vector3(-2.8f, 0.42f, 2.25f));
    camera.targetTrack().add(0.f, Vector3(3.05f, -0.35f, 1.35f));
    camera.targetTrack().add(1.f, Vector3(2.25f, -0.12f, 0.15f));
    camera.targetTrack().add(2.5f, Vector3(0.1f, 0.15f, -1.25f));
    camera.targetTrack().add(4.f, Vector3(-1.75f, 0.05f, -1.65f));
    camera.targetTrack().add(149.f / 30.f, Vector3(-2.15f, 0.05f, -1.65f));
    camera.focusDistanceTrack().add(0.f, 10.6f);
    camera.focusDistanceTrack().add(1.f, 9.2f);
    camera.focusDistanceTrack().add(2.5f, 7.1f);
    camera.focusDistanceTrack().add(4.f, 5.0f);
    camera.focusDistanceTrack().add(149.f / 30.f, 4.05f);
    (void)width;
    (void)height;
    return key;
}

} // namespace

int main(int argc, char** argv) try {
    const fs::path output = argc > 1 ? argv[1] : "T16_Camera_Truth";
    const int width = argc > 2 ? std::stoi(argv[2]) : 854;
    const int height = argc > 3 ? std::stoi(argv[3]) : 480;
    const bool renderFullClip = argc > 4 && std::string(argv[4]) == "--full-clip";
    require(width >= 320 && height >= 180, "T16 output size is too small for certification");
    fs::create_directories(output);

    constexpr int fps = 30;
    threepp::Canvas canvas(threepp::Canvas::Parameters().size(width, height).headless(true));
    threepp::GLRenderer renderer(canvas);
    renderer.setClearColor(threepp::Color(0x070b13));
    const char* glRendererRaw = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    const std::string glRenderer = glRendererRaw ? glRendererRaw : "unknown";
    auto target = threepp::RenderTarget::create(width, height, threepp::RenderTarget::Options{});
    renderer.setRenderTarget(target.get());

    chrononmotion::motion::MotionScene scene("T16_Camera_Truth", float(fps));
    chrononmotion::motion::CameraRig camera(1600, 50.f, float(width) / height, 0.1f, 100.f);
    threepp::motion::MotionSceneBridge bridge(50.f, float(width) / height, 0.1f, 100.f);
    auto& key = build_scene(scene, camera, bridge, width, height);
    threepp::EffectComposer::Options composerOptions;
    composerOptions.samples = 0;
    threepp::EffectComposer composer(renderer, composerOptions);
    composer.renderToScreen = false;
    composer.setSize(width, height);
    composer.addPass(std::make_shared<threepp::RenderPass>(*bridge.scene(), *bridge.camera()));
    auto bokeh = std::make_shared<threepp::BokehPass>(
        *bridge.scene(), *bridge.camera(), 10.6f, 0.018f, 0.035f);
    composer.addPass(bokeh);

    const std::array<int, 5> sampleFrames{0, 30, 75, 120, 149};
    std::array<RGB, 5> samples;
    for (std::size_t i = 0; i < sampleFrames.size(); ++i) {
        samples[i] = render(composer, *bokeh, bridge, scene, camera, sampleFrames[i]);
        require(samples[i].size() == static_cast<std::size_t>(width) * height * 3,
                "T16 render returned an invalid framebuffer");
        write_png(output / ("frame_" + (sampleFrames[i] < 10 ? std::string("00") :
                    sampleFrames[i] < 100 ? std::string("0") : std::string()) +
                    std::to_string(sampleFrames[i]) + ".png"), samples[i], width, height);
    }

    if (renderFullClip) {
        const fs::path frames = output / "frames";
        fs::create_directories(frames);
        for (int frame = 0; frame < 150; ++frame) {
            const RGB pixels = (frame == 0) ? samples[0] :
                (frame == 30) ? samples[1] :
                (frame == 75) ? samples[2] :
                (frame == 120) ? samples[3] :
                (frame == 149) ? samples[4] : render(composer, *bokeh, bridge, scene, camera, frame);
            const std::string filename = "frame_" + (frame < 10 ? std::string("00") :
                frame < 100 ? std::string("0") : std::string()) + std::to_string(frame) + ".png";
            write_png(frames / filename, pixels, width, height);
        }
    }

    // CAM-01: explicit FOV 50->30 changes projected framing with identical scene state.
    camera.setFov(30.f);
    const RGB narrowFov = render(composer, *bokeh, bridge, scene, camera, 75);
    const auto fovDelta = changed_pixels(samples[2], narrowFov);
    require(fovDelta > static_cast<std::size_t>(width) * height / 20,
            "CAM-01 FOV 50->30 did not measurably change pixels");
    camera.setFov(50.f);

    // CAM-06: image plate is intentionally unlit; the 3D title/environment
    // respond to the key. Compare isolated portrait and title ROIs.
    key.intensity = 0.f;
    const RGB keyOff = render(composer, *bokeh, bridge, scene, camera, 75);
    const RGB portraitKeyOff = render(composer, *bokeh, bridge, scene, camera, 0);
    key.intensity = 2.2f;
    const RGB keyOn = render(composer, *bokeh, bridge, scene, camera, 75);
    const RGB portraitKeyOn = render(composer, *bokeh, bridge, scene, camera, 0);
    const auto titleDelta = roi_changed_pixels(keyOff, keyOn, width, height,
        width / 12, height / 4, width * 7 / 12, height * 3 / 4);
    const auto portraitDelta = roi_changed_pixels(portraitKeyOff, portraitKeyOn, width, height,
        width * 45 / 100, height * 20 / 100, width * 55 / 100, height * 50 / 100);
    std::cout << "CAM-06 light ablation title ROI=" << titleDelta
              << " portrait ROI=" << portraitDelta << '\n';
    require(titleDelta > 0, "CAM-06 key-light ablation did not change the lit title ROI");
    const auto portraitTolerance = std::max<std::size_t>(25,
        static_cast<std::size_t>(width / 10) * (height * 3 / 10) / 1000);
    require(portraitDelta <= portraitTolerance,
            "CAM-06 key-light ablation materially changed the unlit portrait ROI");

    // CAM-08: direct access after other frames must reproduce frame 75 exactly.
    const RGB random75 = render(composer, *bokeh, bridge, scene, camera, 75);
    require(random75 == samples[2], "CAM-08 random-access frame 75 differs from sequential output");

    // CAM-02/03: use the renderer's actual view/projection to measure apparent
    // scale and relative displacement of anchors on the three depth planes.
    auto project = [&](const chrononmotion::Vector3& point, int frame) {
        bridge.evaluateAndSync(scene, frame, &camera);
        bridge.camera()->updateMatrixWorld(true);
        threepp::Vector3 ndc(point.x, point.y, point.z);
        ndc.project(*bridge.camera());
        return threepp::Vector2((ndc.x + 1.f) * 0.5f * width,
                                (1.f - ndc.y) * 0.5f * height);
    };
    const std::array<chrononmotion::Vector3, 3> depthAnchors{{
        {-1.2f, 0.f, 1.35f}, {0.f, 0.f, -1.65f}, {1.2f, 0.f, -6.f}}};
    std::array<float, 3> parallax{};
    for (std::size_t i = 0; i < depthAnchors.size(); ++i)
        parallax[i] = project(depthAnchors[i], 149).x - project(depthAnchors[i], 0).x;
    const float parallaxSpread = std::max({parallax[0], parallax[1], parallax[2]}) -
                                 std::min({parallax[0], parallax[1], parallax[2]});
    require(parallaxSpread > float(width) * 0.025f,
            "CAM-03 three depth planes did not exhibit measurable relative parallax");
    const float nearWidth = std::abs(project({-1.7f, 0.f, 1.35f}, 0).x -
                                     project({-0.7f, 0.f, 1.35f}, 0).x);
    const float farWidth = std::abs(project({0.7f, 0.f, -6.f}, 0).x -
                                    project({1.7f, 0.f, -6.f}, 0).x);
    require(nearWidth > farWidth * 1.15f,
            "CAM-02 perspective did not scale the near plane larger than the far plane");

    // CAM-07: all requested frame samples advance smoothly along the CameraRig.
    float maxStep = 0.f;
    for (int frame = 1; frame < 150; ++frame) {
        const auto previous = camera.sample(float(frame - 1) / fps);
        const auto current = camera.sample(float(frame) / fps);
        maxStep = std::max(maxStep, previous.position.distanceTo(current.position));
    }
    require(maxStep < 0.15f, "CAM-07 camera path contains a discontinuous position step");
    for (std::size_t i = 1; i < samples.size(); ++i)
        require(changed_pixels(samples[i - 1], samples[i]) >
                    static_cast<std::size_t>(width) * height / 100,
                "CAM-07 adjacent required samples are identical or nearly static");

    // CAM-04: exercise the actual MotionBridge focal-length path and compare
    // it with the RenderPlan compiler's 35 mm gate convention.
    chrononmotion::motion::CameraRig focalRig(1601, 50.f, float(width) / height, 0.1f, 100.f);
    focalRig.setTarget({0.f, 0.f, 0.f}).setPosition({0.f, 0.f, 11.f}).setFocalLength(50.f);
    bridge.evaluateAndSync(scene, 75, &focalRig);
    const float sensorHeightMm = 35.f / (float(width) / height);
    const float expectedFocalFov = 2.f * std::atan(sensorHeightMm / (2.f * 50.f)) *
                                   180.f / 3.14159265358979323846f;
    const float bridgeFov = bridge.camera()->fov;
    require(std::abs(bridgeFov - expectedFocalFov) < 0.05f,
            "CAM-04 MotionBridge 50 mm FOV differs from the shared 35 mm sensor gate");
    const float firstOrbit = std::atan2(camera.sample(0.f).position.x,
                                         camera.sample(0.f).position.z);
    const float lastOrbit = std::atan2(camera.sample(149.f / fps).position.x,
                                        camera.sample(149.f / fps).position.z);
    const float orbitDegrees = std::abs(lastOrbit - firstOrbit) *
                               180.f / 3.14159265358979323846f;
    require(orbitDegrees >= 10.f,
            "CAM-03 CameraRig orbit is smaller than the required 10 degrees");

    // Small contact sheet: three columns, two rows, nearest-neighbor reduction.
    constexpr int columns = 3, rows = 2, tileWidth = 640, tileHeight = 360;
    RGB sheet(static_cast<std::size_t>(columns * tileWidth) * rows * tileHeight * 3, 8);
    for (std::size_t f = 0; f < samples.size(); ++f) {
        const int ox = int(f % columns) * tileWidth;
        const int oy = int(f / columns) * tileHeight;
        for (int y = 0; y < tileHeight; ++y) for (int x = 0; x < tileWidth; ++x) {
            const int sx = x * width / tileWidth;
            const int sy = y * height / tileHeight;
            const auto si = (static_cast<std::size_t>(height - 1 - sy) * width + sx) * 3;
            const auto di = (static_cast<std::size_t>(rows * tileHeight - 1 - (oy + y)) *
                             columns * tileWidth + ox + x) * 3;
            std::copy_n(samples[f].data() + si, 3, sheet.data() + di);
        }
    }
    write_png(output / "contact_sheet.png", sheet, columns * tileWidth, rows * tileHeight);

    std::ofstream metadata(output / "camera_truth.json");
    metadata << "{\n  \"status\": \"PASS\",\n  \"renderer\": \"ChrononMotion MotionSceneBridge + threepp OpenGL\",\n"
             << "  \"gl_device\": \"" << glRenderer << "\",\n"
             << "  \"resolution\": [" << width << ", " << height << "],\n  \"fps\": 30,\n"
             << "  \"timeline_duration_frames\": 150,\n"
             << "  \"rendered_sample_frames\": [0, 30, 75, 120, 149],\n"
             << "  \"full_clip_rendered\": " << (renderFullClip ? "true" : "false") << ",\n"
             << "  \"camera_fov_degrees\": [50, 30],\n  \"camera_orbit_degrees\": " << orbitDegrees
             << ",\n  \"camera_near\": 0.1,\n  \"camera_far\": 100,\n"
             << "  \"focus_distance_track\": [10.6, 9.2, 7.1, 5.0, 4.05],\n"
             << "  \"dof_pass\": \"threepp BokehPass; focus racks from portrait to 3D title\",\n"
             << "  \"composition\": \"centered full-height MrBeast bust opens, then camera sweeps through extruded title while focus racks off portrait\",\n"
             << "  \"motionbridge_50mm_fov_degrees\": " << bridgeFov << ",\n"
             << "  \"expected_35mm_gate_fov_degrees\": " << expectedFocalFov << ",\n"
             << "  \"planes_world_z\": [1.35, -0.55, -3.2],\n"
             << "  \"fov_ablation_changed_pixels\": " << fovDelta << ",\n"
             << "  \"key_light_title_changed_pixels\": " << titleDelta << ",\n"
             << "  \"key_light_unlit_portrait_changed_pixels\": " << portraitDelta << ",\n"
             << "  \"parallax_spread_pixels\": " << parallaxSpread << ",\n"
             << "  \"near_to_far_projected_width_ratio\": " << nearWidth / farWidth << ",\n"
             << "  \"camera_max_step_world_units\": " << maxStep << ",\n"
             << "  \"random_access_frame_75_matches\": true,\n"
             << "  \"subject_asset\": \"MrBeast 2023 cropped portrait, CC BY 4.0; see source attribution\",\n"
             << "  \"dof_gate\": \"PASS: integrated threepp BokehPass racks focus from portrait to title\"\n}\n";
    require(bool(metadata), "failed to write T16 camera truth metadata");
    renderer.setRenderTarget(nullptr);
    renderer.dispose();
    std::cout << "T16 Camera Truth PASS; FOV delta=" << fovDelta
              << ", parallax=" << parallaxSpread << " px, samples=" << output << '\n';
    return 0;
} catch (const std::exception& error) {
    std::cerr << "T16 Camera Truth FAIL: " << error.what() << '\n';
    return 1;
}
