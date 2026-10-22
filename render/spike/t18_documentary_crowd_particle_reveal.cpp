// T18: documentary crowd + animated GPU matte + depth-sorted particle reveal.
// Based on the certified SCENE-01 crowd/camera/shadow scene.
//
// The crowd is procedurally built from threepp primitives (icosahedron heads,
// capsule limbs) so the scene stays asset-free beyond the pinned CesiumMan.glb.
#include "gpu_certification_gate.hpp"
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <sys/resource.h>
#include <vector>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "glad/glad.h"
#ifdef CHRONONMOTION_HAS_GL_CUDA_NVENC
#include "gl_cuda_nvenc_encoder.hpp"
#endif
#include "stb_image_write.h"

#include "chrononmotion/motion/MotionScene.hpp"
#include "threepp/canvas/Canvas.hpp"
#include "threepp/core/BufferGeometry.hpp"
#include "threepp/geometries/BoxGeometry.hpp"
#include "threepp/geometries/CylinderGeometry.hpp"
#include "threepp/geometries/PlaneGeometry.hpp"
#include "threepp/geometries/SphereGeometry.hpp"
#include "threepp/lights/AmbientLight.hpp"
#include "threepp/lights/DirectionalLight.hpp"
#include "threepp/loaders/GLTFLoader.hpp"
#include "threepp/materials/MeshStandardMaterial.hpp"
#include "threepp/materials/MeshBasicMaterial.hpp"
#include "threepp/math/Box3.hpp"
#include "threepp/motion/MotionBridge.hpp"
#include "threepp/objects/Group.hpp"
#include "threepp/objects/Mesh.hpp"
#include "threepp/objects/SkinnedMesh.hpp"
#include "threepp/renderers/GLRenderer.hpp"
#include "threepp/textures/DataTexture.hpp"

using namespace threepp;
namespace fs = std::filesystem;

