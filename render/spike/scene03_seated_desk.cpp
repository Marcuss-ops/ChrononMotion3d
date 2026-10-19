// Scene 03: a seated character introduces ChrononTemplate at a desk.
// Variants 1-3 keep the same meshes and camera choreography while adding
// cinematic lighting/DoF and then character performance/title compositing.

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>

#include "threepp/cameras/PerspectiveCamera.hpp"
#include "threepp/animation/AnimationMixer.hpp"
#include "threepp/motion/MotionBridge.hpp"
#include "chrononmotion/motion/CameraRig.hpp"
#include "threepp/canvas/Canvas.hpp"
#include "threepp/geometries/BoxGeometry.hpp"
#include "threepp/geometries/CylinderGeometry.hpp"
#include "threepp/geometries/PlaneGeometry.hpp"
#include "threepp/geometries/SphereGeometry.hpp"
#include "threepp/core/BufferGeometry.hpp"
#include "threepp/core/BufferAttribute.hpp"
#include "threepp/lights/AmbientLight.hpp"
#include "threepp/lights/DirectionalLight.hpp"
#include "threepp/lights/HemisphereLight.hpp"
#include "threepp/lights/SpotLight.hpp"
#include "threepp/loaders/GLTFLoader.hpp"
#include "threepp/loaders/ImageLoader.hpp"
#include "threepp/materials/MeshStandardMaterial.hpp"
#include "threepp/materials/MeshPhysicalMaterial.hpp"
#include "threepp/materials/MeshBasicMaterial.hpp"
#include "threepp/math/Box3.hpp"
#include "threepp/math/Quaternion.hpp"
#include "threepp/objects/Group.hpp"
#include "threepp/objects/Mesh.hpp"
#include "threepp/objects/SkinnedMesh.hpp"
#include "threepp/postprocessing/BokehPass.hpp"
#include "threepp/postprocessing/EffectComposer.hpp"
#include "threepp/postprocessing/RenderPass.hpp"
#include "threepp/renderers/GLRenderer.hpp"
#include "threepp/textures/DataTexture.hpp"
#include "threepp/scenes/Scene.hpp"

using namespace threepp;
namespace fs = std::filesystem;

namespace {

std::shared_ptr<MeshStandardMaterial> mat(unsigned int color, float roughness = 0.82f) {
    return MeshStandardMaterial::create(MeshStandardMaterial::Params()
            .color(Color(color)).roughness(roughness).metalness(0.f));
}

std::shared_ptr<Mesh> box(float w, float h, float d, unsigned int color) {
    return Mesh::create(BoxGeometry::create(w, h, d), mat(color));
}

void addCylinder(Scene& scene, float radius, float height, const Vector3& at,
                 unsigned int color, unsigned int segments = 16) {
    auto mesh = Mesh::create(CylinderGeometry::create(radius, radius, height, segments), mat(color));
    mesh->position.copy(at);
    scene.add(mesh);
}

std::shared_ptr<MeshStandardMaterial> material(unsigned int color, float roughness, float metalness = 0.f) {
    return MeshStandardMaterial::create(MeshStandardMaterial::Params()
            .color(Color(color)).roughness(roughness).metalness(metalness));
}

std::shared_ptr<DataTexture> makeClothTexture() {
    constexpr int size = 128;
    std::vector<unsigned char> rgb(size_t(size) * size * 3);
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const int weave = ((x % 4 == 0) != (y % 4 == 0)) ? 8 : -4;
            const int grain = ((x * 37 + y * 73 + x * y * 11) % 5) - 2;
            const int v = weave + grain;
            const size_t i = (size_t(y) * size + x) * 3;
            rgb[i] = static_cast<unsigned char>(std::clamp(194 + v, 0, 255));
            rgb[i + 1] = static_cast<unsigned char>(std::clamp(68 + v / 2, 0, 255));
            rgb[i + 2] = static_cast<unsigned char>(std::clamp(74 + v / 2, 0, 255));
        }
    }
    auto texture = DataTexture::create(std::move(rgb), size, size);
    texture->format = Format::RGB;
    texture->colorSpace = ColorSpace::sRGB;
    texture->wrapS = texture->wrapT = TextureWrapping::Repeat;
    // Keep the weave at a screen-visible scale on the torso rather than
    // aliasing it into a flat colour across a few dozen repeats.
    texture->repeat.set(2.f, 2.f);
    texture->magFilter = Filter::Linear;
    texture->minFilter = Filter::LinearMipmapLinear;
    texture->generateMipmaps = true;
    texture->needsUpdate();
    return texture;
}

