// Two-bone IK demo on the seated-desk mannequins.
//
// The clip library proves that imported performances reach the renderer; this
// proof shows the new solver reaching a *moving target* on the actual UE4 and
// low-poly skeletons. Each character's right arm is driven by
// `RigIKChain::solver = TwoBone` against a world-space target, with a knee-safe
// minimum mid angle on the elbow. The target is also drawn as a small marker, so
// the frames where the joint limit stops the hand short are visible rather than
// described.
//
// The scene graph is threepp's (it owns the pixels); the solve is
// chrononmotion's (it owns space and time). The two math types are distinct, so
// every point crossing the boundary is converted explicitly here.
//
// Usage: chrononmotion_render_two_bone_demo [out.mp4] [width] [height] [frames]

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "chrononmotion/motion/Motion.hpp"

#include "threepp/animation/AnimationMixer.hpp"
#include "threepp/cameras/PerspectiveCamera.hpp"
#include "threepp/canvas/Canvas.hpp"
#include "threepp/core/Object3D.hpp"
#include "threepp/geometries/SphereGeometry.hpp"
#include "threepp/lights/AmbientLight.hpp"
#include "threepp/lights/DirectionalLight.hpp"
#include "threepp/loaders/GLTFLoader.hpp"
#include "threepp/materials/MeshBasicMaterial.hpp"
#include "threepp/math/Box3.hpp"
#include "threepp/objects/Bone.hpp"
#include "threepp/objects/Group.hpp"
#include "threepp/objects/Mesh.hpp"
#include "threepp/objects/SkinnedMesh.hpp"
#include "threepp/objects/Skeleton.hpp"
#include "threepp/renderers/GLRenderer.hpp"
#include "threepp/renderers/RenderTarget.hpp"
#include "threepp/scenes/Scene.hpp"

using namespace threepp;

namespace cm = chrononmotion;
namespace cmm = chrononmotion::motion;

namespace {

    constexpr float kFps = 30.f;
    constexpr float kSeconds = 9.f;

    cm::Vector3 toMotion(const Vector3& v) { return {v.x, v.y, v.z}; }
    Vector3 toThree(const cm::Vector3& v) { return {v.x, v.y, v.z}; }
    cm::Quaternion toMotion(const Quaternion& q) { return cm::Quaternion(q.x, q.y, q.z, q.w); }

    float smoothstep(float edge0, float edge1, float x) {
        const float t = std::clamp((x - edge0) / (edge1 - edge0), 0.f, 1.f);
        return t * t * (3.f - 2.f * t);
    }

    cm::Vector3 normalizedOr(const cm::Vector3& v, const cm::Vector3& fallback) {
        if (v.lengthSq() <= 1e-12f) return fallback;
        cm::Vector3 out = v;
        out.normalize();
        return out;
    }

    cmm::Transform localTransform(const Bone& bone) {
        cmm::Transform t;
        t.position = toMotion(bone.position);
        t.orientation = toMotion(bone.quaternion);
        t.scale = toMotion(bone.scale);
        return t;
    }

    cmm::Transform worldTransform(const Bone& bone) {
        Vector3 position, scale;
        Quaternion orientation;
        bone.matrixWorld->decompose(position, orientation, scale);
        cmm::Transform t;
        t.position = toMotion(position);
        t.orientation = toMotion(orientation);
        t.scale = toMotion(scale);
        return t;
    }

    SkinnedMesh* findSkinned(Object3D& root) {
        SkinnedMesh* found = nullptr;
        root.traverse([&](Object3D& node) {
            if (found) return;
            if (auto* skin = dynamic_cast<SkinnedMesh*>(&node); skin && skin->skeleton) found = skin;
        });
        return found;
    }

    struct Character {
        std::string label;
        std::shared_ptr<Group> root;
        std::shared_ptr<Skeleton> skeleton;
        std::vector<std::shared_ptr<Bone>> path;///< root bone .. hand, parent first
        std::vector<cmm::Transform> localRest;
        std::vector<cmm::RigNodeId> ids;
        cmm::RigGraph graph;
        std::optional<cmm::RigEvaluator> evaluator;
        float reach{1.f};
        float outward{1.f};///< screen side the reach travels to, so both arms stay clear
        cm::Vector3 shoulder{};
        Mesh* marker{nullptr};
    };

