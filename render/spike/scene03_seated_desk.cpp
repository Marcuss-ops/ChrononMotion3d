// SCENE-03 composition proof: a posed seated character behind a desk.
// This target renders one base-composition frame first; cinematic post effects
// and the 180-degree camera sequence are added only after composition approval.

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>

#include "threepp/cameras/PerspectiveCamera.hpp"
#include "threepp/canvas/Canvas.hpp"
#include "threepp/geometries/BoxGeometry.hpp"
#include "threepp/geometries/CylinderGeometry.hpp"
#include "threepp/geometries/SphereGeometry.hpp"
#include "threepp/core/BufferGeometry.hpp"
#include "threepp/core/BufferAttribute.hpp"
#include "threepp/lights/AmbientLight.hpp"
#include "threepp/lights/DirectionalLight.hpp"
#include "threepp/lights/HemisphereLight.hpp"
#include "threepp/lights/SpotLight.hpp"
#include "threepp/loaders/GLTFLoader.hpp"
#include "threepp/materials/MeshStandardMaterial.hpp"
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

void addSeatedCharacter(Scene& scene, const fs::path& assetPath) {
    GLTFLoader loader;
    auto character = loader.load(assetPath);
    if (!character || !character->scene) throw std::runtime_error("failed to load character: " + assetPath.string());
    character->scene->updateMatrixWorld(true);
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
    constexpr float desiredHeight = 1.65f;
    const float scale = desiredHeight / std::max({size.x, size.y, size.z, 0.001f});
    rig->scale.set(scale, scale, scale);
    rig->position.set(-0.5f - center.x * scale, 0.02f - bounds.min().y * scale,
                      -0.25f - center.z * scale);
    scene.add(rig);
    rig->updateMatrixWorld(true);
    scene.updateMatrixWorld(true);
    rig->traverse([](Object3D& node) {
        if (auto* mesh = dynamic_cast<Mesh*>(&node)) mesh->castShadow = mesh->receiveShadow = true;
    });
    std::cout << "character animations=" << character->animations.size() << "\n";
    return;

    auto porcelain = material(0xf4f4f2, 0.12f);
    auto shirt = material(0xb43b45, 0.86f);
    auto shirtSeam = material(0x8e2832, 0.92f);
    auto cloth = makeClothTexture();
    shirt->map = cloth;
    shirt->color.setHex(0xffffff);
    shirt->bumpMap = cloth;
    shirt->bumpScale = 0.0035f;
    auto trousers = material(0x26313b, 0.86f);
    auto shoes = material(0x151a20, 0.48f);
    auto sole = material(0x080b0f, 0.72f);
    constexpr float x = -0.5f;
    constexpr float z = -0.08f;

    // A tapered torso built as one continuous profile, with narrow shoulders and waist.
    addProfile(scene, {{.07f,.19f,.12f},{.13f,.20f,.125f},{.25f,.175f,.105f},
                       {.40f,.225f,.13f},{.50f,.205f,.12f},{.56f,.16f,.105f}},
               Vector3(x,.66f,z), shirt, 14);
    // Narrow crew-neck binding around the neck opening.
    addProfile(scene, {{0.f,.080f,.073f},{.018f,.086f,.079f},{.034f,.080f,.073f}},
               Vector3(x,1.205f,z), shirtSeam, 14);
    // Featureless, softly rounded head and short neck.
    addProfile(scene, {{0.f,.075f,.075f},{.045f,.105f,.10f},{.15f,.115f,.105f},
                       {.29f,.09f,.09f},{.34f,.035f,.045f}}, Vector3(x,1.25f,z-.005f), porcelain, 16);
    addProfile(scene, {{0.f,.072f,.07f},{.10f,.072f,.07f}}, Vector3(x,1.19f,z), porcelain, 12);

    // Bent seated legs: thighs angle forward; shins drop below the desk.
    for (float side : {-1.f, 1.f}) {
        const Vector3 hip(x + side*.105f, .66f, z+.005f);
        const Vector3 knee(x + side*.145f, .49f, z+.37f);
        const Vector3 ankle(x + side*.15f, .15f, z+.34f);
        addTaperedLimb(scene, hip, knee, .125f, .095f, trousers, 10);
        addTaperedLimb(scene, knee, ankle, .078f, .055f, trousers, 10);
        auto trouserSeam = material(0x39434d, 0.88f);
        addTaperedLimb(scene, Vector3(ankle.x,.20f,z+.397f), Vector3(ankle.x,.39f,z+.397f),
                       .0035f,.0035f,trouserSeam,5);
        // Long, low shoe with a distinct sole, pointing toward the desk/camera.
        auto soleMesh = Mesh::create(SphereGeometry::create(1.f, 10, 6), sole);
        soleMesh->scale.set(.066f,.018f,.16f);
        soleMesh->position.set(ankle.x, .058f, z+.49f);
        soleMesh->castShadow = true;
        scene.add(soleMesh);
        auto shoeMesh = Mesh::create(SphereGeometry::create(1.f, 10, 6), shoes);
        shoeMesh->scale.set(.062f,.052f,.155f);
        shoeMesh->position.set(ankle.x, .112f, z+.49f);
        shoeMesh->castShadow = shoeMesh->receiveShadow = true;
        scene.add(shoeMesh);
        auto lace = material(0x59616a, 0.72f);
        for (int i = 0; i < 3; ++i) {
            auto bar = Mesh::create(BoxGeometry::create(.052f,.006f,.009f), lace);
            bar->position.set(ankle.x,.164f,z+.425f+float(i)*.035f);
            scene.add(bar);
        }
    }

    // Slim arms bend from the shoulder toward the tabletop; palms and fingers rest on it.
    for (float side : {-1.f, 1.f}) {
        const Vector3 shoulder(x + side*.14f, 1.16f, z);
        const Vector3 elbow(x + side*.31f, .93f, z+.13f);
        const Vector3 wrist(x + side*.32f, .765f, z+.48f);
        addTaperedLimb(scene, shoulder, elbow, .067f, .052f, shirt, 10);
        const Vector3 cuffA(x + side*.294f, .952f, z+.117f);
        const Vector3 cuffB(x + side*.309f, .931f, z+.132f);
        addTaperedLimb(scene, cuffA, cuffB, .052f,.050f,shirtSeam,10);
        addTaperedLimb(scene, elbow, wrist, .049f, .037f, porcelain, 10);
        auto elbowJoint = Mesh::create(SphereGeometry::create(.045f, 10, 6), porcelain);
        elbowJoint->position.copy(elbow);
        elbowJoint->castShadow = elbowJoint->receiveShadow = true;
        scene.add(elbowJoint);
        auto palm = Mesh::create(SphereGeometry::create(1.f, 10, 6), porcelain);
        palm->scale.set(.052f,.019f,.066f);
        palm->position.set(wrist.x, .755f, wrist.z+.06f);
        palm->castShadow = palm->receiveShadow = true;
        scene.add(palm);
        for (int finger = 0; finger < 4; ++finger) {
            const float spread = (float(finger)-1.5f)*.021f;
            addTaperedLimb(scene,
                    Vector3(wrist.x+spread, .748f, wrist.z+.095f),
                    Vector3(wrist.x+spread*1.35f, .744f, wrist.z+.17f),
                    .010f, .007f, porcelain, 6);
        }
        addTaperedLimb(scene, Vector3(wrist.x+side*.045f,.75f,wrist.z+.045f),
                Vector3(wrist.x+side*.075f,.747f,wrist.z+.105f), .012f,.008f, porcelain,6);
    }
}