struct WrittenTitle {
    std::shared_ptr<DataTexture> texture;
    std::vector<unsigned char> alphaMask;
    int width = 512;
    int height = 360;
};

WrittenTitle addWrittenTitle(Scene& scene, float x, float y, float z, const fs::path& maskPath) {
    ImageLoader loader;
    auto mask = loader.load(maskPath, 4, true);
    if (!mask) throw std::runtime_error("failed to load title mask: " + maskPath.string());
    WrittenTitle title;
    title.width = int(mask->width());
    title.height = int(mask->height());
    title.alphaMask.resize(size_t(title.width) * title.height);
    const auto& source = mask->data<unsigned char>();
    for (size_t i = 0; i < title.alphaMask.size(); ++i) title.alphaMask[i] = source[i * 4 + 3];

    title.texture = DataTexture::create(4, title.width, title.height);
    title.texture->format = Format::RGBA;
    title.texture->colorSpace = ColorSpace::sRGB;
    title.texture->magFilter = Filter::Linear;
    title.texture->minFilter = Filter::Linear;
    title.texture->generateMipmaps = false;
    auto ink = MeshBasicMaterial::create(MeshBasicMaterial::Params()
            .map(title.texture).color(Color(0x18212a)));
    ink->transparent = true;
    ink->depthWrite = false;
    ink->toneMapped = false;
    ink->side = Side::Double;
    auto lettering = Mesh::create(PlaneGeometry::create(0.44f, 0.31f), ink);
    lettering->rotation.x = -1.57079632679f;
    lettering->position.set(x, y, z);
    lettering->castShadow = lettering->receiveShadow = false;
    scene.add(lettering);
    return title;
}

void revealWrittenTitle(WrittenTitle& title, float progress) {
    progress = std::clamp(progress, 0.f, 1.f);
    auto& rgba = title.texture->image().data<unsigned char>();
    const int cursorX = int(progress * float(title.width));
    for (int y = 0; y < title.height; ++y) {
        for (int x = 0; x < title.width; ++x) {
            const size_t pixel = size_t(y) * title.width + x;
            const size_t offset = pixel * 4;
            rgba[offset] = 24;
            rgba[offset + 1] = 31;
            rgba[offset + 2] = 39;
            rgba[offset + 3] = x < cursorX ? title.alphaMask[pixel] : 0;
        }
    }
    title.texture->needsUpdate();
}

std::shared_ptr<MeshStandardMaterial> makeWoodMaterial(Scene& scene) {
    std::shared_ptr<MeshStandardMaterial> source;
    scene.traverse([&](Object3D& node) {
        auto* mesh = dynamic_cast<Mesh*>(&node);
        if (!mesh || source) return;
        for (const auto& candidate : mesh->materials()) {
            if (candidate && candidate->name == "plywood") {
                source = std::dynamic_pointer_cast<MeshStandardMaterial>(candidate);
                if (source) break;
            }
        }
    });
    auto wood = material(0xb49a7d, 0.68f);
    if (source && source->map) {
        wood->map = source->map->clone();
        wood->map->wrapS = wood->map->wrapT = TextureWrapping::Repeat;
        wood->map->offset.set(0.f, 0.f);
        wood->map->repeat.set(1.f, 1.f);
        wood->map->needsUpdate();
    }
    return wood;
}

void applyEnvironmentClay(Scene& scene) {
    auto clay = material(0x80776e, 0.96f);
    scene.traverse([&](Object3D& node) {
        auto* mesh = dynamic_cast<Mesh*>(&node);
        if (!mesh) return;
        // Keep the GLB's material slot count so geometry groups remain valid,
        // while removing its flat source colors and maps for one matte room look.
        std::vector<std::shared_ptr<Material>> slots(mesh->materials().size(), clay);
        if (!slots.empty()) mesh->setMaterials(slots);
        else mesh->setMaterial(clay);
    });
}