    std::shared_ptr<Bone> boneByName(const std::shared_ptr<Skeleton>& skeleton, const std::string& name) {
        for (const auto& bone : skeleton->bones) {
            if (bone && bone->name == name) return bone;
        }
        return nullptr;
    }

    std::vector<std::shared_ptr<Bone>> pathTo(const std::shared_ptr<Skeleton>& skeleton,
                                              const std::shared_ptr<Bone>& tip) {
        std::vector<std::shared_ptr<Bone>> path;
        std::shared_ptr<Bone> current = tip;
        while (current) {
            path.push_back(current);
            auto* parent = dynamic_cast<Bone*>(current->parent);
            if (!parent) break;
            std::shared_ptr<Bone> parentBone;
            for (const auto& candidate : skeleton->bones) {
                if (candidate && candidate.get() == parent) parentBone = candidate;
            }
            if (!parentBone) break;
            current = parentBone;
        }
        std::reverse(path.begin(), path.end());
        return path;
    }

    void buildRig(Character& character, const std::shared_ptr<Group>& root,
                  const std::vector<std::string>& names) {
        character.root = root;
        auto* skin = findSkinned(*root);
        if (!skin) throw std::runtime_error(character.label + ": no skinned mesh in the GLB");
        character.skeleton = skin->skeleton;

        std::shared_ptr<Bone> tip;
        for (const auto& name : names) {
            auto bone = boneByName(character.skeleton, name);
            if (!bone) throw std::runtime_error(character.label + ": missing bone " + name);
            tip = bone;
        }
        character.path = pathTo(character.skeleton, tip);
        // The graph root is the path's top bone, and its local transform is
        // expressed in the armature's space. Four bones guarantee that the
        // skipped root is never one of the three solved joints.
        if (character.path.size() < 4)
            throw std::runtime_error(character.label + ": arm path is too short for a two-bone chain");

        root->updateMatrixWorld(true);
        character.localRest.clear();
        character.ids.clear();
        character.graph = cmm::RigGraph{};
        for (std::size_t i = 0; i < character.path.size(); ++i) {
            const cmm::Transform local = localTransform(*character.path[i]);
            character.localRest.push_back(local);

            cmm::RigNode node;
            node.id = static_cast<cmm::RigNodeId>(i + 1);
            node.parent = i == 0 ? 0 : static_cast<cmm::RigNodeId>(i);
            // The top bone has no parent inside the graph, so its rest is the
            // world pose the placed character already has.
            node.rest = i == 0 ? worldTransform(*character.path[i]) : local;
            node.bind = node.rest;
            character.graph.nodes.push_back(node);
            character.ids.push_back(node.id);
        }
        character.evaluator.emplace(character.graph.compile());

        const std::size_t n = character.path.size();
        Vector3 a, b, c;
        character.path[n - 3]->getWorldPosition(a);
        character.path[n - 2]->getWorldPosition(b);
        character.path[n - 1]->getWorldPosition(c);
        character.shoulder = toMotion(a);
        character.reach = a.distanceTo(b) + b.distanceTo(c);
    }

    void restoreRest(Character& character) {
        for (std::size_t i = 0; i < character.path.size(); ++i) {
            const auto& rest = character.localRest[i];
            auto& bone = *character.path[i];
            bone.position.set(rest.position.x, rest.position.y, rest.position.z);
            bone.quaternion.set(rest.orientation.x, rest.orientation.y, rest.orientation.z,
                                rest.orientation.w);
            bone.scale.set(rest.scale.x, rest.scale.y, rest.scale.z);
        }
        character.root->updateMatrixWorld(true);
    }

    cm::Vector3 targetFor(const Character& character, float t) {
        // Three beats: extend, sweep across the body, then pull in until the
        // elbow limit (not the solver) decides how close the hand can get.
        const float sweep = std::sin(t * 3.14159265f);
        const float extend = smoothstep(0.00f, 0.45f, t);
        const float pull = smoothstep(0.68f, 0.96f, t);
        // The sweep is mostly lateral on purpose: a reach straight toward the
        // camera is foreshortened into a few pixels and hides the elbow.
        const cm::Vector3 direction = normalizedOr(
                cm::Vector3(character.outward * (0.05f + 0.90f * sweep),
                            0.30f - 0.55f * smoothstep(0.25f, 0.75f, t), 0.30f),
                cm::Vector3(0.f, 0.f, 1.f));
        const float fraction = 0.72f + 0.22f * extend - 0.58f * pull;
        return character.shoulder + direction * (character.reach * fraction);
    }

