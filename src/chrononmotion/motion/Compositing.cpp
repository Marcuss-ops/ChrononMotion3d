#include "chrononmotion/motion/Compositing.hpp"

#include "chrononmotion/motion/CameraScreenSpace.hpp"
#include "chrononmotion/motion/MotionScene.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>

namespace chrononmotion::motion {
    namespace {

        /// The anchor is the world translation of the composed matrix: Motion's
        /// transform is `T(position) · R · S · T(-anchor)`, so the point the
        /// anchor names is exactly the matrix translation.
        [[nodiscard]] float anchorDepth(const Matrix4& world, const CameraPose& pose,
                                        Layer::Id id) {

            const float x = world.elements[12];
            const float y = world.elements[13];
            const float z = world.elements[14];
            if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z))
                throw std::invalid_argument("resolveCompositeStack: layer " +
                                            std::to_string(id) + " has a non-finite anchor");

            const float depth = viewDepth(Vector3(x, y, z), pose);
            if (!std::isfinite(depth))
                throw std::invalid_argument("resolveCompositeStack: layer " +
                                            std::to_string(id) + " resolves to a non-finite depth");
            return depth;
        }

        /// Far-to-near key. A layer at or behind the camera is painted before
        /// everything else, so it maps to +infinity rather than competing with
        /// the real depths.
        [[nodiscard]] float farness(float depth) {

            return depth > 1e-4f ? depth : std::numeric_limits<float>::infinity();
        }

    }// namespace

    std::string_view compositeModeName(CompositeMode mode) {

        switch (mode) {
            case CompositeMode::Stack: return "stack";
            case CompositeMode::Depth: return "depth";
        }
        return "stack";
    }

    std::optional<CompositeMode> parseCompositeMode(std::string_view name) {

        if (name == "stack") return CompositeMode::Stack;
        if (name == "depth") return CompositeMode::Depth;
        return std::nullopt;
    }

    CompositeStack resolveCompositeStack(const MotionScene& scene, const CameraPose& pose) {

        if (!scene.hasEvaluated())
            throw std::invalid_argument(
                "resolveCompositeStack: call evaluate() at the pose instant before resolving the stack");
        if (!std::isfinite(pose.position.x) || !std::isfinite(pose.position.y) ||
            !std::isfinite(pose.position.z) || !std::isfinite(pose.orientation.x) ||
            !std::isfinite(pose.orientation.y) || !std::isfinite(pose.orientation.z) ||
            !std::isfinite(pose.orientation.w))
            throw std::invalid_argument("resolveCompositeStack: camera pose is not finite");

        CompositeStack stack;
        stack.mode = scene.compositeMode();
        stack.backToFront.reserve(scene.layerCount());

        int stackIndex = 0;
        for (const Layer& layer : scene.layers()) {
            const int authoredIndex = stackIndex++;
            // A controller carries transforms for its children; it never draws.
            if (layer.isNull()) continue;
            if (!scene.state(layer.id).visible) continue;
            stack.backToFront.push_back(CompositeDraw{
                layer.id, authoredIndex,
                anchorDepth(scene.worldMatrix(layer.id), pose, layer.id)});
        }

        if (stack.mode == CompositeMode::Depth) {
            // Total order: far first; equal depth keeps the authored stack, and
            // the layer id is the final tie-break so the result never depends on
            // the sort algorithm's stability.
            std::sort(stack.backToFront.begin(), stack.backToFront.end(),
                      [](const CompositeDraw& a, const CompositeDraw& b) {
                          const float farA = farness(a.viewDepth);
                          const float farB = farness(b.viewDepth);
                          if (farA != farB) return farA > farB;
                          if (a.stackIndex != b.stackIndex) return a.stackIndex < b.stackIndex;
                          return a.layerId < b.layerId;
                      });
        }
        return stack;
    }

}// namespace chrononmotion::motion
