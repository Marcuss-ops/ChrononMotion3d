// ChrononMotion preview — renderer-neutral debug drawing.
//
// Bevy's `Gizmos` idea, reduced to a command buffer the preview layer can test
// without a GL context. `DebugDraw` owns no GL objects and knows no camera
// uniform: a renderer consumes its line list, and the helpers below turn the
// motion model (camera rig, light rig, rig graph, scene layers) into lines.

#ifndef CHRONONMOTION_PREVIEW_DEBUGDRAW_HPP
#define CHRONONMOTION_PREVIEW_DEBUGDRAW_HPP

#include "chrononmotion/math/Box3.hpp"
#include "chrononmotion/math/Color.hpp"
#include "chrononmotion/math/Matrix4.hpp"
#include "chrononmotion/math/Vector3.hpp"

#include "chrononmotion/cameras/PerspectiveCamera.hpp"
#include "chrononmotion/motion/CameraRig.hpp"
#include "chrononmotion/motion/LightRig.hpp"
#include "chrononmotion/motion/MotionPath.hpp"
#include "chrononmotion/motion/MotionScene.hpp"
#include "chrononmotion/motion/RigGraph.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace chrononmotion::preview {

    struct DebugVertex {
        Vector3 position{};
        Color color{};
        float alpha{1.f};
    };

    class DebugDraw {
    public:
        void clear();
        [[nodiscard]] bool empty() const { return vertices_.empty(); }
        [[nodiscard]] std::size_t lineCount() const { return vertices_.size() / 2; }
        [[nodiscard]] const std::vector<DebugVertex>& vertices() const { return vertices_; }
        [[nodiscard]] Box3 bounds() const;

        void line(const Vector3& a, const Vector3& b, const Color& color, float alpha = 1.f);
        void polyline(std::span<const Vector3> points, const Color& color,
                      bool closed = false, float alpha = 1.f);
        void box(const Box3& box, const Color& color, float alpha = 1.f);
        void box(const Vector3& min, const Vector3& max, const Color& color, float alpha = 1.f);
        void axes(const Vector3& origin, float length,
                  const Color& xColor, const Color& yColor, const Color& zColor);
        void grid(int halfLines, float spacing, const Color& color, float alpha = 0.35f);
        void frustum(const std::array<Vector3, 8>& corners, const Color& color);
        void frustum(const PerspectiveCamera& camera, const Color& color);
        void cone(const Vector3& apex, const Vector3& direction, float height, float radius,
                  int segments, const Color& color);
        void sphere(const Vector3& center, float radius, int segments, const Color& color);
        void cross(const Vector3& center, float size, const Color& color);
        void rectFilled(const Vector3& min, const Vector3& max, const Color& color,
                        float alpha = 0.6f, int rows = 24);

        /// Tiny 3x5 bitmap text rendered as line segments. Coordinates are in
        /// the same space as the other primitives: world units for a world
        /// overlay, NDC for the diagnostics overlay.
        void text(const Vector3& origin, std::string_view text, const Color& color,
                  float scale = 0.02f, float alpha = 1.f);

        // --- motion-model helpers
        void cameraRig(const motion::CameraRig& rig, float seconds, const Color& color,
                       const Matrix4* parentWorld = nullptr);
        void lightRig(const motion::LightRig& rig, float seconds, const Color& color);
        void rigGraph(const motion::RigGraph& graph, const Color& color,
                      std::span<const motion::RigPoseValue> pose = {});
        void motionScene(motion::MotionScene& scene, int frame, const Color& color,
                         bool drawPaths = true);

    private:
        void addLine(const Vector3& a, const Vector3& b, const Color& color, float alpha);
        void addCornerEdges(const std::array<Vector3, 8>& corners, const Color& color,
                            float alpha = 1.f);

        std::vector<DebugVertex> vertices_{};
    };

}// namespace chrononmotion::preview

#endif//CHRONONMOTION_PREVIEW_DEBUGDRAW_HPP