    struct FrameStats {
        float handError{0.f};
        float elbowAngle{0.f};
    };

    FrameStats poseCharacter(Character& character, float seconds, float duration) {
        restoreRest(character);
        const float t = std::clamp(seconds / duration, 0.f, 1.f);
        const cm::Vector3 target = targetFor(character, t);

        cmm::RigIKChain chain;
        chain.joints = {character.ids[character.ids.size() - 3], character.ids[character.ids.size() - 2],
                        character.ids.back()};
        chain.target = target;
        // A point below and behind the shoulder keeps the elbow bending down.
        chain.pole = character.shoulder + cm::Vector3(0.15f, -1.4f, -0.35f);
        chain.solver = cmm::RigIKSolverKind::TwoBone;
        chain.minMidAngle = 1.15f;// 66 degrees of fold, the joint's own limit
        chain.maxMidAngle = 3.14159265f;

        const cmm::RigPose pose = character.evaluator->solveIK(chain);
        for (const auto& value : pose) {
            if (value.id < 2) continue;// the graph root carries a world pose
            auto& bone = *character.path[value.id - 1];
            const auto local = value.local;
            const cm::Quaternion q = local.localQuaternion();
            bone.position.set(local.position.x, local.position.y, local.position.z);
            bone.quaternion.set(q.x, q.y, q.z, q.w);
            bone.scale.set(local.scale.x, local.scale.y, local.scale.z);
        }
        character.root->updateMatrixWorld(true);

        Vector3 handThree, elbowThree, wristThree, shoulderThree;
        character.path.back()->getWorldPosition(handThree);
        character.path[character.path.size() - 3]->getWorldPosition(shoulderThree);
        character.path[character.path.size() - 2]->getWorldPosition(elbowThree);
        character.path.back()->getWorldPosition(wristThree);
        const cm::Vector3 upper = toMotion(shoulderThree) - toMotion(elbowThree);
        const cm::Vector3 lower = toMotion(wristThree) - toMotion(elbowThree);
        float angle = 0.f;
        if (upper.lengthSq() > 1e-12f && lower.lengthSq() > 1e-12f) {
            cm::Vector3 u = upper, l = lower;
            u.normalize();
            l.normalize();
            angle = std::acos(std::clamp(u.dot(l), -1.f, 1.f));
        }
        return {toMotion(handThree).distanceTo(target), angle};
    }

    // Mesh bounds are unreliable for these assets (`low-poly_man.glb` carries a
    // 0.00167 root scale that the skeleton does not share), so the placement is
    // solved from the skeleton's own world positions instead.
    void placeAt(const std::shared_ptr<Group>& root, float x, float height) {
        auto* skin = findSkinned(*root);
        if (!skin) throw std::runtime_error("two_bone_demo: placement needs a skeleton");
        const auto skeleton = skin->skeleton;

        const auto skeletonBounds = [&]() {
            Vector3 min(1e30f, 1e30f, 1e30f);
            Vector3 max(-1e30f, -1e30f, -1e30f);
            for (const auto& bone : skeleton->bones) {
                if (!bone) continue;
                Vector3 p;
                bone->getWorldPosition(p);
                min.x = std::min(min.x, p.x);
                min.y = std::min(min.y, p.y);
                min.z = std::min(min.z, p.z);
                max.x = std::max(max.x, p.x);
                max.y = std::max(max.y, p.y);
                max.z = std::max(max.z, p.z);
            }
            return std::pair<Vector3, Vector3>(min, max);
        };

        root->updateMatrixWorld(true);
        auto [min0, max0] = skeletonBounds();
        const float skeletonHeight = std::max(max0.y - min0.y, 1e-5f);
        root->scale.multiplyScalar(height / skeletonHeight);
        root->updateMatrixWorld(true);

        auto [min1, max1] = skeletonBounds();
        const float centerX = (min1.x + max1.x) * 0.5f;
        const float centerZ = (min1.z + max1.z) * 0.5f;
        root->position.x += x - centerX;
        root->position.y += -min1.y;
        root->position.z += -centerZ;
        root->updateMatrixWorld(true);
    }

    std::shared_ptr<Mesh> makeMarker() {
        auto material = MeshBasicMaterial::create(MeshBasicMaterial::Params().color(Color(0xffc23d)));
        material->toneMapped = false;
        return Mesh::create(SphereGeometry::create(0.035f, 16, 12), material);
    }

}// namespace