namespace {
constexpr int W = 1920, H = 1080, FPS = 30, FRAMES = 150;
constexpr const char* kId = "T18_DOCUMENTARY_CROWD_PARTICLE_REVEAL";
constexpr unsigned char kTolerance = 6;
constexpr int kParticleCount = 48;
constexpr int kMatteSize = 256;

void writePng(const fs::path& p, const std::vector<unsigned char>& rgb) {
    std::vector<unsigned char> top(rgb.size());
    const size_t row = size_t(W) * 3;
    for (int y = 0; y < H; ++y) std::copy_n(rgb.data() + size_t(H - y - 1) * row, row, top.data() + size_t(y) * row);
    if (!stbi_write_png(p.c_str(), W, H, 3, top.data(), int(row))) throw std::runtime_error("PNG write failed");
}

bool differs(const std::vector<unsigned char>& a, size_t i, const std::vector<unsigned char>& b) {
    return std::abs(int(a[i]) - int(b[i])) + std::abs(int(a[i + 1]) - int(b[i + 1])) +
               std::abs(int(a[i + 2]) - int(b[i + 2])) >
           kTolerance;
}

struct ShadowDiff {
    size_t changed = 0;
    size_t lowerHalf = 0;
};

ShadowDiff shadowDiff(const std::vector<unsigned char>& on, const std::vector<unsigned char>& off) {
    ShadowDiff d;
    const size_t floorRows = size_t(H * 0.6);
    for (size_t p = 0; p < size_t(W) * H; ++p) {
        const size_t i = p * 3;
        if (!differs(on, i, off)) continue;
        ++d.changed;
        if (p / W < floorRows) ++d.lowerHalf;
    }
    return d;
}

std::vector<float> boneMatrices(const std::shared_ptr<Object3D>& root) {
    std::vector<float> bones;
    root->traverse([&](Object3D& o) {
        if (auto* sk = dynamic_cast<SkinnedMesh*>(&o); sk && sk->skeleton)
            bones.insert(bones.end(), sk->skeleton->boneMatrices.begin(), sk->skeleton->boneMatrices.end());
    });
    return bones;
}

float vectorDelta(const std::vector<float>& a, const std::vector<float>& b) {
    if (a.size() != b.size()) return std::numeric_limits<float>::infinity();
    float d = 0;
    for (size_t i = 0; i < a.size(); ++i) d += std::abs(a[i] - b[i]);
    return d;
}

std::shared_ptr<DataTexture> makeRevealTexture() {
    auto texture = DataTexture::create<unsigned char>(4, kMatteSize, kMatteSize);
    texture->magFilter = Filter::Linear;
    texture->minFilter = Filter::Linear;
    texture->name = "T18_animated_iris_matte";
    return texture;
}

std::size_t updateRevealTexture(const std::shared_ptr<DataTexture>& texture, int frame) {
    auto& pixels = texture->image().data<unsigned char>();
    const float progress = std::clamp(float(frame) / 55.f, 0.f, 1.f);
    const float radius = progress * float(kMatteSize) * 0.82f;
    constexpr float feather = 5.f;
    std::size_t transparentPixels = 0;
    for (int y = 0; y < kMatteSize; ++y) {
        for (int x = 0; x < kMatteSize; ++x) {
            const float dx = float(x) + 0.5f - float(kMatteSize) * 0.5f;
            const float dy = float(y) + 0.5f - float(kMatteSize) * 0.5f;
            const float distance = std::sqrt(dx * dx + dy * dy);
            const float alpha = std::clamp((distance - radius + feather) / feather, 0.f, 1.f);
            const std::size_t i = (std::size_t(y) * kMatteSize + std::size_t(x)) * 4;
            pixels[i] = 0x0b; pixels[i + 1] = 0x0f; pixels[i + 2] = 0x18;
            pixels[i + 3] = static_cast<unsigned char>(std::lround(alpha * 255.f));
            if (alpha < 0.01f) ++transparentPixels;
        }
    }
    texture->needsUpdate();
    return transparentPixels;
}

struct Particle {
    std::shared_ptr<Mesh> mesh;
    std::shared_ptr<MeshBasicMaterial> material;
    Vector3 direction;
    float startFrame;
    float lifetime;
    float speed;
};

void updateParticles(std::vector<Particle>& particles, int frame, const Vector3& origin) {
    for (auto& particle : particles) {
        const float ageFrames = float(frame) - particle.startFrame;
        const bool active = ageFrames >= 0.f && ageFrames <= particle.lifetime * FPS;
        particle.mesh->visible = active;
        if (!active) continue;
        const float age = ageFrames / FPS;
        particle.mesh->position.copy(origin)
            .addScaledVector(particle.direction, particle.speed * age);
        particle.mesh->position.y -= 0.85f * age * age;
        particle.material->opacity = std::clamp(1.f - age / particle.lifetime, 0.f, 1.f);
    }
}

// One static low-poly mannequin, feet at y=0, total height ~1.8 world units.
std::shared_ptr<Group> makeMannequin(unsigned int seedColor, float yaw) {
    auto mat = MeshStandardMaterial::create();
    mat->color.setHex(seedColor);
    mat->roughness = 0.95f;
    mat->metalness = 0.f;

    auto darker = MeshStandardMaterial::create();
    darker->color.setHex(seedColor).multiplyScalar(0.7f);
    darker->roughness = 0.95f;
    darker->metalness = 0.f;

    auto g = Group::create();
    g->rotation.y = yaw;

    auto add = [&](std::shared_ptr<BufferGeometry> geo, float x, float y, float z,
                   std::shared_ptr<MeshStandardMaterial> material) {
        auto mesh = Mesh::create(std::move(geo), material);
        mesh->position.set(x, y, z);
        mesh->castShadow = true;
        mesh->receiveShadow = true;
        g->add(mesh);
    };

    // Head: faceted low-poly sphere reads sharp at any orbit angle.
    add(SphereGeometry::create(0.14f, 8, 6), 0.f, 1.64f, 0.f, mat);
    add(CylinderGeometry::create(CylinderGeometry::Params{0.05f, 0.055f, 0.1f, 6}), 0.f, 1.49f, 0.f, darker);
    add(BoxGeometry::create(0.42f, 0.52f, 0.2f), 0.f, 1.18f, 0.f, mat);
    add(BoxGeometry::create(0.34f, 0.2f, 0.18f), 0.f, 0.83f, 0.f, darker);
    // Arms hang slightly out of the torso for readable silhouettes.
    for (int side = -1; side <= 1; side += 2) {
        auto arm = Mesh::create(CylinderGeometry::create(CylinderGeometry::Params{0.045f, 0.05f, 0.5f, 6}), mat);
        arm->position.set(float(side) * 0.28f, 1.22f, 0.f);
        arm->rotation.z = float(side) * -0.12f;
        arm->castShadow = true;
        arm->receiveShadow = true;
        g->add(arm);
        auto leg = Mesh::create(CylinderGeometry::create(CylinderGeometry::Params{0.065f, 0.075f, 0.62f, 6}), darker);
        leg->position.set(float(side) * 0.12f, 0.45f, 0.f);
        leg->castShadow = true;
        leg->receiveShadow = true;
        g->add(leg);
    }
    return g;
}

int run(const fs::path& asset, const fs::path& out) {
#ifndef CHRONONMOTION_HAS_GL_CUDA_NVENC
    (void)asset;
    (void)out;
    throw std::runtime_error("SCENE-01 requires the GL PBO -> CUDA -> NVENC build; CPU encoding fallback is disabled");
#else
    fs::create_directories(out);
    GLTFLoader loader;
    auto result = loader.load(asset);
    if (!result || !result->scene) throw std::runtime_error("GLTFLoader failed: " + asset.string());
    if (result->animations.empty()) throw std::runtime_error("GLB contains no animation clips");
    auto clip = result->animations.front();

    // Lead actor: skinned handler of the imported subtree.
    chrononmotion::motion::MotionScene motion(kId, float(FPS));
    chrononmotion::motion::MeshAttachment attachment;
    attachment.assetId = "humanoid_asset";
    auto& lead = motion.addMesh(1, "lead", std::move(attachment));
    lead.castsShadow = true;
    lead.acceptsShadow = true;

    auto& fill = motion.addAmbientLight(100, "fill");
    fill.color = chrononmotion::Color(0xffffff);
    fill.intensity = 0.55f;

    // Warm key, same tuning as the humanoid shadow certification.
    auto& key = motion.addDirectionalLight(101, "key");
    key.color = chrononmotion::Color(0xffedcf);
    key.intensity = 2.2f;
    key.castsShadow = true;
    key.shadowBias = -0.00015f;
    key.shadowNormalBias = 0.025f;
    key.shadowRadius = 3.f;
    key.positionTrack.add(0.f, {2.5f, 8.f, 3.f});
    key.directionTrack.add(0.f, {-2.5f, -8.f, -3.f});

    // Cool rim from behind for separation against the darker set.
    auto& rim = motion.addDirectionalLight(102, "rim");
    rim.color = chrononmotion::Color(0x9ecbff);
    rim.intensity = 0.8f;
    rim.positionTrack.add(0.f, {1.5f, 3.f, -4.f});
    rim.directionTrack.add(0.f, {-1.5f, -3.f, 4.f});

    threepp::motion::MotionSceneBridge bridge(40.f, float(W) / H, 0.05f, 1000.f);
    bridge.bindAsset("humanoid_asset", result->scene);
    bridge.evaluateAndSync(motion, 0);
    bridge.bindLayerAnimation(1, clip);
    auto leadNode = bridge.layerNode(1);
    if (!leadNode) throw std::runtime_error("Motion bridge did not materialize the lead subtree");

    Box3 leadBox;
    leadBox.setFromObject(*leadNode, true);
    Vector3 leadCenter, leadSize;
    leadBox.getCenter(leadCenter);
    leadBox.getSize(leadSize);
    const float leadSpan = std::max({leadSize.x, leadSize.y, leadSize.z, 0.25f});
    // Crate mannequins to the lead height so the crowd reads as cast, not set dressing.
    const float mannequinScale = leadSize.y / 1.78f;
    const float floorY = leadCenter.y - leadSize.y * 0.5f;

    // Crowd: lead front and centre, mannequins in two staggered rows behind.
    std::vector<std::pair<Vector3, float>> spots{
        {{-1.4f, 0.f, -0.9f}, 0.15f},  {{0.f, 0.f, -1.35f}, -0.1f},
        {{1.4f, 0.f, -0.9f}, -0.15f},  {{-0.75f, 0.f, -2.3f}, 0.35f},
        {{0.75f, 0.f, -2.3f}, -0.35f},
    };
    const std::array<unsigned int, 5> palette{0x8d99ae, 0xa68a64, 0x6b7f6f, 0x9c6b62, 0x7d7f85};
    int mannequinMeshes = 0;
    for (size_t i = 0; i < spots.size(); ++i) {
        auto mannequin = makeMannequin(palette[i], spots[i].second);
        mannequin->scale.setScalar(mannequinScale);
        mannequin->position.set(
            spots[i].first.x, floorY, spots[i].first.z);
        bridge.scene()->add(mannequin);
        mannequin->traverse([&](Object3D& o) {
            if (dynamic_cast<Mesh*>(&o)) ++mannequinMeshes;
        });
    }

    // Shadow-receiving floor just under the lead's lowest point.
    auto floorMaterial = MeshStandardMaterial::create();
    floorMaterial->color.setHex(0x82858c);
    floorMaterial->roughness = 0.9f;
    floorMaterial->metalness = 0.f;
    auto floor = Mesh::create(BoxGeometry::create(60.f, 0.02f, 60.f), floorMaterial);
    floor->position.set(0.f, floorY - 0.01f, 0.f);
    floor->castShadow = false;
    floor->receiveShadow = true;
    bridge.scene()->add(floor);

    // Cinematic orbit: sweep ~64 degrees around the crowd while dollying in
    // and easing the height down, then lift off. Target rides the lead chest.
    chrononmotion::motion::CameraRig camera(700, 44.f, float(W) / H, 0.05f, 1000.f);
    const float radius0 = 6.4f, radius1 = 3.9f;
    const float azimuth0 = 0.28f, azimuth1 = 1.4f;   // radians, CCW sweep; keep the lens on the lead side of the crowd
    const float height0 = 2.f, height1 = 1.05f;
    const Vector3 pivot{0.f, floorY + leadSize.y * 0.55f, -0.5f};
    for (int k = 0; k <= 4; ++k) {
        const float t = float(k) * 1.25f;
        const float s = float(k) / 4.f;
        // Smoothstep so the orbit eases in and out instead of constant spin.
        const float e = s * s * (3.f - 2.f * s);
        const float azimuth = azimuth0 + (azimuth1 - azimuth0) * e;
        const float radius = radius0 + (radius1 - radius0) * e;
        const float height = height0 + (height1 - height0) * e;
        camera.positionTrack().add(t, {pivot.x + radius * std::sin(azimuth), height, pivot.z + radius * std::cos(azimuth)});
        camera.targetTrack().add(t, {pivot.x, pivot.y + (k % 2 == 0 ? 0.f : -0.08f), pivot.z});
    }

    // Depth-layered particles burst from the lead actor and drift toward the
    // lens. Each is a separate transparent draw item so renderer depth sorting
    // remains active rather than being replaced by a screen-space composite.
    const Vector3 particleOrigin{0.f, floorY + leadSize.y * 0.72f, -0.45f};
    std::vector<Particle> particles;
    particles.reserve(kParticleCount);
    const auto particleGeometry = SphereGeometry::create(1.f, 8, 6);
    const std::array<unsigned int, 4> particlePalette{0xf2c879, 0x9ecbff, 0xf4f1e8, 0xd5a6ff};
    constexpr float goldenAngle = 2.39996323f;
    for (int i = 0; i < kParticleCount; ++i) {
        const float y = 1.f - 2.f * (float(i) + 0.5f) / float(kParticleCount);
        const float r = std::sqrt(std::max(0.f, 1.f - y * y));
        const float theta = goldenAngle * float(i);
        Vector3 direction{r * std::cos(theta), y, r * std::sin(theta)};
        direction.normalize();
        auto material = MeshBasicMaterial::create();
        material->color.setHex(particlePalette[std::size_t(i) % particlePalette.size()]);
        material->transparent = true;
        material->depthWrite = false;
        material->opacity = 0.f;
        auto mesh = Mesh::create(particleGeometry, material);
        const float size = 0.035f + 0.025f * float(i % 5) / 4.f;
        mesh->scale.setScalar(size);
        mesh->visible = false;
        mesh->renderOrder = 20;
        bridge.scene()->add(mesh);
        particles.push_back({mesh, material, direction, float(i % 24), 1.45f + 0.025f * float(i % 12), 1.4f + 0.035f * float(i % 9)});
    }

    // Camera-facing iris matte: opaque outside, transparent inside, feathered
    // edge. Its RGBA texture is updated deterministically per frame and drawn
    // after the 3D scene; the reveal therefore occludes particles by depth.
    auto revealTexture = makeRevealTexture();
    auto matteMaterial = MeshBasicMaterial::create();
    matteMaterial->map = revealTexture;
    matteMaterial->transparent = true;
    matteMaterial->depthTest = false;
    matteMaterial->depthWrite = false;
    matteMaterial->toneMapped = false;
    auto matteMesh = Mesh::create(PlaneGeometry::create(1.f, 1.f), matteMaterial);
    matteMesh->renderOrder = 1000;
    bridge.scene()->add(matteMesh);
    auto positionMatte = [&](int frame) {
        const auto pose = camera.sample(float(frame) / FPS);
        auto* viewCamera = bridge.camera().get();
        viewCamera->position.set(pose.position.x, pose.position.y, pose.position.z);
        const Vector3 target{pose.target.x, pose.target.y, pose.target.z};
        viewCamera->lookAt(target);
        viewCamera->updateMatrixWorld(true);
        Vector3 forward;
        viewCamera->getWorldDirection(forward);
        constexpr float distance = 0.12f;
        const float height = 2.f * distance * std::tan(viewCamera->fov * 3.14159265f / 360.f);
        matteMesh->position.copy(viewCamera->position).addScaledVector(forward, distance);
        matteMesh->quaternion.copy(viewCamera->quaternion);
        matteMesh->scale.set(height * float(W) / H, height, 1.f);
    };

    std::shared_ptr<DirectionalLight> shadowLight;
    bridge.scene()->traverse([&](Object3D& object) {
        if (auto* directional = dynamic_cast<DirectionalLight*>(&object))
            if (!shadowLight || directional->castShadow)
                shadowLight = std::shared_ptr<DirectionalLight>(directional, [](DirectionalLight*) {});
    });
    if (!shadowLight || !shadowLight->castShadow || !shadowLight->shadow)
        throw std::runtime_error("MotionSceneBridge did not create a shadow-casting directional light");
    shadowLight->shadow->mapSize.set(2048.f, 2048.f);

    Canvas canvas(Canvas::Parameters().size(W, H).antialiasing(4).headless(true));
    GLRenderer renderer(canvas);
    const auto* rendererName = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    const std::string glRenderer = rendererName ? rendererName : "unknown";
    chrononmotion::certification::require_nvidia_renderer(glRenderer);
    renderer.setClearColor(Color(0x151922));
    renderer.toneMapping = ToneMapping::ACESFilmic;
    renderer.toneMappingExposure = 1.f;
    renderer.shadowMap().enabled = true;
    renderer.shadowMap().type = ShadowMap::PFCSoft;

    const fs::path videoPath = out / "T18_documentary_crowd_particle_reveal_1080p30.mp4";
    chrononmotion::certification::require_nvenc_encoder();
    chrononmotion::certification::GlCudaNvencEncoder gpuEncoder(W, H, FPS, videoPath.string());
    const auto begin = std::chrono::steady_clock::now();
    std::size_t matteTransparentAt0 = 0, matteTransparentAt55 = 0;
    int particlesVisibleAt45 = 0, particlesVisibleAt75 = 0;
    for (int f = 0; f < FRAMES; ++f) {
        bridge.evaluateAndSync(motion, f, &camera);
        const auto transparentPixels = updateRevealTexture(revealTexture, f);
        if (f == 0) matteTransparentAt0 = transparentPixels;
        if (f == 55) matteTransparentAt55 = transparentPixels;
        updateParticles(particles, f, particleOrigin);
        if (f == 45) particlesVisibleAt45 = int(std::count_if(particles.begin(), particles.end(), [](const Particle& p) { return p.mesh->visible; }));
        if (f == 75) particlesVisibleAt75 = int(std::count_if(particles.begin(), particles.end(), [](const Particle& p) { return p.mesh->visible; }));
        positionMatte(f);
        renderer.render(*bridge.scene(), *bridge.camera());
        gpuEncoder.encode_current_target(f);
    }
    gpuEncoder.finish();
    if (gpuEncoder.frames_submitted() != FRAMES || gpuEncoder.packets_written() != FRAMES)
        throw std::runtime_error("SCENE-01 GPU encoder did not submit and mux all frames");

    // Shadow probes run after reveal, and separate samples record the matte beat.
    const std::array<int, 3> probes{75, 100, 149};
    std::array<std::vector<unsigned char>, 3> probeOn;
    std::vector<unsigned char> frame77, frame75Pixels;
    std::vector<float> bones0;
    for (int f = 0; f < FRAMES; ++f) {
        bridge.evaluateAndSync(motion, f, &camera);
        updateRevealTexture(revealTexture, f);
        updateParticles(particles, f, particleOrigin);
        positionMatte(f);
        renderer.render(*bridge.scene(), *bridge.camera());
        auto pixels = renderer.readRGBPixels();
        if (pixels.size() != size_t(W) * H * 3) throw std::runtime_error("invalid diagnostic pixel readback");
        if (f == 0) {
            bones0 = boneMatrices(leadNode);
        }
        if (f == 75) frame75Pixels = pixels;
        if (f == 77) frame77 = pixels;
        if (f == 0 || f == 45 || f == 55 || f == 75 || f == 120 || f == 149)
            writePng(out / ("frame-" + std::to_string(f) + ".png"), pixels);
        for (size_t i = 0; i < probes.size(); ++i)
            if (f == probes[i]) {
                probeOn[i] = pixels;
                writePng(out / ("frame-" + std::to_string(f) + ".png"), pixels);
            }
    }
    const auto renderDone = std::chrono::steady_clock::now();

    std::array<ShadowDiff, 3> diffs;
    bool toggleDeterministic = true;
    for (size_t i = 0; i < probes.size(); ++i) {
        bridge.evaluateAndSync(motion, probes[i], &camera);
        updateRevealTexture(revealTexture, probes[i]);
        updateParticles(particles, probes[i], particleOrigin);
        positionMatte(probes[i]);
        renderer.shadowMap().enabled = false;
        renderer.render(*bridge.scene(), *bridge.camera());
        const auto off = renderer.readRGBPixels();
        renderer.shadowMap().enabled = true;
        renderer.render(*bridge.scene(), *bridge.camera());
        const auto restored = renderer.readRGBPixels();
        if (restored != probeOn[i]) toggleDeterministic = false;
        diffs[i] = shadowDiff(probeOn[i], off);
    }

    bridge.evaluateAndSync(motion, 77, &camera);
    updateRevealTexture(revealTexture, 77);
    updateParticles(particles, 77, particleOrigin);
    positionMatte(77);
    renderer.render(*bridge.scene(), *bridge.camera());
    const bool randomOk = renderer.readRGBPixels() == frame77;

    const float boneDelta = vectorDelta(bones0, boneMatrices(leadNode));
    // The reveal is intentionally closed at frame 0; use fully revealed frame
    // 75 for subject coverage while the camera continues orbiting the group.
    int subjectMinY = H, subjectMaxY = -1;
    size_t subjectCoverage = 0;
    const std::array<unsigned char, 3> background{0x15, 0x19, 0x22};
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            const size_t i = (size_t(y) * W + size_t(x)) * 3;
            const int delta = std::abs(int(frame75Pixels[i]) - int(background[0])) +
                              std::abs(int(frame75Pixels[i + 1]) - int(background[1])) +
                              std::abs(int(frame75Pixels[i + 2]) - int(background[2]));
            if (delta <= 6) continue;
            ++subjectCoverage;
            subjectMinY = std::min(subjectMinY, y);
            subjectMaxY = std::max(subjectMaxY, y);
        }
    const float subjectHeightRatio = subjectMaxY >= subjectMinY ? float(subjectMaxY - subjectMinY + 1) / float(H) : 0.f;