struct ProfileRing { float y, rx, rz; };

std::shared_ptr<BufferGeometry> makeProfile(const std::vector<ProfileRing>& rings, int sides = 12) {
    std::vector<float> positions;
    std::vector<float> uvs;
    std::vector<unsigned int> indices;
    positions.reserve(rings.size() * size_t(sides) * 3);
    uvs.reserve(rings.size() * size_t(sides) * 2);
    for (const auto& ring : rings) {
        for (int i = 0; i < sides; ++i) {
            const float a = 2.f * 3.14159265f * float(i) / float(sides);
            positions.insert(positions.end(), {ring.rx * std::cos(a), ring.y, ring.rz * std::sin(a)});
            uvs.insert(uvs.end(), {float(i) / float(sides), ring.y * 2.f});
        }
    }
    for (int r = 0; r + 1 < int(rings.size()); ++r) {
        for (int i = 0; i < sides; ++i) {
            const unsigned int a = unsigned(r * sides + i);
            const unsigned int b = unsigned(r * sides + (i + 1) % sides);
            const unsigned int c = unsigned((r + 1) * sides + i);
            const unsigned int d = unsigned((r + 1) * sides + (i + 1) % sides);
            indices.insert(indices.end(), {a, c, b, b, c, d});
        }
    }
    auto geometry = BufferGeometry::create();
    geometry->setAttribute("position", FloatBufferAttribute::create(std::move(positions), 3));
    geometry->setAttribute("uv", FloatBufferAttribute::create(std::move(uvs), 2));
    geometry->setIndex(std::move(indices));
    geometry->computeVertexNormals();
    return geometry;
}

void addProfile(Scene& scene, const std::vector<ProfileRing>& rings, const Vector3& at,
                const std::shared_ptr<MeshStandardMaterial>& surface, int sides = 12) {
    auto mesh = Mesh::create(makeProfile(rings, sides), surface);
    mesh->position.copy(at);
    mesh->castShadow = mesh->receiveShadow = true;
    scene.add(mesh);
}

void addTaperedLimb(Scene& scene, const Vector3& a, const Vector3& b, float rA, float rB,
                    const std::shared_ptr<MeshStandardMaterial>& surface, int sides = 10) {
    Vector3 axis = b - a;
    const float length = axis.length();
    axis.normalize();
    auto mesh = Mesh::create(CylinderGeometry::create(rB, rA, length, sides, 1), surface);
    mesh->position.copy(a).add(b).multiplyScalar(0.5f);
    mesh->quaternion.setFromUnitVectors(Vector3(0, 1, 0), axis);
    mesh->castShadow = mesh->receiveShadow = true;
    scene.add(mesh);
}

void styleCharacter(const std::shared_ptr<Object3D>& root) {
    // The source pack assigns yellow paint to the body and purple to its
    // joint caps. A single porcelain material makes the whole rig read as
    // one matte porcelain figure, with broad highlights instead of plastic glare.
    auto porcelain = MeshPhysicalMaterial::create(MeshPhysicalMaterial::Params()
            .color(Color(0xf4f6fa))
            .roughness(0.38f)
            .metalness(0.02f)
            .clearcoat(0.24f)
            .clearcoatRoughness(0.24f));
    root->traverse([&](Object3D& node) {
        auto* mesh = dynamic_cast<Mesh*>(&node);
        if (!mesh) return;
        mesh->setMaterial(porcelain);
    });
}

struct CharacterAnimation {
    std::unique_ptr<AnimationMixer> mixer;
    AnimationAction* idle = nullptr;
    AnimationAction* talk = nullptr;
    float idleDuration = 0.f;
    float talkDuration = 0.f;
};

