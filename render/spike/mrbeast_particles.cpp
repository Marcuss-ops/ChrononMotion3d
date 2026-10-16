// Proof & Tool: bone-rigged 3D MrBeast character with floating pack particles.
//
// Uses a transparent portrait mesh, subtle RigProgram head/chest deformation,
// parallax, and twelve original floating carton particles.
//
// Usage:
//   chrononmotion_render_mrbeast_particles [input_cutout.png] [out.mp4] [w h [frames]]

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <array>

#include "chrononmotion/geometries/PlaneGeometry.hpp"
#include "threepp/geometries/SphereGeometry.hpp"
#include "chrononmotion/materials/Descriptors.hpp"
#include "chrononmotion/motion/CameraRig.hpp"
#include "chrononmotion/motion/Easing.hpp"
#include "chrononmotion/motion/MotionScene.hpp"

#include "threepp/canvas/Canvas.hpp"
#include "threepp/core/BufferGeometry.hpp"
#include "threepp/core/BufferAttribute.hpp"
#include "threepp/geometries/BoxGeometry.hpp"
#include "threepp/loaders/ImageLoader.hpp"
#include "threepp/materials/MeshPhongMaterial.hpp"
#include "threepp/lights/AmbientLight.hpp"
#include "threepp/objects/Bone.hpp"
#include "threepp/lights/DirectionalLight.hpp"
#include "threepp/objects/Group.hpp"
#include "threepp/objects/Mesh.hpp"
#include "threepp/motion/MotionBridge.hpp"
#include "threepp/renderers/GLRenderer.hpp"
#include "threepp/renderers/RenderTarget.hpp"
#include "threepp/textures/DataTexture.hpp"

namespace {

struct MrBeastRig3D {
    std::shared_ptr<threepp::Group> root = threepp::Group::create();
    std::shared_ptr<threepp::Bone> hips = threepp::Bone::create();
    std::shared_ptr<threepp::Bone> spine = threepp::Bone::create();
    std::shared_ptr<threepp::Bone> neck = threepp::Bone::create();
    std::shared_ptr<threepp::Bone> head = threepp::Bone::create();
    std::shared_ptr<threepp::Bone> leftShoulder = threepp::Bone::create();
    std::shared_ptr<threepp::Bone> leftElbow = threepp::Bone::create();
    std::shared_ptr<threepp::Bone> rightShoulder = threepp::Bone::create();
    std::shared_ptr<threepp::Bone> rightElbow = threepp::Bone::create();

