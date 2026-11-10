// Emit the resolved compositing stack for the showcase animation.
//
// This tool is deliberately not a renderer. It authors one Motion scene — an
// object that crosses a text plane twice — and writes, per frame, the order the
// *real* resolver returns in Stack and Depth mode, plus the camera and anchor
// positions the renderer needs. The renderer that turns this into an MP4 is
// `tools/render_compositing_showcase.py`; it composites in exactly the order
// this file emits.
//
// Build (from the repository root):
//   c++ -std=c++20 -O2 -Wall -Wextra -Werror -I ChrononMotion3D/include ChrononMotion3D/tools/composite_stack_showcase.cpp ChrononMotion3D/build/compositing/libchrononmotion.a -o ChrononMotion3D/build/compositing/composite_stack_showcase
//
// Run:
//   ./ChrononMotion3D/build/compositing/composite_stack_showcase RenderingGen/Upload/cm3d_compositing_20261110/showcase.json

#include "chrononmotion/motion/CameraRig.hpp"
#include "chrononmotion/motion/Compositing.hpp"
#include "chrononmotion/motion/MotionScene.hpp"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace chrononmotion;
using namespace chrononmotion::motion;

namespace {

    constexpr int kFps = 30;
    constexpr int kFrames = 240;
    constexpr int kWidth = 1920;
    constexpr int kHeight = 1080;

    constexpr float kTextWidth = 7.0f;
    constexpr float kTextHeight = 3.0f;
    constexpr float kObjectSize = 2.7f;

    void writeOrder(std::ostringstream& out, const CompositeStack& stack) {

        out << '[';
        for (std::size_t index = 0; index < stack.size(); ++index) {
            if (index != 0) out << ',';
            out << stack.backToFront[index].layerId;
        }
        out << ']';
    }

    [[nodiscard]] float depthOf(const CompositeStack& stack, Layer::Id id) {

        for (const CompositeDraw& draw : stack.backToFront)
            if (draw.layerId == id) return draw.viewDepth;
        throw std::runtime_error("composite stack is missing layer " + std::to_string(id));
    }

    void writeWorld(std::ostringstream& out, const MotionScene& scene, Layer::Id id) {

        const Matrix4& world = scene.worldMatrix(id);
        out << '[' << world.elements[12] << ',' << world.elements[13] << ','
            << world.elements[14] << ']';
    }

}// namespace

int main(int argc, char** argv) {

    const std::string outputPath =
        argc > 1 ? argv[1] : "compositing_showcase.json";

    // One scene, two layers, authored object-first. Stack mode therefore paints
    // the text last (in front); Depth mode is free to move it behind the object.
    MotionScene scene("compositing-showcase", static_cast<float>(kFps));

    MeshAttachment attachment;
    attachment.assetId = "showcase-object";
    Layer& object = scene.addMesh(1, "object", std::move(attachment));
    Layer& text = scene.addContent(
        2, "title", ContentRef::text("showcase-title", Vector2(kTextWidth, kTextHeight)));
    text.transform.position = Vector3(0.f, -0.35f, -2.f);

    // The object crosses the text plane twice: behind -> in front -> behind.
    object.tracks.position
        .add(0.f, Vector3(0.f, 0.20f, -6.f))
        .add(2.f, Vector3(0.f, 0.05f, -1.6f))
        .add(4.f, Vector3(0.f, -0.10f, 3.f))
        .add(6.f, Vector3(0.f, 0.05f, -1.6f))
        .add(8.f, Vector3(0.f, 0.20f, -6.f));

    CameraRig rig(1, 42.f, static_cast<float>(kWidth) / static_cast<float>(kHeight), 0.1f, 100.f);
    rig.setTarget(Vector3(0.f, 0.f, -0.2f));

    const CameraPose initialPose = rig.sample(0.f);
    (void)initialPose;

    std::ostringstream out;
    out << std::fixed << std::setprecision(6);

    out << "{\n";
    out << "  \"schema\": \"chrononmotion.compositing-showcase.v1\",\n";
    out << "  \"width\": " << kWidth << ",\n";
    out << "  \"height\": " << kHeight << ",\n";
    out << "  \"fps\": " << kFps << ",\n";
    out << "  \"frame_count\": " << kFrames << ",\n";
    out << "  \"layers\": [\n";
    out << "    {\"id\": 1, \"kind\": \"object\", \"label\": \"OBJECT\"},\n";
    out << "    {\"id\": 2, \"kind\": \"text\", \"label\": \"CHRONON\"}\n";
    out << "  ],\n";
    out << "  \"props\": {\"object_size\": " << kObjectSize
        << ", \"text_width\": " << kTextWidth
        << ", \"text_height\": " << kTextHeight << "},\n";
    out << "  \"samples\": [\n";

    for (int frame = 0; frame < kFrames; ++frame) {
        const double seconds = static_cast<double>(frame) / static_cast<double>(kFps);

        scene.evaluate(frame);

        rig.setPosition(Vector3(
            static_cast<float>(2.2 * std::sin(seconds * 0.62)),
            static_cast<float>(0.55 + 0.12 * std::sin(seconds * 0.9)),
            static_cast<float>(11.5 + 0.35 * std::cos(seconds * 0.45))));
        rig.setTarget(Vector3(0.f, 0.f, -0.2f));
        const CameraPose pose = rig.sample(static_cast<float>(seconds));

        // Both orders from the same evaluated state: this is the comparison the
        // animation is about.
        scene.setCompositeMode(CompositeMode::Stack);
        const CompositeStack stacked = resolveCompositeStack(scene, pose);
        scene.setCompositeMode(CompositeMode::Depth);
        const CompositeStack depthSorted = resolveCompositeStack(scene, pose);

        out << "    {\"frame\": " << frame << ", \"seconds\": " << seconds;
        out << ", \"camera\": {\"position\": [" << pose.position.x << ','
            << pose.position.y << ',' << pose.position.z << "], \"target\": [0, 0, -0.2]"
            << ", \"fov\": " << pose.fov << "}";
        out << ", \"order_stack\": ";
        writeOrder(out, stacked);
        out << ", \"order_depth\": ";
        writeOrder(out, depthSorted);
        out << ", \"layers\": [";
        for (int index = 0; index < 2; ++index) {
            const Layer::Id id = index == 0 ? 1 : 2;
            if (index != 0) out << ", ";
            out << "{\"id\": " << id << ", \"depth\": " << depthOf(depthSorted, id)
                << ", \"world\": ";
            writeWorld(out, scene, id);
            out << '}';
        }
        out << "]}";
        if (frame + 1 != kFrames) out << ',';
        out << '\n';
    }

    out << "  ]\n";
    out << "}\n";

    std::ofstream file(outputPath, std::ios::binary);
    if (!file.good())
        throw std::runtime_error("cannot open " + outputPath + " for writing");
    file << out.str();
    file.close();
    if (!file.good())
        throw std::runtime_error("failed while writing " + outputPath);

    std::printf("composite_stack_showcase: wrote %s (%d frames, %dx%d @ %d)\n",
                outputPath.c_str(), kFrames, kWidth, kHeight, kFps);
    return 0;
}