CharacterAnimation addSeatedCharacter(Scene& scene, const fs::path& assetPath,
                                     const std::string& clipName, float phase) {
    GLTFLoader loader;
    auto character = loader.load(assetPath);
    if (!character || !character->scene) throw std::runtime_error("failed to load character: " + assetPath.string());
    auto sitting = AnimationClip::findByName(character->animations, clipName);
    auto talking = AnimationClip::findByName(character->animations, "Sitting_Talking_Loop");
    if (!sitting) throw std::runtime_error("character GLB is missing animation " + clipName);
    if (!talking) throw std::runtime_error("character GLB is missing animation Sitting_Talking_Loop");
    auto mixer = std::make_unique<AnimationMixer>(*character->scene);
    auto idleAction = mixer->clipAction(sitting);
    auto talkAction = mixer->clipAction(talking);
    idleAction->play().setEffectiveWeight(1.f).setLocalTime(sitting->getDuration() * std::clamp(phase, 0.f, 1.f));
    talkAction->play().setEffectiveWeight(0.f).setLocalTime(0.f);
    mixer->update(0.f);
    character->scene->updateMatrixWorld(true);
    styleCharacter(character->scene);
    auto rig = Group::create();
    rig->add(character->scene);
    // The GLB armature already contains its source-axis conversion. Rotating
    // the imported subtree again lays the skinned character on its side.
    rig->rotation.x = 0.f;
    rig->updateMatrixWorld(true);
    Box3 bounds;
    bounds.setFromObject(*rig, true);
    Vector3 center, size;
    bounds.getCenter(center);
    bounds.getSize(size);
    std::cerr << "character bounds after axis conversion=" << size.x << "," << size.y << "," << size.z
              << " center=" << center.x << "," << center.y << "," << center.z << "\n";
    constexpr float desiredHeight = 2.1f;
    const float scale = desiredHeight / std::max({size.x, size.y, size.z, 0.001f});
    rig->scale.set(scale, scale, scale);
    rig->position.set(-0.5f - center.x * scale, 0.02f - bounds.min().y * scale,
                      -0.07f - center.z * scale);
    scene.add(rig);
    rig->updateMatrixWorld(true);
    scene.updateMatrixWorld(true);
    rig->traverse([](Object3D& node) {
        if (auto* mesh = dynamic_cast<Mesh*>(&node)) mesh->castShadow = mesh->receiveShadow = true;
    });
    std::cout << "character animations=" << character->animations.size() << "\n";
    return {std::move(mixer), idleAction, talkAction,
            sitting->getDuration(), talking->getDuration()};
}