    void animate(float t, float duration) {
        const float phase = 6.28318530718f * t / duration;
        head->rotation.y = 0.10f * std::sin(phase);
        head->rotation.x = 0.025f * std::sin(phase * 2.f);
        spine->rotation.z = 0.018f * std::sin(phase);
        leftShoulder->rotation.z = -0.04f + 0.035f * std::sin(phase);
        rightShoulder->rotation.z = 0.10f + 0.045f * std::sin(phase + 0.6f);
        leftElbow->rotation.z = -0.04f + 0.025f * std::sin(phase + 1.f);
        rightElbow->rotation.z = -0.12f + 0.035f * std::sin(phase + 0.8f);
    }
};

std::shared_ptr<threepp::MeshPhongMaterial> phong(int color, float shininess = 18.f) {
    return threepp::MeshPhongMaterial::create(
        threepp::MeshPhongMaterial::Params{}.color(threepp::Color(color)).shininess(shininess));
}

std::shared_ptr<threepp::Mesh> addEllipsoid(
    threepp::Object3D& parent, const std::shared_ptr<threepp::Material>& material,
    const chrononmotion::Vector3& position, const chrononmotion::Vector3& scale,
    const std::string& name) {
    auto mesh = threepp::Mesh::create(threepp::SphereGeometry::create(1.f, 32, 24), material);
    mesh->position.set(position.x, position.y, position.z);
    mesh->scale.set(scale.x, scale.y, scale.z);
    mesh->name = name;
    parent.add(mesh);
    return mesh;
}

std::shared_ptr<threepp::BufferGeometry> makeCurvedFacePatch() {
    constexpr int nx = 48, ny = 64;
    constexpr float rx = 0.335f, ry = 0.465f, rz = 0.265f;
    constexpr float cy = 0.f; // Face patch is authored in head-bone local space.
    std::vector<float> positions, uvs;
    std::vector<unsigned int> indices;
    positions.reserve((nx + 1) * (ny + 1) * 3);
    uvs.reserve((nx + 1) * (ny + 1) * 2);
    for (int y = 0; y <= ny; ++y) {
        const float v = static_cast<float>(y) / ny;
        const float py = (v - 0.5f) * 2.f * ry;
        for (int x = 0; x <= nx; ++x) {
            const float u = static_cast<float>(x) / nx;
            const float px = (u - 0.5f) * 2.f * rx;
            const float r2 = (px * px) / (rx * rx) + (py * py) / (ry * ry);
            const float pz = 0.065f + rz * std::sqrt(std::max(0.035f, 1.f - r2));
            positions.insert(positions.end(), {px, cy + py, pz});
            // Crop the source portrait's hairline-to-chin region directly in UV space.
            // Image UVs use a bottom-left origin, while the crop bounds above
            // are measured from the source image's top edge.
            uvs.insert(uvs.end(), {0.235f + u * 0.545f, 0.415f + v * 0.535f});
        }
    }
    for (int y = 0; y < ny; ++y) for (int x = 0; x < nx; ++x) {
        const unsigned a = static_cast<unsigned>(y * (nx + 1) + x);
        const unsigned b = a + 1, c = a + nx + 1, d = c + 1;
        indices.insert(indices.end(), {a, b, c, b, d, c});
    }
    auto geometry = std::make_shared<threepp::BufferGeometry>();
    geometry->setAttribute("position", threepp::FloatBufferAttribute::create(std::move(positions), 3));
    geometry->setAttribute("uv", threepp::FloatBufferAttribute::create(std::move(uvs), 2));
    geometry->setIndex(std::move(indices));
    geometry->computeVertexNormals();
    return geometry;
}

MrBeastRig3D buildMrBeastRig(const std::shared_ptr<threepp::Texture>& portrait) {
    MrBeastRig3D rig;
    rig.root->name = "MrBeast_Rig_ROOT";
    rig.hips->name = "hips";
    rig.spine->name = "spine_chest";
    rig.neck->name = "neck";
    rig.head->name = "head";
    rig.leftShoulder->name = "shoulder_L";
    rig.leftElbow->name = "elbow_L";
    rig.rightShoulder->name = "shoulder_R";
    rig.rightElbow->name = "elbow_R";

    const auto shirt = phong(0x111820, 10.f);
    const auto skin = phong(0xe3ae91, 24.f);
    const auto hair = phong(0x684027, 12.f);
    const auto accent = phong(0x20b8e7, 30.f);

    rig.root->add(rig.hips);
    rig.hips->add(rig.spine);
    rig.spine->position.y = 0.10f;
    rig.spine->add(rig.neck);
    rig.neck->position.y = 0.39f;
    rig.neck->add(rig.head);
    rig.head->position.y = 0.08f;

    // Volumetric torso, collar and chest detail.
    addEllipsoid(*rig.spine, shirt, {0.f, -0.43f, 0.f}, {0.58f, 0.70f, 0.32f}, "hoodie_torso");
    addEllipsoid(*rig.spine, shirt, {0.f, 0.02f, 0.f}, {0.53f, 0.22f, 0.30f}, "shoulder_line");
    addEllipsoid(*rig.neck, skin, {0.f, -0.03f, 0.f}, {0.19f, 0.27f, 0.19f}, "neck_mesh");
    addEllipsoid(*rig.head, hair, {0.f, 0.f, 0.f}, {0.36f, 0.475f, 0.29f}, "hair_and_skull_volume");

    // The source portrait is wrapped onto a curved facial surface that follows the 3D skull.
    auto faceMat = threepp::MeshPhongMaterial::create(
        threepp::MeshPhongMaterial::Params{}.map(portrait).shininess(8.f));
    faceMat->transparent = true;
    auto face = threepp::Mesh::create(makeCurvedFacePatch(), faceMat);
    face->name = "MrBeast_curved_face_surface";
    rig.head->add(face);

    // Real bone hierarchy drives independently modeled 3D upper arms and forearms.
    rig.spine->add(rig.leftShoulder);
    rig.leftShoulder->position.set(-0.48f, -0.03f, 0.f);
    rig.leftShoulder->add(rig.leftElbow);
    rig.leftElbow->position.set(-0.03f, -0.39f, 0.f);
    rig.spine->add(rig.rightShoulder);
    rig.rightShoulder->position.set(0.48f, -0.03f, 0.f);
    rig.rightShoulder->add(rig.rightElbow);
    rig.rightElbow->position.set(0.03f, -0.39f, 0.f);
    for (const auto& shoulder : {rig.leftShoulder, rig.rightShoulder}) {
        addEllipsoid(*shoulder, shirt, {0.f, -0.20f, 0.f}, {0.145f, 0.28f, 0.17f}, "upper_arm_mesh");
        addEllipsoid(*shoulder, accent, {0.f, -0.15f, 0.16f}, {0.09f, 0.11f, 0.018f}, "sleeve_badge");
    }
    for (const auto& elbow : {rig.leftElbow, rig.rightElbow}) {
        addEllipsoid(*elbow, shirt, {0.f, -0.19f, 0.f}, {0.115f, 0.25f, 0.135f}, "forearm_mesh");
        addEllipsoid(*elbow, skin, {0.f, -0.40f, 0.025f}, {0.10f, 0.105f, 0.105f}, "hand_mesh");
    }
    // Cyan beast-like chest badge adds the creator's recognizable color cue.
    addEllipsoid(*rig.spine, accent, {0.27f, -0.47f, 0.285f}, {0.105f, 0.09f, 0.025f}, "cyan_chest_badge");
    return rig;
}

bool isImagePath(const std::string& s) {
    auto lower = s;
    for (char& c : lower) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    const auto endsWith = [&](const char* ext) {
        const std::size_t len = std::char_traits<char>::length(ext);
        return lower.size() >= len && lower.compare(lower.size() - len, len, ext) == 0;
    };
    return endsWith(".png") || endsWith(".jpg") || endsWith(".jpeg") || endsWith(".webp");
}

bool endsWithMp4(const std::string& s) {
    if (s.size() < 4) return false;
    std::string tail = s.substr(s.size() - 4);
    for (char& c : tail) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return tail == ".mp4";
}

// Procedural dark slate atmospheric backdrop matching the reference video
// with deep charcoal radial gradient and subtle bokeh light motes
std::shared_ptr<threepp::DataTexture> makeAtmosphericBackdropTexture(int texW, int texH) {
    std::vector<unsigned char> rgb(static_cast<std::size_t>(texW * texH * 3));
    for (int y = 0; y < texH; ++y) {
        const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(texH);
        for (int x = 0; x < texW; ++x) {
            const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(texW);
            const float dx = (u - 0.5f) * 1.6f;
            const float dy = (v - 0.5f) * 1.2f;
            const float dist2 = dx * dx + dy * dy;

            // Base slate gradient
            const float vignette = std::clamp(1.0f - dist2 * 0.55f, 0.0f, 1.0f);
            float r = (32.f + 25.f * vignette) * (0.85f + 0.3f * v);
            float g = (38.f + 30.f * vignette) * (0.85f + 0.3f * v);
            float b = (46.f + 40.f * vignette) * (0.85f + 0.3f * v);

            // Subtle glowing bokeh spots in background (matching reference particles)
            const float spot1 = std::exp(-((u - 0.22f) * (u - 0.22f) + (v - 0.68f) * (v - 0.68f)) / 0.004f);
            const float spot2 = std::exp(-((u - 0.78f) * (u - 0.78f) + (v - 0.42f) * (v - 0.42f)) / 0.008f);
            const float spot3 = std::exp(-((u - 0.85f) * (u - 0.85f) + (v - 0.20f) * (v - 0.20f)) / 0.006f);
            const float spot4 = std::exp(-((u - 0.15f) * (u - 0.15f) + (v - 0.35f) * (v - 0.35f)) / 0.003f);

            r += spot1 * 80.f + spot2 * 50.f + spot3 * 75.f + spot4 * 65.f;
            g += spot1 * 95.f + spot2 * 60.f + spot3 * 85.f + spot4 * 75.f;
            b += spot1 * 130.f + spot2 * 90.f + spot3 * 115.f + spot4 * 105.f;

            const std::size_t idx = static_cast<std::size_t>((y * texW + x) * 3);
            rgb[idx + 0] = static_cast<unsigned char>(std::clamp(r, 0.f, 255.f));
            rgb[idx + 1] = static_cast<unsigned char>(std::clamp(g, 0.f, 255.f));
            rgb[idx + 2] = static_cast<unsigned char>(std::clamp(b, 0.f, 255.f));
        }
    }
    auto tex = threepp::DataTexture::create(rgb, texW, texH);
    tex->format = threepp::Format::RGB;
    tex->needsUpdate();
    return tex;
}

// Small, original snack-style cartons used as floating 2.5D props.
std::shared_ptr<threepp::DataTexture> makeParticlePackTexture(
    int texW, int texH, const std::array<unsigned char, 3>& base) {
    std::vector<unsigned char> rgba(static_cast<std::size_t>(texW * texH * 4));
    for (int y = 0; y < texH; ++y) {
        for (int x = 0; x < texW; ++x) {
            const bool edge = x < 4 || x >= texW - 4 || y < 4 || y >= texH - 4;
            const bool topBand = y < texH / 7;
            const bool label = x > texW / 6 && x < texW * 5 / 6 &&
                               y > texH / 3 && y < texH * 3 / 4;
            const bool stripe = (x + y / 2) % 19 < 3;
            const std::size_t i = static_cast<std::size_t>((y * texW + x) * 4);
            const float light = (topBand || stripe) ? 1.22f : 1.0f;
            for (int c = 0; c < 3; ++c) {
                float channel = static_cast<float>(base[static_cast<std::size_t>(c)]) * light;
                if (label) channel = c == 0 ? 248.f : (c == 1 ? 242.f : 226.f);
                if (edge) channel *= 0.55f;
                rgba[i + static_cast<std::size_t>(c)] = static_cast<unsigned char>(std::clamp(channel, 0.f, 255.f));
            }
            rgba[i + 3] = 255;
        }
    }
    auto texture = threepp::DataTexture::create(rgba, texW, texH);
    texture->format = threepp::Format::RGBA;
    texture->needsUpdate();
    return texture;
}

std::size_t countDiffPixels(const std::vector<unsigned char>& a,
                            const std::vector<unsigned char>& b,
                            int threshold = 8) {
    if (a.size() != b.size()) return 0;
    std::size_t diffCount = 0;
    for (std::size_t i = 0; i < a.size(); i += 3) {
        const int dr = std::abs(static_cast<int>(a[i + 0]) - static_cast<int>(b[i + 0]));
        const int dg = std::abs(static_cast<int>(a[i + 1]) - static_cast<int>(b[i + 1]));
        const int db = std::abs(static_cast<int>(a[i + 2]) - static_cast<int>(b[i + 2]));
        if (dr > threshold || dg > threshold || db > threshold) {
            ++diffCount;
        }
    }
    return diffCount;
}

} // namespace