int renderPreview(const fs::path& roomPath, const fs::path& output, const fs::path& characterPath) {
    GLTFLoader loader;
    auto roomResult = loader.load(roomPath);
    if (!roomResult || !roomResult->scene) throw std::runtime_error("failed to load room: " + roomPath.string());
    auto scene = Scene::create();
    scene->add(roomResult->scene);
    Box3 roomBounds;
    roomBounds.setFromObject(*roomResult->scene, true);
    Vector3 roomCenter, roomSize;
    roomBounds.getCenter(roomCenter);
    roomBounds.getSize(roomSize);
    addSeatedCharacter(*scene, characterPath);

    // Desk in front of the character.
    constexpr float deskX = -0.5f;
    constexpr float deskZ = 0.54f;
    auto deskTop = box(1.75f, 0.07f, 0.76f, 0xb49a7d);
    deskTop->setMaterial(makeWoodMaterial(*scene));
    deskTop->position.set(deskX, 0.71f, deskZ);
    deskTop->castShadow = deskTop->receiveShadow = true;
    scene->add(deskTop);
    for (float x : {-1.28f, 0.28f}) {
        for (float z : {0.24f, 0.84f}) {
            auto leg = box(0.075f, 0.67f, 0.075f, 0x4a3528);
            leg->setMaterial(makeWoodMaterial(*scene));
            leg->position.set(x, 0.335f, z);
            leg->castShadow = true;
            scene->add(leg);
        }
    }

    // Simple office chair behind the desk: seat, back, central stem and base.
    const float chairX = -0.5f;
    const float chairZ = -0.25f;
    auto seat = box(0.78f, 0.12f, 0.68f, 0x303944);
    seat->position.set(chairX, 0.46f, chairZ);
    seat->castShadow = seat->receiveShadow = true;
    scene->add(seat);
    auto back = box(0.82f, 0.88f, 0.11f, 0x354250);
    back->position.set(chairX, 0.98f, chairZ - 0.28f);
    back->castShadow = back->receiveShadow = true;
    scene->add(back);
    auto chairStitch = material(0x536171, 0.9f);
    for (float y : {.79f, 1.13f}) {
        auto stitch = Mesh::create(BoxGeometry::create(.58f,.008f,.006f), chairStitch);
        stitch->position.set(chairX,.98f + (y-.98f),chairZ-.218f);
        scene->add(stitch);
    }
    addCylinder(*scene, 0.045f, 0.43f, Vector3(chairX, 0.22f, chairZ), 0x505760, 20);
    auto base = box(0.72f, 0.045f, 0.12f, 0x505760);
    base->position.set(chairX, 0.025f, chairZ);
    scene->add(base);
    auto baseDepth = box(0.12f, 0.045f, 0.72f, 0x505760);
    baseDepth->position.set(chairX, 0.025f, chairZ);
    scene->add(baseDepth);
    scene->add(HemisphereLight::create(Color(0xe1e8f2), Color(0x463a30), 0.85f));
    scene->add(AmbientLight::create(Color(0xfff4e7), 0.68f));
    auto key = DirectionalLight::create(Color(0xffe4ca), 3.1f);
    key->position.set(-2.6f, 4.2f, 2.8f);
    key->castShadow = true;
    key->shadow->mapSize.set(2048.f, 2048.f);
    key->shadow->bias = -0.00015f;
    key->shadow->normalBias = 0.025f;
    auto keyTarget = Group::create();
    keyTarget->position.set(-0.5f, 0.82f, 0.22f);
    key->setTarget(*keyTarget);
    scene->add(keyTarget);
    scene->add(key);
    auto rim = DirectionalLight::create(Color(0xc6d8f3), 1.1f);
    rim->position.set(1.7f, 2.2f, -1.4f);
    scene->add(rim);
    auto fill = DirectionalLight::create(Color(0xf0f3fb), 1.1f);
    fill->position.set(0.8f, 2.4f, 3.4f);
    auto fillTarget = Group::create();
    fillTarget->position.set(-0.5f, 0.9f, 0.15f);
    fill->setTarget(*fillTarget);
    scene->add(fillTarget);
    scene->add(fill);

    constexpr int width = 854, height = 480;
    Canvas canvas(Canvas::Parameters().size(width, height).headless(true));
    GLRenderer renderer(canvas);
    renderer.setClearColor(Color(0x343b45));
    renderer.toneMapping = ToneMapping::ACESFilmic;
    renderer.toneMappingExposure = 1.05f;
    renderer.shadowMap().enabled = true;
    renderer.shadowMap().type = ShadowMap::PFCSoft;
    scene->traverse([](Object3D& node) {
        if (auto* mesh = dynamic_cast<Mesh*>(&node)) {
            mesh->castShadow = true;
            mesh->receiveShadow = true;
        }
    });
    auto camera = PerspectiveCamera::create(43.f, float(width) / height, 0.1f, 100.f);
    camera->position.set(deskX + 0.62f, 1.42f, 2.62f);
    camera->lookAt(Vector3(deskX, 0.88f, 0.34f));
    scene->updateMatrixWorld(true);
    EffectComposer composer(renderer);
    composer.renderToScreen = true;
    composer.setSize(width, height);
    composer.addPass(std::make_shared<RenderPass>(*scene, *camera));
    auto bokeh = std::make_shared<BokehPass>(*scene, *camera, 2.2f, 0.0015f, 0.008f);
    composer.addPass(bokeh);
    composer.render();
    auto pixels = renderer.readRGBPixels();
    if (pixels.size() != size_t(width * height * 3)) throw std::runtime_error("unexpected RGB readback size");
    std::ofstream ppm(output, std::ios::binary);
    if (!ppm) throw std::runtime_error("cannot write preview: " + output.string());
    ppm << "P6\n" << width << " " << height << "\n255\n";
    for (int y = height - 1; y >= 0; --y) ppm.write(reinterpret_cast<const char*>(&pixels[y * width * 3]), width * 3);
    std::cout << "scene03 preview written: " << output << "\n"
              << "room bounds=" << roomSize.x << "," << roomSize.y << "," << roomSize.z
              << " center=" << roomCenter.x << "," << roomCenter.y << "," << roomCenter.z << "\n"
              << "subject=Sketchfab-low-poly-man-rig\n";
    renderer.dispose();
    return 0;
}

}// namespace

int main(int argc, char** argv) {
    try {
        if (argc != 4) {
            std::cerr << "usage: chrononmotion_render_scene_seated_desk <room.glb> <preview.ppm> <character.glb>\n";
            return 2;
        }
        return renderPreview(argv[1], argv[2], argv[3]);
    } catch (const std::exception& e) {
        std::cerr << "scene03: " << e.what() << "\n";
        return 1;
    }
}