int main(int argc, char** argv) {
    std::string outPath = "two_bone_ik_demo.mp4";
    int width = 1280;
    int height = 720;
    int totalFrames = static_cast<int>(kFps * kSeconds) + 1;
    if (argc > 1) outPath = argv[1];
    if (argc > 2) width = std::atoi(argv[2]);
    if (argc > 3) height = std::atoi(argv[3]);
    if (argc > 4) totalFrames = std::max(4, std::atoi(argv[4]));

    const float duration = static_cast<float>(totalFrames - 1) / kFps;
    const std::filesystem::path assetRoot = "render/spike/assets/scene03";

    Canvas canvas(Canvas::Parameters().size(width, height).headless(true));
    GLRenderer renderer(canvas);
    // Studio grey, for the same reason the clip library uses it: these
    // mannequins are dark, and a near-black clear colour swallows the silhouette.
    renderer.setClearColor(Color(0x9aa0a8));
    auto renderTarget = RenderTarget::create(width, height, RenderTarget::Options{});
    renderer.setRenderTarget(renderTarget.get());

    auto scene = Scene::create();
    scene->add(AmbientLight::create(Color(0xdfe8ff), 2.2f));
    auto key = DirectionalLight::create(Color(0xfff1dc), 4.5f);
    key->position.set(3.5f, 5.0f, 4.5f);
    auto keyTarget = Group::create();
    keyTarget->position.set(0.f, 1.f, 0.f);
    scene->add(keyTarget);
    key->setTarget(*keyTarget);
    scene->add(key);
    auto rim = DirectionalLight::create(Color(0x8fb7ff), 2.6f);
    rim->position.set(-4.f, 3.f, -5.f);
    auto rimTarget = Group::create();
    rimTarget->position.set(0.f, 1.2f, 0.f);
    scene->add(rimTarget);
    rim->setTarget(*rimTarget);
    scene->add(rim);

    GLTFLoader loader;
    std::vector<Character> characters;
    const std::array<std::pair<std::string, std::array<std::string, 3>>, 2> specs{{
            {(assetRoot / "UAL1_Standard.glb").string(), {"upperarm_r", "lowerarm_r", "hand_r"}},
            // The low-poly mannequin reaches with its *left* arm so both
            // characters extend away from the centre of the frame.
            {(assetRoot / "low-poly_man.glb").string(),
             {"L.upperArm_06", "L.lowerArm_07", "L.hand_00"}},
    }};

    for (std::size_t i = 0; i < specs.size(); ++i) {
        auto loaded = loader.load(specs[i].first);
        if (!loaded || !loaded->scene)
            throw std::runtime_error("two_bone_demo: failed to load " + specs[i].first);
        Character character;
        character.label = std::filesystem::path(specs[i].first).stem().string();
        scene->add(loaded->scene);
        auto marker = makeMarker();
        scene->add(marker);
        character.marker = marker.get();

        // Sample one idle frame so the mannequins stand instead of holding the
        // bind T-pose. The mixer is advanced once and then left alone: from here
        // on the solver, not the clip player, owns the arm.
        std::shared_ptr<AnimationClip> idleClip;
        for (const auto& clip : loaded->animations) {
            if (clip && clip->name() == "Idle_Loop") {
                idleClip = clip;
                break;
            }
        }
        for (const auto& clip : loaded->animations) {
            if (idleClip) break;
            if (clip && clip->name().rfind("Idle", 0) == 0) idleClip = clip;
        }
        for (const auto& clip : loaded->animations) {
            if (idleClip) break;
            if (clip && clip->name().find("Idle") != std::string::npos) idleClip = clip;
        }
        if (!idleClip && !loaded->animations.empty()) idleClip = loaded->animations.front();
        if (idleClip) {
            AnimationMixer mixer(*loaded->scene);
            auto* action = mixer.clipAction(idleClip);
            action->play();
            mixer.update(0.f);
            loaded->scene->updateMatrixWorld(true);
            std::cout << "two_bone_demo: " << character.label << " base pose '" << idleClip->name()
                      << "' at t=0\n";
        }

        // Place first, then capture the rig: the graph root is a world pose, so
        // it must be the *placed* pose the frames are solved in.
        character.outward = i == 0 ? -1.f : 1.f;
        placeAt(loaded->scene, i == 0 ? -1.05f : 1.05f, 1.78f);
        const std::vector<std::string> names(specs[i].second.begin(), specs[i].second.end());
        buildRig(character, loaded->scene, names);
        characters.push_back(std::move(character));
    }

    auto camera = PerspectiveCamera::create(34.f, float(width) / float(height), 0.05f, 100.f);
    camera->position.set(0.f, 1.03f, 3.75f);
    camera->lookAt(Vector3(0.f, 0.95f, 0.f));

    std::cout << "two_bone_demo: " << characters.size() << " characters, " << totalFrames
              << " frames at " << width << "x" << height << "\n";
    for (const auto& character : characters) {
        Vector3 head;
        character.path.back()->getWorldPosition(head);
        std::cout << "two_bone_demo: " << character.label << " reach=" << character.reach
                  << " chain=" << character.path.size() << " bones"
                  << " reach/height=" << (character.reach / 1.78f) << "\n";
        if (character.reach / 1.78f < 0.20f || character.reach / 1.78f > 0.55f) {
            std::cerr << "two_bone_demo: " << character.label
                      << " skeleton scale looks wrong; refusing to render a broken arm\n";
            return 1;
        }
    }

    auto renderFrame = [&](int frame) {
        const float seconds = static_cast<float>(frame) / kFps;
        for (auto& character : characters) {
            const auto stats = poseCharacter(character, seconds, duration);
            const auto t = std::clamp(seconds / duration, 0.f, 1.f);
            character.marker->position.copy(toThree(targetFor(character, t)));
            if (frame == 0 || frame == totalFrames / 2 || frame == totalFrames - 1) {
                std::cout << "  " << character.label << " f=" << frame
                          << " hand_error=" << stats.handError
                          << " elbow=" << (stats.elbowAngle * 57.2958f) << "deg\n";
            }
        }
        renderer.render(*scene, *camera);
        return renderer.readRGBPixels();
    };

    // Sanity: the solve must actually move the mannequins between the opening
    // and the pull-in, or the video is a still image with a moving marker.
    const auto first = renderFrame(0);
    const auto middle = renderFrame(totalFrames / 2);
    std::size_t changed = 0;
    for (std::size_t i = 0; i + 2 < first.size(); i += 3) {
        if (std::abs(int(first[i]) - int(middle[i])) > 8 ||
            std::abs(int(first[i + 1]) - int(middle[i + 1])) > 8 ||
            std::abs(int(first[i + 2]) - int(middle[i + 2])) > 8)
            ++changed;
    }
    std::cout << "two_bone_demo: changed pixels first->middle = " << changed << "\n";
    if (changed < 500) {
        std::cerr << "two_bone_demo: the IK solve did not move the characters\n";
        return 1;
    }

    const std::string filter =
            "vflip,"
            "drawtext=fontfile=/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf:"
            "text='TWO-BONE IK  |  ANALYTIC  |  DETERMINISTIC':fontsize=30:fontcolor=white:"
            "box=1:boxcolor=black@0.55:boxborderw=12:x=(w-text_w)/2:y=34,"
            "drawtext=fontfile=/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf:"
            "text='UAL1 (UE4 rig) + low-poly mannequin  -  elbow limit 66 deg':fontsize=22:"
            "fontcolor=0xffc23d:box=1:boxcolor=black@0.55:boxborderw=10:x=(w-text_w)/2:y=h-56";
    const std::string command =
            "ffmpeg -y -loglevel error -f rawvideo -pixel_format rgb24 -video_size " +
            std::to_string(width) + "x" + std::to_string(height) + " -framerate " +
            std::to_string(static_cast<int>(kFps)) + " -i - -vf \"" + filter +
            "\" -c:v libx264 -preset slow -crf 18 -pix_fmt yuv420p -movflags +faststart \"" +
            outPath + "\"";
    FILE* pipe = popen(command.c_str(), "w");
    if (!pipe) {
        std::cerr << "two_bone_demo: failed to open ffmpeg\n";
        return 1;
    }
    for (int frame = 0; frame < totalFrames; ++frame) {
        const auto pixels = renderFrame(frame);
        std::fwrite(pixels.data(), 1, pixels.size(), pipe);
    }
    const int rc = pclose(pipe);
    if (rc != 0) {
        std::cerr << "two_bone_demo: ffmpeg exited with " << rc << "\n";
        return 1;
    }
    std::cout << "two_bone_demo: wrote " << totalFrames << " frames to "
              << std::filesystem::absolute(outPath) << "\n";
    return 0;
}