    float cameraTravel = 0.f;
    for (int f = 1; f < FRAMES; ++f)
        cameraTravel += (camera.sample(float(f) / FPS).position - camera.sample(float(f - 1) / FPS).position).length();

    const bool shadowsPresent = std::all_of(diffs.begin(), diffs.end(), [](const ShadowDiff& d) { return d.changed > 2000; });
    const bool shadowsOnFloor = std::all_of(diffs.begin(), diffs.end(), [](const ShadowDiff& d) {
        return d.changed > 0 && double(d.lowerHalf) / double(d.changed) >= 0.5;
    });
    const bool matteOpens = matteTransparentAt0 < 2000 && matteTransparentAt55 > 60000;
    const bool particleBurst = particlesVisibleAt45 >= 24 && particlesVisibleAt75 == 0;

    std::cout << kId << " metrics: mannequin_meshes=" << mannequinMeshes << " subject_coverage=" << subjectCoverage
              << " subject_height_ratio=" << subjectHeightRatio << " bone_delta=" << boneDelta
              << " camera_travel=" << cameraTravel << " random=" << randomOk
              << " toggle_deterministic=" << toggleDeterministic << " matte_open=" << matteTransparentAt0 << "->" << matteTransparentAt55
              << " particles_45_75=" << particlesVisibleAt45 << ',' << particlesVisibleAt75 << std::endl;
    for (size_t i = 0; i < probes.size(); ++i)
        std::cout << kId << " frame" << probes[i] << " shadow_changed=" << diffs[i].changed
                  << " on_floor_rows=" << diffs[i].lowerHalf << std::endl;