int main(int argc, char** argv) {
    std::string inputImagePath = std::string(CHRONONMOTION_SPIKE_ASSET_DIR) + "/mrbeast_chromo/mrbeast_cutout.png";
    std::string outPath = "mrbeast_particles_rig_1920x1080_10s.mp4";
    int width = 1920;
    int height = 1080;
    int totalFrames = 301; // 10.0 seconds @ 30 fps, seamless loop

    int argIdx = 1;
    if (argc >= 3 && isImagePath(argv[1])) {
        inputImagePath = argv[1];
        outPath = argv[2];
        argIdx = 3;
    } else if (argc >= 2 && !isImagePath(argv[1]) && (endsWithMp4(argv[1]) || argv[1][0] != '-')) {
        outPath = argv[1];
        argIdx = 2;
    }
    if (argc > argIdx) width = std::atoi(argv[argIdx++]);
    if (argc > argIdx) height = std::atoi(argv[argIdx++]);
    if (argc > argIdx) totalFrames = std::max(4, std::atoi(argv[argIdx++]));

    const float fps = 30.f;
    const float durationSec = static_cast<float>(totalFrames - 1) / fps;

    // Use the visible GLX path when a display (including Xvfb) exists;
    // otherwise fall back to the renderer's headless context.
    const char* displayName = std::getenv("DISPLAY");
    const bool useHeadlessContext = displayName == nullptr || displayName[0] == '\0';
    threepp::Canvas canvas(threepp::Canvas::Parameters().size(width, height).headless(useHeadlessContext));
    threepp::GLRenderer renderer(canvas);
    renderer.setClearColor(threepp::Color(0x161b22));

    auto target = threepp::RenderTarget::create(width, height, threepp::RenderTarget::Options{});
    renderer.setRenderTarget(target.get());

    // Load the licensed source portrait as the curved face texture on the 3D character.
    threepp::ImageLoader loader;
    auto imgOpt = loader.load(inputImagePath, 4, true);
    if (!imgOpt || imgOpt->width() == 0 || imgOpt->height() == 0) {
        std::cerr << "mrbeast_particles: failed to load image " << inputImagePath << "\n";
        return 1;
    }
    std::cout << "mrbeast_particles: loaded " << inputImagePath << " ("
              << imgOpt->width() << "x" << imgOpt->height() << ")\n";
    auto mrbeastTexture = threepp::Texture::create(std::move(*imgOpt));
    mrbeastTexture->needsUpdate();

    // Author MotionScene (atmospheric studio backdrop + 3D rigged hero + camera parallax).
    chrononmotion::motion::MotionScene motionScene("mrbeast_scene", fps);

    // Atmospheric backdrop plane at z = -0.85
    const float camAspect = static_cast<float>(width) / static_cast<float>(height);
    const float bgDist = 3.8f;
    const float bgH = 2.f * std::tan(45.f * 0.5f * 3.14159265f / 180.f) * bgDist * 1.35f;
    const float bgW = bgH * camAspect * 1.35f;
    chrononmotion::motion::MeshAttachment bgAttachment;
    bgAttachment.geometry = chrononmotion::PlaneGeometry::create(bgW, bgH, 2, 2);
    bgAttachment.material = chrononmotion::MaterialDescriptor::unlit(2, chrononmotion::Color(0xffffff));
    const chrononmotion::motion::Layer::Id bgLayerId = 5;
    auto& bgLayer = motionScene.addMesh(bgLayerId, "atmospheric_backdrop", std::move(bgAttachment));
    bgLayer.tracks.position.add(0.f, chrononmotion::Vector3(0.f, 0.f, -0.85f), chrononmotion::motion::Easing::easeInOut());
    bgLayer.tracks.position.add(durationSec * 0.5f, chrononmotion::Vector3(-0.045f, -0.02f, -0.85f), chrononmotion::motion::Easing::easeInOut());
    bgLayer.tracks.position.add(durationSec, chrononmotion::Vector3(0.f, 0.f, -0.85f));

    struct PackParticle {
        chrononmotion::motion::Layer::Id id;
        std::shared_ptr<threepp::DataTexture> texture;
    };
    std::vector<PackParticle> packParticles;
    const std::array<std::array<unsigned char, 3>, 4> packColors{{
        {{231, 36, 67}}, {{28, 168, 224}}, {{248, 173, 42}}, {{88, 67, 210}}
    }};
    const std::array<chrononmotion::Vector3, 12> packRest{{
        {-1.15f,  0.82f, -0.24f}, { 1.12f,  0.76f, -0.18f},
        {-1.20f,  0.25f,  0.10f}, { 1.18f,  0.20f,  0.13f},
        {-1.08f, -0.32f, -0.12f}, { 1.11f, -0.38f, -0.08f},
        {-0.88f,  1.05f,  0.05f}, { 0.86f,  1.02f, -0.20f},
        {-1.26f, -0.90f,  0.08f}, { 1.24f, -0.92f,  0.05f},
        {-0.80f, -1.12f, -0.20f}, { 0.82f, -1.10f, -0.16f}
    }};
    for (std::size_t i = 0; i < packRest.size(); ++i) {
        const auto id = static_cast<chrononmotion::motion::Layer::Id>(20 + i);
        chrononmotion::motion::MeshAttachment card;
        card.geometry = chrononmotion::PlaneGeometry::create(0.30f, 0.40f, 1, 1);
        card.material = chrononmotion::MaterialDescriptor::unlit(id, chrononmotion::Color(0xffffff));
        card.material.transparent = true;
        auto& layer = motionScene.addMesh(id, "floating_pack_" + std::to_string(i), std::move(card));
        const auto start = packRest[i];
        auto middle = start;
        middle.x += (i % 2 == 0 ? 0.08f : -0.08f);
        middle.y += (i % 3 == 0 ? 0.11f : -0.09f);
        const auto rotation = chrononmotion::Vector3(0.f, 0.f, (i % 2 == 0 ? -0.13f : 0.12f));
        auto middleRotation = rotation;
        middleRotation.z += (i % 2 == 0 ? 0.08f : -0.08f);
        layer.tracks.position.add(0.f, start, chrononmotion::motion::Easing::easeInOut());
        layer.tracks.position.add(durationSec * 0.5f, middle, chrononmotion::motion::Easing::easeInOut());
        layer.tracks.position.add(durationSec, start);
        layer.tracks.rotation.add(0.f, rotation, chrononmotion::motion::Easing::easeInOut());
        layer.tracks.rotation.add(durationSec * 0.5f, middleRotation, chrononmotion::motion::Easing::easeInOut());
        layer.tracks.rotation.add(durationSec, rotation);
        packParticles.push_back(PackParticle{
            id, makeParticlePackTexture(96, 128, packColors[i % packColors.size()])});
    }

    // Parallax CameraRig: subtle push-in & cinematic drift at 1920x1080
    chrononmotion::motion::CameraRig cameraRig(
        100, 45.f, camAspect, 0.1f, 100.f);
    cameraRig.setTarget(chrononmotion::Vector3(0.f, 0.04f, 0.f));
    cameraRig.positionTrack().add(0.f, chrononmotion::Vector3(0.f, 0.02f, 2.70f), chrononmotion::motion::Easing::easeInOut());
    cameraRig.positionTrack().add(durationSec * 0.5f, chrononmotion::Vector3(0.06f, 0.04f, 2.54f), chrononmotion::motion::Easing::easeInOut());
    cameraRig.positionTrack().add(durationSec, chrononmotion::Vector3(0.f, 0.02f, 2.70f));

    // 6. Connect MotionSceneBridge and bind textures
    threepp::motion::MotionSceneBridge bridge(
        45.f, camAspect, 0.1f, 100.f);
    auto atmosphericBackdropTex = makeAtmosphericBackdropTexture(1024, 576);
    bridge.bindLayerTexture(bgLayerId, atmosphericBackdropTex);
    for (const auto& particle : packParticles) {
        bridge.bindLayerTexture(particle.id, particle.texture);
    }

    auto ambient = threepp::AmbientLight::create(threepp::Color(0xffffff), 1.45f);
    auto key = threepp::DirectionalLight::create(threepp::Color(0xffffff), 2.1f);
    key->position.set(-2.2f, 3.5f, 4.2f);
    bridge.scene()->add(ambient);
    bridge.scene()->add(key);
    auto character = buildMrBeastRig(mrbeastTexture);
    character.root->position.set(0.f, -0.08f, 0.12f);
    bridge.scene()->add(character.root);

    auto renderAtFrame = [&](int frame) {
        const float characterTime = frame == totalFrames - 1
            ? 0.f
            : static_cast<float>(frame) / fps;
        character.animate(characterTime, durationSec);
        bridge.evaluateAndSync(motionScene, frame, &cameraRig);
        renderer.render(*bridge.scene(), *bridge.camera());
        return renderer.readRGBPixels();
    };

    const int midFrame1 = totalFrames / 4;
    const int midFrame2 = totalFrames / 2;
    const int lastFrame = totalFrames - 1;

    std::cout << "mrbeast_particles: validating deformation and determinism...\n";
    const auto f0 = renderAtFrame(0);
    const auto fMid1 = renderAtFrame(midFrame1);
    const auto fMid2 = renderAtFrame(midFrame2);
    const auto fLast = renderAtFrame(lastFrame);
    const auto fMid1Repeat = renderAtFrame(midFrame1);

    const std::size_t diff0ToMid1 = countDiffPixels(f0, fMid1);
    const std::size_t diffMid1ToMid2 = countDiffPixels(fMid1, fMid2);
    const std::size_t diff0ToMid2 = countDiffPixels(f0, fMid2);

    std::cout << "mrbeast_particles: changed_pixels(0->" << midFrame1 << ")=" << diff0ToMid1
              << " changed_pixels(" << midFrame1 << "->" << midFrame2 << ")=" << diffMid1ToMid2
              << " changed_pixels(0->" << midFrame2 << ")=" << diff0ToMid2 << "\n";

    if (diff0ToMid1 < 100 || diffMid1ToMid2 < 50 || diff0ToMid2 < 100) {
        std::cerr << "mrbeast_particles: expected significant pixel motion from mesh deformation\n";
        return 1;
    }
    if (f0 != fLast) {
        std::cerr << "mrbeast_particles: expected final rest frame " << lastFrame << " to match frame 0\n";
        return 1;
    }
    if (fMid1 != fMid1Repeat) {
        std::cerr << "mrbeast_particles: out-of-order frame evaluation was not deterministic\n";
        return 1;
    }

    // 7. Render output MP4 at 1920x1080 via ffmpeg pipe
    if (endsWithMp4(outPath)) {
        const std::string cmd =
            "ffmpeg -y -loglevel error -f rawvideo -pixel_format rgb24 -video_size " +
            std::to_string(width) + "x" + std::to_string(height) +
            " -framerate 30 -i - -vf \"vflip,drawtext=text='MRBEAST  |  FLOATING PRIZES':fontsize=42:fontcolor=white:box=1:boxcolor=black@0.42:boxborderw=12:x=(w-text_w)/2:y=24,drawtext=text='TRUE 3D BONE RIG  |  PARTICLE PACKS':fontsize=25:fontcolor=0x35dcff:box=1:boxcolor=black@0.30:boxborderw=8:x=(w-text_w)/2:y=h-78\" -c:v libx264 -pix_fmt yuv420p \"" + outPath + "\"";
        FILE* pipe = popen(cmd.c_str(), "w");
        if (!pipe) {
            std::cerr << "mrbeast_particles: failed to open ffmpeg pipe\n";
            return 1;
        }
        for (int f = 0; f < totalFrames; ++f) {
            const auto pixels = renderAtFrame(f);
            std::fwrite(pixels.data(), 1, pixels.size(), pipe);
        }
        const int rc = pclose(pipe);
        if (rc != 0) {
            std::cerr << "mrbeast_particles: ffmpeg exited with " << rc << "\n";
            return 1;
        }
        std::cout << "mrbeast_particles: wrote Full HD 1080p MP4 video (" << totalFrames << " frames) to " << outPath << "\n";
    }

    return 0;
}
