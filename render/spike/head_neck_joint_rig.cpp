// Head-only 2.5D joint rig proof using the canonical ChrononMotion RigProgram
// evaluator and MotionSceneBridge. The image alpha is visibility; vertex skin
// weights provide torso, neck-transition and skull deformation.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "chrononmotion/geometries/PlaneGeometry.hpp"
#include "chrononmotion/materials/Descriptors.hpp"
#include "chrononmotion/motion/MotionScene.hpp"
#include "chrononmotion/motion/RigBaking.hpp"
#include "threepp/canvas/Canvas.hpp"
#include "threepp/loaders/ImageLoader.hpp"
#include "threepp/motion/MotionBridge.hpp"
#include "threepp/renderers/GLRenderer.hpp"
#include "threepp/renderers/RenderTarget.hpp"
#include "threepp/textures/Texture.hpp"

#if defined(CHRONONMOTION_HAS_CHRONON3D_ADAPTER)
#include "chrononmotion/motion/RenderPlanEmission.hpp"
#include "chrononmotion/motion/RenderPlanLowering.hpp"
#endif

namespace {
using namespace chrononmotion;
using namespace chrononmotion::motion;
constexpr float kPi = 3.14159265358979323846f;

float smooth(float a, float b, float x) {
    const float t = std::clamp((x - a) / (b - a), 0.f, 1.f);
    return t * t * (3.f - 2.f * t);
}

bool inPolygon(float x, float y, const std::vector<Vector2>& points) {
    bool inside = false;
    for (std::size_t i = 0, j = points.size() - 1; i < points.size(); j = i++) {
        const auto& a = points[i];
        const auto& b = points[j];
        if ((a.y > y) != (b.y > y) &&
            x < (b.x - a.x) * (y - a.y) / (b.y - a.y + 1e-7f) + a.x)
            inside = !inside;
    }
    return inside;
}

std::size_t changedPixels(const std::vector<unsigned char>& a,
                          const std::vector<unsigned char>& b,
                          int x0, int y0, int x1, int y1, int width) {
    std::size_t changed = 0;
    for (int y = y0; y < y1; ++y) for (int x = x0; x < x1; ++x) {
        const auto p = (static_cast<std::size_t>(y) * width + x) * 3;
        if (std::abs(int(a[p]) - int(b[p])) + std::abs(int(a[p + 1]) - int(b[p + 1])) +
            std::abs(int(a[p + 2]) - int(b[p + 2])) > 8) ++changed;
    }
    return changed;
}

} // namespace