    if (!randomOk || !toggleDeterministic || mannequinMeshes < 20 || boneDelta < 1e-4f || subjectHeightRatio < 0.20f ||
        !matteOpens || !particleBurst ||
        cameraTravel < 3.f || !shadowsPresent || !shadowsOnFloor) {
        throw std::runtime_error(std::string(kId) + " gate failed: crowd, bones, orbit, matte, particles, shadows or random access");
    }

    std::ofstream scene(out / "scene.json");
    scene << "{\n  \"scene\": \"T18_documentary_crowd_particle_reveal\",\n"
             "  \"lead_asset\": \"" << asset.filename().string() << "\",\n  \"lead_clip\": \"" << clip->name()
          << "\",\n  \"mannequins\": 5,\n  \"mannequin_style\": \"procedural low-poly primitives\",\n"
             "  \"particles\": 48,\n  \"animated_matte\": \"256x256 RGBA camera-facing iris\",\n"
             "  \"camera\": \"orbit + dolly-in 64deg, CameraRig tracks\",\n"
             "  \"shadow_light\": \"key directional (castsShadow=true, mapSize 2048)\",\n"
             "  \"output\": \"1920x1080 30fps 150 frames\",\n"
             "  \"gpu_gate\": \"PARTIAL canary; full T18 certification remains open\"\n}\n";