int renderScene(const fs::path& roomPath, const fs::path& output, const fs::path& characterPath,
                const std::string& clipName, float phase, int width, int height,
                float durationSeconds, int fps, int variant) {
    GLTFLoader loader;
    auto roomResult = loader.load(roomPath);
    if (!roomResult || !roomResult->scene) throw std::runtime_error("failed to load room: " + roomPath.string());
    auto scene = Scene::create();
    scene->add(roomResult->scene);
    // threepp's OpenGL path provides camera-depth fog (not true participating
    // media); keep its density low so the desk and the white figure stay crisp.
    scene->fog = FogExp2(Color(0x403a35), 0.025f);
    auto deskWood = makeWoodMaterial(*scene);
    applyEnvironmentClay(*scene);
    Box3 roomBounds;
    roomBounds.setFromObject(*roomResult->scene, true);
    Vector3 roomCenter, roomSize;
    roomBounds.getCenter(roomCenter);
    roomBounds.getSize(roomSize);
    auto characterAnimation = addSeatedCharacter(*scene, characterPath, clipName, phase);

    // Desk in front of the character.
    constexpr float deskX = -0.5f;
    constexpr float deskZ = 0.54f;
    auto deskTop = box(1.75f, 0.07f, 0.76f, 0xb49a7d);
    deskTop->setMaterial(deskWood);
    deskTop->position.set(deskX, 0.71f, deskZ);
    deskTop->castShadow = deskTop->receiveShadow = true;
    scene->add(deskTop);
    for (float x : {-1.28f, 0.28f}) {
        for (float z : {0.24f, 0.84f}) {
            auto leg = box(0.075f, 0.67f, 0.075f, 0x4a3528);
            leg->setMaterial(deskWood);
            leg->position.set(x, 0.335f, z);
            leg->castShadow = true;
            scene->add(leg);
        }
    }

    // Simple office chair behind the desk: seat, back, central stem and base.
    const float chairX = -0.5f;
    const float chairZ = -0.25f;
    auto chairClay = material(0x34383d, 0.96f);
    auto seat = box(0.78f, 0.12f, 0.68f, 0x303944);
    seat->setMaterial(chairClay);
    seat->position.set(chairX, 0.46f, chairZ);
    seat->castShadow = seat->receiveShadow = true;
    scene->add(seat);
    auto back = box(0.82f, 0.88f, 0.11f, 0x354250);
    back->setMaterial(chairClay);
    back->position.set(chairX, 0.98f, chairZ - 0.28f);
    back->castShadow = back->receiveShadow = true;
    scene->add(back);
    auto chairStitch = chairClay;
    for (float y : {.79f, 1.13f}) {
        auto stitch = Mesh::create(BoxGeometry::create(.58f,.008f,.006f), chairStitch);
        stitch->position.set(chairX,.98f + (y-.98f),chairZ-.218f);
        scene->add(stitch);
    }
    auto chairStem = Mesh::create(CylinderGeometry::create(0.045f, 0.045f, 0.43f, 20), chairClay);
    chairStem->position.set(chairX, 0.22f, chairZ);
    scene->add(chairStem);
    auto base = box(0.72f, 0.045f, 0.12f, 0x505760);
    base->setMaterial(chairClay);
    base->position.set(chairX, 0.025f, chairZ);
    scene->add(base);
    auto baseDepth = box(0.12f, 0.045f, 0.72f, 0x505760);
    baseDepth->setMaterial(chairClay);
    baseDepth->position.set(chairX, 0.025f, chairZ);
    scene->add(baseDepth);
    // One paper-thin sheet with subtly rounded corners; the earlier stacked
    // boxes made a thick, contrasting edge at the close camera angle.
    // A single 1 mm opaque sheet keeps the silhouette stable in motion while
    // staying thin enough to read as paper at the final close camera angle.
    auto paper = box(0.44f, 0.001f, 0.31f, 0xf5f3ed);
    paper->setMaterial(material(0xf5f3ed, 0.98f));
    paper->position.set(deskX, 0.7456f, 0.59f);
    paper->castShadow = true;
    paper->receiveShadow = true;
    scene->add(paper);
    auto writtenTitle = addWrittenTitle(*scene, deskX, 0.74635f, 0.59f,
                                        characterPath.parent_path() / "Welcome_Text_Mask.png");

    scene->add(HemisphereLight::create(Color(0xc9d3df), Color(0x302722), variant == 1 ? 0.28f : 0.18f));
    scene->add(AmbientLight::create(Color(0xfff0dd), variant == 1 ? 0.20f : 0.10f));
    auto key = SpotLight::create(Color(0xffe2c4), variant == 1 ? 0.f : 27.f, 7.5f, 0.62f, 0.8f, 2.f);
    key->position.set(-1.85f, 3.3f, 3.1f);
    key->castShadow = false;
    auto keyTarget = Group::create();
    keyTarget->position.set(deskX, 0.76f, 0.56f);
    key->setTarget(*keyTarget);
    scene->add(keyTarget);
    scene->add(key);
    auto shadowKey = DirectionalLight::create(Color(0xffe3c8), variant == 1 ? 0.f : 1.45f);
    shadowKey->position.set(-2.6f, 4.2f, 2.8f);
    shadowKey->castShadow = variant != 1;
    shadowKey->shadow->mapSize.set(2048.f, 2048.f);
    shadowKey->shadow->bias = -0.00015f;
    shadowKey->shadow->normalBias = 0.018f;
    auto shadowTarget = Group::create();
    shadowTarget->position.set(deskX, 0.76f, 0.56f);
    shadowKey->setTarget(*shadowTarget);
    scene->add(shadowTarget);
    scene->add(shadowKey);
    auto rim = DirectionalLight::create(Color(0xc6d8f3), variant == 1 ? 0.f : 0.62f);
    rim->position.set(1.7f, 2.2f, -1.4f);
    scene->add(rim);
    auto fill = DirectionalLight::create(Color(0xf0f3fb), variant == 1 ? 0.10f : 0.20f);
    fill->position.set(0.8f, 2.4f, 3.4f);
    auto fillTarget = Group::create();
    fillTarget->position.set(-0.5f, 0.9f, 0.15f);
    fill->setTarget(*fillTarget);
    scene->add(fillTarget);
    scene->add(fill);

    Canvas canvas(Canvas::Parameters().size(width, height).headless(true));
    GLRenderer renderer(canvas);
    renderer.setClearColor(Color(0x343b45));
    renderer.toneMapping = ToneMapping::ACESFilmic;
    renderer.toneMappingExposure = 1.0f;
    renderer.shadowMap().enabled = variant != 1;
    renderer.shadowMap().type = ShadowMap::PFCSoft;
    scene->traverse([](Object3D& node) {
        if (auto* mesh = dynamic_cast<Mesh*>(&node)) {
            mesh->castShadow = true;
            mesh->receiveShadow = true;
        }
    });
    auto camera = PerspectiveCamera::create(41.f, float(width) / height, 0.1f, 100.f);
    chrononmotion::motion::CameraRig cameraRig(700, 41.f, float(width) / height, 0.1f, 100.f);
    using chrononmotion::motion::Easing;
    cameraRig.positionTrack().add(0.f, {deskX + 0.44f, 1.32f, 2.78f}, Easing::easeInOut());
    cameraRig.positionTrack().add(2.8f, {deskX - 0.24f, 1.30f, 2.62f}, Easing::easeInOut());
    cameraRig.positionTrack().add(6.3f, {deskX - 0.13f, 1.24f, 2.02f}, Easing::easeInOut());
    cameraRig.positionTrack().add(8.4f, {deskX + 0.03f, 1.29f, 1.64f}, Easing::easeInOut());
    cameraRig.positionTrack().add(10.f, {deskX + 0.03f, 1.29f, 1.64f});
    cameraRig.targetTrack().add(0.f, {deskX, 0.98f, 0.22f}, Easing::easeInOut());
    cameraRig.targetTrack().add(2.8f, {deskX, 0.91f, 0.24f}, Easing::easeInOut());
    cameraRig.targetTrack().add(6.3f, {deskX, 0.83f, 0.51f}, Easing::easeInOut());
    cameraRig.targetTrack().add(8.4f, {deskX, 0.78f, 0.59f}, Easing::easeInOut());
    cameraRig.targetTrack().add(10.f, {deskX, 0.78f, 0.59f});
    cameraRig.focusDistanceTrack().add(0.f, 2.82f, Easing::easeInOut());
    cameraRig.focusDistanceTrack().add(2.8f, 2.55f, Easing::easeInOut());
    cameraRig.focusDistanceTrack().add(6.3f, 1.56f, Easing::easeInOut());
    cameraRig.focusDistanceTrack().add(8.4f, 1.18f, Easing::easeInOut());
    cameraRig.focusDistanceTrack().add(10.f, 1.18f);
    threepp::motion::applyCameraPose(cameraRig.sample(0.f), *camera, nullptr, 0.1f, 100.f);
    scene->updateMatrixWorld(true);
    EffectComposer composer(renderer);
    composer.renderToScreen = true;
    composer.setSize(width, height);
    composer.addPass(std::make_shared<RenderPass>(*scene, *camera));
    auto bokeh = std::make_shared<BokehPass>(*scene, *camera, 2.55f,
                                             variant == 1 ? 0.f : 0.009f,
                                             variant == 1 ? 0.f : 0.008f);
    composer.addPass(bokeh);
    const int frameCount = durationSeconds > 0.f ? int(std::lround(durationSeconds * fps)) : 1;
    if (durationSeconds > 0.f) fs::create_directories(output);
    for (int frame = 0; frame < frameCount; ++frame) {
        const float time = durationSeconds > 0.f ? float(frame) / float(fps) : 0.f;
        if (durationSeconds > 0.f) {
            threepp::motion::applyCameraPose(cameraRig.sample(time), *camera, nullptr, 0.1f, 100.f);
            const auto pose = cameraRig.sample(time);
            bokeh->focus = pose.focusDistance;
            if (variant == 3) {
                // Deterministic pose crossfade: hold the seated idle in the
                // opening, then let the supplied talking gesture animate the
                // head/arms as the camera approaches the page.
                const float gesture = std::clamp((time - 2.2f) / 1.0f, 0.f, 1.f);
                const float blend = gesture * gesture * (3.f - 2.f * gesture);
                characterAnimation.idle->setEffectiveWeight(1.f - blend)
                        .setLocalTime(std::fmod(time * 0.45f, characterAnimation.idleDuration));
                characterAnimation.talk->setEffectiveWeight(blend)
                        .setLocalTime(std::fmod(std::max(0.f, time - 2.2f) * 0.45f,
                                                characterAnimation.talkDuration));
                characterAnimation.mixer->update(0.f);
                revealWrittenTitle(writtenTitle, (time - 5.4f) / 3.6f);
            } else {
                characterAnimation.idle->setEffectiveWeight(1.f)
                        .setLocalTime(std::fmod(time * 0.45f, characterAnimation.idleDuration));
                characterAnimation.talk->setEffectiveWeight(0.f)
                        .setLocalTime(std::fmod(time * 0.45f, characterAnimation.talkDuration));
                characterAnimation.mixer->update(0.f);
                revealWrittenTitle(writtenTitle, 0.f);
            }
        }
        composer.render();
        auto pixels = renderer.readRGBPixels();
        if (pixels.size() != size_t(width * height * 3)) throw std::runtime_error("unexpected RGB readback size");
        fs::path framePath = output;
        if (durationSeconds > 0.f) {
            char name[32];
            std::snprintf(name, sizeof(name), "frame_%05d.ppm", frame);
            framePath /= name;
        }
        std::ofstream ppm(framePath, std::ios::binary);
        if (!ppm) throw std::runtime_error("cannot write render frame: " + framePath.string());
        ppm << "P6\n" << width << " " << height << "\n255\n";
        for (int y = height - 1; y >= 0; --y) ppm.write(reinterpret_cast<const char*>(&pixels[y * width * 3]), width * 3);
        if (frame % std::max(1, fps) == 0 || frame + 1 == frameCount)
            std::cout << "rendered frame " << frame + 1 << "/" << frameCount << "\n";
    }
    std::cout << "scene03 render written: " << output << "\n"
              << "room bounds=" << roomSize.x << "," << roomSize.y << "," << roomSize.z
              << " center=" << roomCenter.x << "," << roomCenter.y << "," << roomCenter.z << "\n"
              << "subject=Quaternius-UAL1-rigged-character\n";
    renderer.dispose();
    return 0;
}

}// namespace

int main(int argc, char** argv) {
    try {
        if ((argc == 10 || argc == 11) && std::string(argv[1]) == "--video") {
            return renderScene(argv[2], argv[3], argv[4], argv[5], 0.f,
                               std::stoi(argv[8]), std::stoi(argv[9]),
                               std::stof(argv[6]), std::stoi(argv[7]),
                               argc == 11 ? std::stoi(argv[10]) : 3);
        }
        if (argc < 4 || argc > 6) {
            std::cerr << "usage: chrononmotion_render_scene_seated_desk <room.glb> <preview.ppm> <character.glb> [animation] [phase 0..1]\n"
                         "   or: chrononmotion_render_scene_seated_desk --video <room.glb> <frames-dir> <character.glb> <animation> <seconds> <fps> <width> <height>\n";
            return 2;
        }
        return renderScene(argv[1], argv[2], argv[3], argc > 4 ? argv[4] : "Sitting_Idle_Loop",
                           argc > 5 ? std::stof(argv[5]) : 0.5f, 854, 480, 0.f, 30, 3);
    } catch (const std::exception& e) {
        std::cerr << "scene03: " << e.what() << "\n";
        return 1;
    }
}