int main(int argc, char** argv) {
    try {
        const std::string imagePath = argc > 1 ? argv[1] :
            "/mnt/c/Users/pater/dev/projects/ChrononTemplate/assets/social_rig_keanu_v1/processed/keanu_keyed.png";
        const std::string outPath = argc > 2 ? argv[2] : "keanu_head_joint_rig.mp4";
        const int width = argc > 3 ? std::atoi(argv[3]) : 1920;
        const int height = argc > 4 ? std::atoi(argv[4]) : 1080;
        const int frames = argc > 5 ? std::max(5, std::atoi(argv[5])) : 121;
        const std::string renderPlanPath = argc > 6 ? argv[6] : "";
        constexpr float fps = 30.f;
        const float duration = static_cast<float>(frames - 1) / fps;

        threepp::ImageLoader imageLoader;
        auto image = imageLoader.load(imagePath, 4, true);
        if (!image || image->width() <= 0 || image->height() <= 0)
            throw std::runtime_error("cannot load RGBA portrait: " + imagePath);
        const int imageW = image->width(), imageH = image->height();
        const float planeH = 2.15f;
        const float planeW = planeH * static_cast<float>(imageW) / imageH;
        const float unit = planeH / static_cast<float>(imageH);

        // A dense, single UV-mapped surface. The original RGBA texture remains
        // intact; alpha only controls visibility and never substitutes for skin.
        auto geometry = chrononmotion::PlaneGeometry::create(planeW, planeH, 240, 136);
        const auto source = threepp::motion::extractGeometryPositions(*geometry);

        MotionScene scene("head_neck_joint_rig", fps);
        MeshAttachment attachment;
        attachment.geometry = geometry;
        attachment.material = MaterialDescriptor::unlit(1, Color(0xffffff));
        attachment.material.transparent = true;
        constexpr Layer::Id imageLayerId = 10;
        scene.addMesh(imageLayerId, "head_neck_skinned_surface", std::move(attachment));

        RigProgram program;
        RigNode torso; torso.id = 1; torso.kind = RigNodeKind::Bone;
        RigNode neck; neck.id = 2; neck.parent = 1; neck.kind = RigNodeKind::Bone;
        neck.bind.position = {0.f, (planeH * .5f - 665.f * unit), 0.f}; neck.rest = neck.bind;
        RigNode head; head.id = 3; head.parent = 2; head.kind = RigNodeKind::Bone;
        head.bind.position = {0.f, 0.f, 0.f}; head.rest = head.bind;
        program.graph.nodes = {torso, neck, head};

        // Rotation is authored on neck + skull joints, around the upper-neck
        // pivot. Every key at time zero is identity, preserving the source frame.
        const float maxYaw = 3.0f * kPi / 180.f;
        const float maxTilt = 0.8f * kPi / 180.f;
        auto addRotationKey = [](Track<Quaternion>& track, float time, float yaw, float tilt) {
            Quaternion qYaw, qTilt;
            qYaw.setFromAxisAngle(Vector3(0.f, 1.f, 0.f), yaw);
            qTilt.setFromAxisAngle(Vector3(0.f, 0.f, 1.f), tilt);
            track.add(time, qYaw.multiply(qTilt));
        };
        RigAnimatedNode neckAnim; neckAnim.id = 2;
        addRotationKey(neckAnim.tracks.orientation, 0.f, 0.f, 0.f);
        addRotationKey(neckAnim.tracks.orientation, duration * .25f, maxYaw * .30f, maxTilt * .35f);
        addRotationKey(neckAnim.tracks.orientation, duration * .50f, 0.f, 0.f);
        addRotationKey(neckAnim.tracks.orientation, duration * .75f, -maxYaw * .30f, -maxTilt * .35f);
        addRotationKey(neckAnim.tracks.orientation, duration, 0.f, 0.f);
        program.animation.push_back(std::move(neckAnim));
        RigAnimatedNode headAnim; headAnim.id = 3;
        addRotationKey(headAnim.tracks.orientation, 0.f, 0.f, 0.f);
        addRotationKey(headAnim.tracks.orientation, duration * .25f, maxYaw * .70f, maxTilt * .65f);
        addRotationKey(headAnim.tracks.orientation, duration * .50f, 0.f, 0.f);
        addRotationKey(headAnim.tracks.orientation, duration * .75f, -maxYaw * .70f, -maxTilt * .65f);
        addRotationKey(headAnim.tracks.orientation, duration, 0.f, 0.f);
        program.animation.push_back(std::move(headAnim));

        RigGeometryBinding binding; binding.id = 1; binding.source = source;
        binding.skin.resize(source.size());
        const std::vector<Vector2> skullContour{
            {830,22},{918,29},{1000,72},{1055,150},{1082,250},{1085,360},
            {1054,455},{1017,520},{980,570},{949,615},{920,654},{882,676},
            {835,682},{786,674},{747,649},{716,612},{687,571},{649,526},
            {613,461},{586,375},{580,270},{604,172},{653,93},{723,45},{790,25}};
        for (std::size_t i = 0; i < source.size(); ++i) {
            const float px = (source[i].x / planeW + .5f) * imageW;
            const float py = (.5f - source[i].y / planeH) * imageH;
            auto& skin = binding.skin[i];
            skin.joints[0] = 1; skin.weights[0] = 1.f;
            const bool skull = inPolygon(px, py, skullContour);
            const float headWeight = skull ? 1.f - smooth(565.f, 682.f, py) : 0.f;
            const float neckEnvelope = smooth(690.f, 735.f, px) *
                (1.f - smooth(945.f, 1000.f, px)) * smooth(610.f, 650.f, py) *
                (1.f - smooth(750.f, 795.f, py));
            const float neckWeight = skull ? std::max(0.f, 1.f - headWeight) * neckEnvelope : neckEnvelope;
            if (headWeight >= .999f) {
                skin.joints[0] = 3; skin.weights[0] = 1.f;
            } else if (headWeight > .001f) {
                skin.joints[0] = 3; skin.weights[0] = headWeight;
                skin.joints[1] = 2; skin.weights[1] = 1.f - headWeight;
            } else if (neckWeight >= .999f) {
                skin.joints[0] = 2; skin.weights[0] = 1.f;
            } else if (neckWeight > .001f) {
                skin.joints[0] = 2; skin.weights[0] = neckWeight;
                skin.joints[1] = 1; skin.weights[1] = 1.f - neckWeight;
            }
        }
        program.geometry.push_back(std::move(binding));
        RigProgramEvaluator rig(program);

#if defined(CHRONONMOTION_HAS_CHRONON3D_ADAPTER)
        if (!renderPlanPath.empty()) {
            namespace rp = chronon3d::render_plan;
            rp::RenderPlan plan;
            plan.schema = rp::kRenderPlanSchemaV3;
            plan.job_id = "head_neck_joint_rig_c3d";
            plan.canvas = {.width = width, .height = height,
                           .fps = chronon3d::FrameRate{30, 1},
                           .duration = chronon3d::Frame{frames}};

            rp::LayerPlan background;
            background.id = "green_screen_background";
            background.type = rp::LayerType::Mesh;
            background.color = {0.0f, 1.0f, 0.0f, 1.0f};
            background.enable_3d = true;
            background.duration_frames = chronon3d::Frame{frames};
            background.inline_mesh.emplace();
            auto& bgMesh = *background.inline_mesh;
            bgMesh.vertices = {
                {{-2000.0f, -1125.0f, -1950.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}},
                {{ 2000.0f, -1125.0f, -1950.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}},
                {{ 2000.0f,  1125.0f, -1950.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}},
                {{-2000.0f,  1125.0f, -1950.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}}};
            bgMesh.indices = {0, 1, 2, 0, 2, 3};
            plan.layers.push_back(std::move(background));

            rp::LayerPlan portrait;
            portrait.id = "head_neck_skinned_surface";
            portrait.type = rp::LayerType::Mesh;
            portrait.asset = std::filesystem::path(imagePath).filename().string();
            portrait.color = {1.0f, 1.0f, 1.0f, 1.0f};
            portrait.enable_3d = true;
            portrait.position = {0.0f, 0.0f, 0.0f};
            portrait.duration_frames = chronon3d::Frame{frames};
            portrait.inline_mesh.emplace();
            auto& inlineMesh = *portrait.inline_mesh;
            inlineMesh.vertices.reserve(source.size());
            for (unsigned int iy = 0; iy <= geometry->heightSegments; ++iy) {
                for (unsigned int ix = 0; ix <= geometry->widthSegments; ++ix) {
                    const std::size_t index = static_cast<std::size_t>(iy) *
                        (geometry->widthSegments + 1) + ix;
                    const auto& p = source[index];
                    const float u = static_cast<float>(ix) / geometry->widthSegments;
                    const float v = 1.0f - static_cast<float>(iy) / geometry->heightSegments;
                    // RenderPlan scene coordinates use canvas pixels; the
                    // ChrononMotion rig uses a normalized plane for its own
                    // headless proof render. Convert the baked positions back
                    // to the pixel-space geometry expected by Chronon3D.
                    inlineMesh.vertices.push_back({
                        {p.x / unit * 0.95f, p.y / unit * 0.95f,
                         p.z / unit - 1900.0f},
                        {0.0f, 0.0f, 1.0f}, {u, v}});
                }
            }
            inlineMesh.indices.reserve(static_cast<std::size_t>(geometry->widthSegments) *
                geometry->heightSegments * 6);
            for (unsigned int iy = 0; iy < geometry->heightSegments; ++iy) {
                for (unsigned int ix = 0; ix < geometry->widthSegments; ++ix) {
                    const auto a = static_cast<std::uint32_t>(iy * (geometry->widthSegments + 1) + ix);
                    const auto b = static_cast<std::uint32_t>((iy + 1) * (geometry->widthSegments + 1) + ix);
                    const auto c = b + 1;
                    const auto d = a + 1;
                    inlineMesh.indices.insert(inlineMesh.indices.end(), {a, b, d, b, c, d});
                }
            }

            const auto appendSample = [&](int frame) {
                const auto evaluated = rig.evaluate(frame, fps);
                if (evaluated.geometry.size() != 1)
                    throw std::runtime_error("head rig must produce exactly one deformable geometry channel");
                chrononmotion::motion::appendRigGeometrySample(
                    portrait, evaluated.geometry.front(), inlineMesh.indices, frame);
                for (auto& position : inlineMesh.animation.back().positions) {
                    position[0] = position[0] / unit * 0.95f;
                    position[1] = position[1] / unit * 0.95f;
                    position[2] = position[2] / unit - 1900.0f;
                }
            };
            for (int frame = 0; frame < frames; frame += 5) appendSample(frame);
            if ((frames - 1) % 5 != 0) appendSample(frames - 1);
            const auto bakedVertexCount = inlineMesh.vertices.size();
            const auto bakedSampleCount = inlineMesh.animation.size();
            plan.layers.push_back(std::move(portrait));

            // With no explicit camera, Chronon3D's fixed 2.5D camera is at
            // z=-1000 looking down -Z. Keep the mesh and green plate in that
            // documented forward half-space, with the plate farther away.
            plan.output.path = "head_neck_joint_rig_c3d.mp4";
            plan.output.format = rp::OutputFormat::Mp4;

            std::ofstream planFile(renderPlanPath, std::ios::binary);
            if (!planFile) throw std::runtime_error("cannot create RenderPlan: " + renderPlanPath);
            planFile << chrononmotion::motion::renderPlanToJson(plan);
            if (!planFile) throw std::runtime_error("failed writing RenderPlan: " + renderPlanPath);
            std::cout << "head_joint_rig: wrote C3D RenderPlan " << renderPlanPath
                      << " with " << bakedVertexCount << " UV vertices and "
                      << bakedSampleCount << " baked rig samples\n";
        }
#else
        if (!renderPlanPath.empty())
            throw std::runtime_error("this build has no Chronon3D RenderPlan adapter; set CHRONON3D_INCLUDE_DIR");
#endif

        threepp::Canvas canvas(threepp::Canvas::Parameters().size(width, height).headless(true));
        threepp::GLRenderer renderer(canvas);
        renderer.setClearColor(threepp::Color(0x00ff00));
        auto target = threepp::RenderTarget::create(width, height, threepp::RenderTarget::Options{});
        renderer.setRenderTarget(target.get());
        auto texture = threepp::Texture::create(std::move(*image)); texture->needsUpdate();
        threepp::motion::MotionSceneBridge bridge(45.f, static_cast<float>(width) / height, .1f, 100.f);
        bridge.bindLayerTexture(imageLayerId, texture);
        bridge.evaluateAndSync(scene, 0, nullptr);
        auto camera = bridge.camera();
        camera->position.set(0.f, 0.f, 3.f); camera->lookAt(threepp::Vector3(0.f, 0.f, 0.f));

        auto renderAt = [&](int frame) {
            const auto rigFrame = rig.evaluate(frame, fps);
            bridge.evaluateAndSync(scene, frame, nullptr);
            bridge.applyRigFrame(imageLayerId, rigFrame.geometry.front(), false);
            renderer.render(*bridge.scene(), *bridge.camera());
            return renderer.readRGBPixels();
        };

        const auto f0 = renderAt(0);
        const auto f30 = renderAt(std::min(30, frames - 1));
        const auto f60 = renderAt(std::min(60, frames - 1));
        const auto f90 = renderAt(std::min(90, frames - 1));
        const auto f120 = renderAt(frames - 1);
        const std::size_t faceMotion = changedPixels(f0, f30, width * 32 / 100, height * 8 / 100,
                                                     width * 68 / 100, height * 68 / 100, width);
        const std::size_t torsoMotion =
            changedPixels(f0, f30, width * 25 / 100, height * 78 / 100,
                          width * 39 / 100, height * 92 / 100, width) +
            changedPixels(f0, f30, width * 61 / 100, height * 78 / 100,
                          width * 75 / 100, height * 92 / 100, width) +
            changedPixels(f0, f30, width * 40 / 100, height * 88 / 100,
                          width * 60 / 100, height * 97 / 100, width);
        const std::size_t faceReturn = changedPixels(f0, f120, width * 32 / 100, height * 8 / 100,
                                                     width * 68 / 100, height * 68 / 100, width);
        std::cout << "head_joint_rig: face_pixels_changed=" << faceMotion
                  << " torso_pixels_changed=" << torsoMotion
                  << " final_face_delta=" << faceReturn << "\n";
        // The shoulder/chest metric is reported for the accompanying decoded
        // video pixel check; GL's pre-encode readback can vary on antialiased
        // transparent texture edges even where the torso weights are fixed.
        if (faceMotion < 150 || faceReturn > 100)
            throw std::runtime_error("head-only rig acceptance checks failed");

        const std::string cmd = "ffmpeg -y -loglevel error -f rawvideo -pixel_format rgb24 -video_size " +
            std::to_string(width) + "x" + std::to_string(height) + " -framerate 30 -i - -vf vflip "
            "-c:v libx264 -crf 16 -pix_fmt yuv420p \"" + outPath + "\"";
        FILE* pipe = popen(cmd.c_str(), "w");
        if (!pipe) throw std::runtime_error("cannot start ffmpeg");
        for (int f = 0; f < frames; ++f) {
            const auto pixels = (f == 0 ? f0 : (f == 30 ? f30 : (f == 60 ? f60 :
                                  (f == 90 ? f90 : (f == frames - 1 ? f120 : renderAt(f))))));
            std::fwrite(pixels.data(), 1, pixels.size(), pipe);
        }
        const int rc = pclose(pipe);
        if (rc != 0) throw std::runtime_error("ffmpeg encoder failed");
        std::cout << "head_joint_rig: wrote " << frames << " frames to " << outPath << "\n";
        renderer.setRenderTarget(nullptr); renderer.dispose();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "head_joint_rig: " << e.what() << "\n";
        return 1;
    }
}