    struct rusage usage {};
    getrusage(RUSAGE_SELF, &usage);
    const auto* version = reinterpret_cast<const char*>(glGetString(GL_VERSION));
    std::ofstream meta(out / "metadata.json");
    meta << std::fixed << std::setprecision(5) << "{\n  \"test_id\": \"" << kId
         << "\",\n  \"status\": \"PARTIAL\",\n  \"canary_gates_passed\": true,\n"
            "  \"pipeline\": \"GLTFLoader animation + procedural crowd + animated RGBA matte + depth-sorted particles -> MotionSceneBridge (CameraRig orbit) -> threepp GLRenderer -> GL PBO -> CUDA -> NVENC\",\n"
            "  \"lead_asset\": \"" << asset.filename().string()
         << "\",\n  \"lead_asset_license\": \"CC BY 4.0 (Cesium, Khronos glTF-Sample-Assets)\",\n"
            "  \"lead_clip_name\": \"" << clip->name()
         << "\",\n  \"mannequin_mesh_count\": " << mannequinMeshes
         << ",\n  \"bone_matrix_delta_frame_0_to_last\": " << boneDelta
         << ",\n  \"camera_travel_world_units\": " << cameraTravel
         << ",\n  \"subject_coverage_frame_75\": " << subjectCoverage
         << ",\n  \"subject_height_ratio_frame_75\": " << subjectHeightRatio
         << ",\n  \"matte_transparent_texels_frame_0\": " << matteTransparentAt0
         << ",\n  \"matte_transparent_texels_frame_55\": " << matteTransparentAt55
         << ",\n  \"particles_visible_frame_45\": " << particlesVisibleAt45
         << ",\n  \"particles_visible_frame_75\": " << particlesVisibleAt75
         << ",\n  \"shadow_changed_pixels\": [" << diffs[0].changed << ", " << diffs[1].changed << ", " << diffs[2].changed
         << "],\n  \"shadow_changed_pixels_on_floor_rows\": [" << diffs[0].lowerHalf << ", " << diffs[1].lowerHalf
         << ", " << diffs[2].lowerHalf << "],\n  \"shadow_toggle_deterministic\": " << (toggleDeterministic ? "true" : "false")
         << ",\n  \"random_access_frame_77_identical\": " << (randomOk ? "true" : "false")
         << ",\n  \"render_seconds\": " << std::chrono::duration<double>(renderDone - begin).count()
         << ",\n  \"peak_rss_kib\": " << usage.ru_maxrss
         << ",\n  \"gl_renderer\": \"" << glRenderer
         << "\",\n  \"gl_version\": \"" << (version ? version : "unknown")
         << "\",\n  \"video_encoder\": \"h264_nvenc\",\n  \"gpu_device\": \"" << gpuEncoder.device_name()
         << "\",\n  \"frames_submitted\": " << gpuEncoder.frames_submitted()
         << ",\n  \"packets_muxed\": " << gpuEncoder.packets_written()
         << ",\n  \"cpu_framebuffer_readback_in_encode_path\": false"
         << ",\n  \"diagnostic_cpu_readback_passes\": 151"
         << ",\n  \"gpu_resident_zero_copy\": " << (gpuEncoder.zero_copy_input_enabled() ? "true" : "false")
         << ",\n  \"explicit_cuda_d2d_copies_per_encoded_frame\": " << gpuEncoder.explicit_cuda_d2d_copies_per_frame()
         << ",\n  \"gpu_framebuffer_to_pbo_transfer\": true"
         << ",\n  \"zero_copy_scope\": \"mapped GL PBO is the external CUDA AVFrame input; no explicit CUDA D2D copy; framebuffer-to-PBO transfer remains\""
         << ",\n  \"vram_bytes\": null\n}\n";

    renderer.dispose();
    std::cout << kId << " OK crowd=" << mannequinMeshes << " travel=" << cameraTravel
              << " matte=" << matteOpens << " particles=" << particleBurst
              << " random_access=" << randomOk << "\n";
    return 0;
#endif
}
} // namespace

int main(int argc, char** argv) try {
    chrononmotion::certification::prepare_nvidia_egl_headless();
    if (argc < 2) throw std::invalid_argument("usage: t18_documentary_crowd_particle_reveal output_dir [asset.glb]");
    const fs::path out = argv[1];
    const fs::path asset = argc > 2 ? fs::path(argv[2]) : fs::path("render/spike/assets/threepp-certification/CesiumMan.glb");
    return run(asset, out);
} catch (const std::exception& e) {
    std::cerr << "SCENE-01 FAIL: " << e.what() << '\n';
    return 1;
}
