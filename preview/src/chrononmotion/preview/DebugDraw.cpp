#include "chrononmotion/preview/DebugDraw.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>

namespace chrononmotion::preview {
    using namespace chrononmotion::motion;

    namespace {

        constexpr float kPi = 3.14159265358979323846f;

        std::array<std::uint8_t, 5> glyphRows(char input) {
            const char c = static_cast<char>(std::toupper(static_cast<unsigned char>(input)));
            switch (c) {
                case 'A': return {0b010, 0b101, 0b111, 0b101, 0b101};
                case 'B': return {0b110, 0b101, 0b110, 0b101, 0b110};
                case 'C': return {0b011, 0b100, 0b100, 0b100, 0b011};
                case 'D': return {0b110, 0b101, 0b101, 0b101, 0b110};
                case 'E': return {0b111, 0b100, 0b110, 0b100, 0b111};
                case 'F': return {0b111, 0b100, 0b110, 0b100, 0b100};
                case 'G': return {0b011, 0b100, 0b101, 0b101, 0b011};
                case 'H': return {0b101, 0b101, 0b111, 0b101, 0b101};
                case 'I': return {0b111, 0b010, 0b010, 0b010, 0b111};
                case 'J': return {0b001, 0b001, 0b001, 0b101, 0b010};
                case 'K': return {0b101, 0b101, 0b110, 0b101, 0b101};
                case 'L': return {0b100, 0b100, 0b100, 0b100, 0b111};
                case 'M': return {0b101, 0b111, 0b111, 0b101, 0b101};
                case 'N': return {0b101, 0b111, 0b111, 0b111, 0b101};
                case 'O': return {0b010, 0b101, 0b101, 0b101, 0b010};
                case 'P': return {0b110, 0b101, 0b110, 0b100, 0b100};
                case 'Q': return {0b010, 0b101, 0b101, 0b111, 0b011};
                case 'R': return {0b110, 0b101, 0b110, 0b101, 0b101};
                case 'S': return {0b011, 0b100, 0b010, 0b001, 0b110};
                case 'T': return {0b111, 0b010, 0b010, 0b010, 0b010};
                case 'U': return {0b101, 0b101, 0b101, 0b101, 0b111};
                case 'V': return {0b101, 0b101, 0b101, 0b101, 0b010};
                case 'W': return {0b101, 0b101, 0b111, 0b111, 0b101};
                case 'X': return {0b101, 0b101, 0b010, 0b101, 0b101};
                case 'Y': return {0b101, 0b101, 0b010, 0b010, 0b010};
                case 'Z': return {0b111, 0b001, 0b010, 0b100, 0b111};
                case '0': return {0b111, 0b101, 0b101, 0b101, 0b111};
                case '1': return {0b010, 0b110, 0b010, 0b010, 0b111};
                case '2': return {0b110, 0b001, 0b010, 0b100, 0b111};
                case '3': return {0b110, 0b001, 0b010, 0b001, 0b110};
                case '4': return {0b101, 0b101, 0b111, 0b001, 0b001};
                case '5': return {0b111, 0b100, 0b110, 0b001, 0b110};
                case '6': return {0b011, 0b100, 0b110, 0b101, 0b010};
                case '7': return {0b111, 0b001, 0b010, 0b010, 0b010};
                case '8': return {0b010, 0b101, 0b010, 0b101, 0b010};
                case '9': return {0b010, 0b101, 0b011, 0b001, 0b110};
                case '.': return {0b000, 0b000, 0b000, 0b000, 0b010};
                case ':': return {0b000, 0b010, 0b000, 0b010, 0b000};
                case '-': return {0b000, 0b000, 0b111, 0b000, 0b000};
                case '/': return {0b001, 0b001, 0b010, 0b100, 0b100};
                case '%': return {0b101, 0b001, 0b010, 0b100, 0b101};
                case '+': return {0b000, 0b010, 0b111, 0b010, 0b000};
                case '=': return {0b000, 0b111, 0b000, 0b111, 0b000};
                case '_': return {0b000, 0b000, 0b000, 0b000, 0b111};
                case ',': return {0b000, 0b000, 0b000, 0b010, 0b100};
                case '!': return {0b010, 0b010, 0b010, 0b000, 0b010};
                case '?': return {0b110, 0b001, 0b010, 0b000, 0b010};
                case '(': return {0b001, 0b010, 0b010, 0b010, 0b001};
                case ')': return {0b100, 0b010, 0b010, 0b010, 0b100};
                default: return {0b000, 0b000, 0b000, 0b000, 0b000};
            }
        }

        Vector3 translation(const Matrix4& matrix) {
            return Vector3{matrix.elements[12], matrix.elements[13], matrix.elements[14]};
        }

        Vector3 normalizedOr(const Vector3& value, const Vector3& fallback) {
            const float length = std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
            if (length < 1e-8f) return fallback;
            return Vector3{value.x / length, value.y / length, value.z / length};
        }

        std::pair<Vector3, Vector3> basisFor(const Vector3& forward) {
            const Vector3 f = normalizedOr(forward, {0.f, 0.f, -1.f});
            const Vector3 up = std::fabs(f.y) > 0.95f ? Vector3{1.f, 0.f, 0.f} : Vector3{0.f, 1.f, 0.f};
            Vector3 right{f.y * up.z - f.z * up.y,
                          f.z * up.x - f.x * up.z,
                          f.x * up.y - f.y * up.x};
            right = normalizedOr(right, {1.f, 0.f, 0.f});
            const Vector3 realUp{f.y * right.z - f.z * right.y,
                                 f.z * right.x - f.x * right.z,
                                 f.x * right.y - f.y * right.x};
            return {right, normalizedOr(realUp, {0.f, 1.f, 0.f})};
        }

    }// namespace

    void DebugDraw::clear() {
        vertices_.clear();
    }

    void DebugDraw::addLine(const Vector3& a, const Vector3& b, const Color& color, float alpha) {
        vertices_.push_back(DebugVertex{a, color, alpha});
        vertices_.push_back(DebugVertex{b, color, alpha});
    }

    void DebugDraw::line(const Vector3& a, const Vector3& b, const Color& color, float alpha) {
        addLine(a, b, color, alpha);
    }

    void DebugDraw::polyline(std::span<const Vector3> points, const Color& color,
                             bool closed, float alpha) {
        if (points.size() < 2) return;
        for (std::size_t i = 1; i < points.size(); ++i) {
            addLine(points[i - 1], points[i], color, alpha);
        }
        if (closed) addLine(points.back(), points.front(), color, alpha);
    }

    void DebugDraw::box(const Box3& bounds, const Color& color, float alpha) {
        if (bounds.isEmpty()) return;
        box(bounds.min(), bounds.max(), color, alpha);
    }

    void DebugDraw::box(const Vector3& min, const Vector3& max, const Color& color, float alpha) {
        const std::array<Vector3, 8> corners{
                Vector3{min.x, min.y, min.z}, Vector3{max.x, min.y, min.z},
                Vector3{max.x, max.y, min.z}, Vector3{min.x, max.y, min.z},
                Vector3{min.x, min.y, max.z}, Vector3{max.x, min.y, max.z},
                Vector3{max.x, max.y, max.z}, Vector3{min.x, max.y, max.z}};
        addCornerEdges(corners, color, alpha);
    }

    void DebugDraw::addCornerEdges(const std::array<Vector3, 8>& c, const Color& color, float alpha) {
        // Near face, far face, then the four connecting edges.
        for (int i = 0; i < 4; ++i) {
            addLine(c[static_cast<std::size_t>(i)], c[static_cast<std::size_t>((i + 1) % 4)], color, alpha);
            addLine(c[static_cast<std::size_t>(4 + i)], c[static_cast<std::size_t>(4 + (i + 1) % 4)], color, alpha);
            addLine(c[static_cast<std::size_t>(i)], c[static_cast<std::size_t>(4 + i)], color, alpha);
        }
    }

    void DebugDraw::axes(const Vector3& origin, float length,
                         const Color& xColor, const Color& yColor, const Color& zColor) {
        line(origin, origin + Vector3{length, 0.f, 0.f}, xColor);
        line(origin, origin + Vector3{0.f, length, 0.f}, yColor);
        line(origin, origin + Vector3{0.f, 0.f, length}, zColor);
    }

    void DebugDraw::grid(int halfLines, float spacing, const Color& color, float alpha) {
        if (halfLines < 1 || !(spacing > 0.f)) return;
        const float extent = static_cast<float>(halfLines) * spacing;
        for (int i = -halfLines; i <= halfLines; ++i) {
            const float offset = static_cast<float>(i) * spacing;
            line({-extent, 0.f, offset}, {extent, 0.f, offset}, color, alpha);
            line({offset, 0.f, -extent}, {offset, 0.f, extent}, color, alpha);
        }
    }

    void DebugDraw::frustum(const std::array<Vector3, 8>& corners, const Color& color) {
        addCornerEdges(corners, color);
    }

    void DebugDraw::frustum(const PerspectiveCamera& camera, const Color& color) {
        const float tanHalf = std::tan(camera.fov * 0.5f * kPi / 180.f);
        const float aspect = camera.aspect;
        const Matrix4 world = *camera.matrixWorld;
        auto corner = [&](float sx, float sy, float depth) {
            Vector3 point{sx * tanHalf * depth * aspect, sy * tanHalf * depth, -depth};
            point.applyMatrix4(world);
            return point;
        };
        const std::array<Vector3, 8> corners{
                corner(-1.f, -1.f, camera.nearPlane), corner(1.f, -1.f, camera.nearPlane),
                corner(1.f, 1.f, camera.nearPlane), corner(-1.f, 1.f, camera.nearPlane),
                corner(-1.f, -1.f, camera.farPlane), corner(1.f, -1.f, camera.farPlane),
                corner(1.f, 1.f, camera.farPlane), corner(-1.f, 1.f, camera.farPlane)};
        frustum(corners, color);
    }

    void DebugDraw::cone(const Vector3& apex, const Vector3& direction, float height, float radius,
                         int segments, const Color& color) {
        if (segments < 3 || !(height > 0.f) || !(radius >= 0.f)) return;
        const Vector3 forward = normalizedOr(direction, {0.f, 0.f, -1.f});
        const auto [right, up] = basisFor(forward);
        const Vector3 base = apex + forward * height;
        std::vector<Vector3> rim;
        rim.reserve(static_cast<std::size_t>(segments));
        for (int i = 0; i < segments; ++i) {
            const float angle = 2.f * kPi * static_cast<float>(i) / static_cast<float>(segments);
            const Vector3 point = base + right * (std::cos(angle) * radius) + up * (std::sin(angle) * radius);
            rim.push_back(point);
            line(apex, point, color);
        }
        for (int i = 0; i < segments; ++i) {
            line(rim[static_cast<std::size_t>(i)],
                 rim[static_cast<std::size_t>((i + 1) % segments)], color);
        }
    }

    void DebugDraw::sphere(const Vector3& center, float radius, int segments, const Color& color) {
        if (segments < 3 || !(radius > 0.f)) return;
        std::vector<Vector3> circle;
        circle.reserve(static_cast<std::size_t>(segments));
        for (int axis = 0; axis < 3; ++axis) {
            circle.clear();
            for (int i = 0; i < segments; ++i) {
                const float angle = 2.f * kPi * static_cast<float>(i) / static_cast<float>(segments);
                const float a = std::cos(angle) * radius;
                const float b = std::sin(angle) * radius;
                if (axis == 0) circle.push_back(center + Vector3{0.f, a, b});
                else if (axis == 1) circle.push_back(center + Vector3{a, 0.f, b});
                else circle.push_back(center + Vector3{a, b, 0.f});
            }
            polyline(circle, color, true);
        }
    }

    void DebugDraw::cross(const Vector3& center, float size, const Color& color) {
        line(center + Vector3{-size, 0.f, 0.f}, center + Vector3{size, 0.f, 0.f}, color);
        line(center + Vector3{0.f, -size, 0.f}, center + Vector3{0.f, size, 0.f}, color);
        line(center + Vector3{0.f, 0.f, -size}, center + Vector3{0.f, 0.f, size}, color);
    }

    void DebugDraw::rectFilled(const Vector3& min, const Vector3& max, const Color& color,
                               float alpha, int rows) {
        if (rows < 1) return;
        for (int i = 0; i < rows; ++i) {
            const float t = (static_cast<float>(i) + 0.5f) / static_cast<float>(rows);
            const float y = min.y + (max.y - min.y) * t;
            line({min.x, y, min.z}, {max.x, y, max.z}, color, alpha);
        }
    }

    void DebugDraw::text(const Vector3& origin, std::string_view value, const Color& color,
                         float scale, float alpha) {
        if (scale <= 0.f) return;
        const float advance = 4.f * scale;
        const float lineHeight = 7.f * scale;
        float x = origin.x;
        float y = origin.y;
        for (char c : value) {
            if (c == '\n') {
                x = origin.x;
                y -= lineHeight;
                continue;
            }
            const auto rows = glyphRows(c);
            for (int row = 0; row < 5; ++row) {
                const std::uint8_t bits = rows[static_cast<std::size_t>(row)];
                if (bits == 0) continue;
                int first = -1;
                int last = -1;
                for (int column = 0; column < 3; ++column) {
                    if ((bits & (1u << (2 - column))) != 0) {
                        if (first < 0) first = column;
                        last = column;
                    }
                }
                if (first >= 0) {
                    const float rowY = y - static_cast<float>(row) * scale;
                    line({x + static_cast<float>(first) * scale, rowY, origin.z},
                         {x + static_cast<float>(last + 1) * scale, rowY, origin.z},
                         color, alpha);
                }
            }
            x += advance;
        }
    }

    Box3 DebugDraw::bounds() const {
        Box3 result;
        result.makeEmpty();
        for (const auto& vertex : vertices_) result.expandByPoint(vertex.position);
        return result;
    }

    void DebugDraw::cameraRig(const CameraRig& rig, float seconds, const Color& color,
                              const Matrix4* parentWorld) {
        auto camera = rig.makeCamera();
        rig.apply(seconds, *camera, parentWorld);
        camera->updateProjectionMatrix();
        camera->updateMatrixWorld(true);
        frustum(*camera, color);

        if (rig.hasTarget()) {
            CameraPose pose = rig.sample(seconds);
            Vector3 target = pose.target;
            if (parentWorld) target.applyMatrix4(*parentWorld);
            line(translation(*camera->matrixWorld), target, color, 0.8f);
            cross(target, 0.05f, color);
        }
        cross(translation(*camera->matrixWorld), 0.08f, color);
    }

    void DebugDraw::lightRig(const LightRig& rig, float seconds, const Color& color) {
        const auto samples = rig.sample(seconds);
        for (const LightSample& sample : samples) {
            // LightRig::sample already resolves source.local through the rig
            // matrix and the direction through both local and rig rotations.
            const Vector3 position = sample.local.position;
            const Vector3 direction = sample.direction;
            cross(position, 0.08f, color);
            if (sample.kind == LightKind::Spot) {
                const float height = sample.range > 0.f ? std::min(sample.range, 5.f) : 2.f;
                const float radius = std::tan(std::max(sample.angle, 0.01f)) * height;
                cone(position, direction, height, radius, 16, color);
            } else if (sample.kind == LightKind::Point) {
                sphere(position, 0.15f, 12, color);
            } else if (sample.kind == LightKind::Directional) {
                line(position, position + normalizedOr(direction, {0.f, 0.f, -1.f}) * 1.5f, color);
            }
        }
    }

    void DebugDraw::rigGraph(const RigGraph& graph, const Color& color,
                             std::span<const RigPoseValue> pose) {
        const RigEvaluator evaluator = graph.compile();
        const RigEvaluation evaluation = evaluator.evaluate(pose);
        auto indexOf = [&](RigNodeId id) -> std::optional<std::size_t> {
            const auto it = std::find(evaluation.ids.begin(), evaluation.ids.end(), id);
            if (it == evaluation.ids.end()) return std::nullopt;
            return static_cast<std::size_t>(std::distance(evaluation.ids.begin(), it));
        };
        for (std::size_t i = 0; i < evaluation.ids.size(); ++i) {
            const Vector3 position = translation(evaluation.world[i]);
            cross(position, 0.03f, color);
            const auto nodeIt = std::find_if(graph.nodes.begin(), graph.nodes.end(),
                                             [id = evaluation.ids[i]](const RigNode& node) {
                                                 return node.id == id;
                                             });
            if (nodeIt == graph.nodes.end() || nodeIt->parent == 0) continue;
            const auto parentIndex = indexOf(nodeIt->parent);
            if (!parentIndex) continue;
            line(position, translation(evaluation.world[*parentIndex]), color);
        }
    }

    void DebugDraw::motionScene(MotionScene& scene, int frame, const Color& color,
                                bool drawPaths) {
        scene.evaluate(frame);
        const auto& layers = scene.layers();
        for (const Layer& layer : layers) {
            if (!scene.visible(layer.id)) continue;
            const Vector3 position = translation(scene.worldMatrix(layer.id));
            cross(position, 0.04f, color);
            if (layer.parentId != Layer::kRoot && scene.hasLayer(layer.parentId)) {
                line(position, translation(scene.worldMatrix(layer.parentId)), color, 0.7f);
            }
            if (!drawPaths || layer.tracks.position.empty()) continue;
            const Track<Vector3>& track = layer.tracks.position;
            const float start = track.startTime();
            const float end = track.endTime();
            if (!(end > start)) continue;
            Matrix4 parent = Matrix4{};
            if (layer.parentId != Layer::kRoot && scene.hasLayer(layer.parentId))
                parent = scene.worldMatrix(layer.parentId);
            std::vector<Vector3> points;
            constexpr int divisions = 32;
            points.reserve(divisions + 1);
            for (int i = 0; i <= divisions; ++i) {
                const float t = start + (end - start) * static_cast<float>(i) /
                                               static_cast<float>(divisions);
                Vector3 point = track.sample(t);
                point.applyMatrix4(parent);
                points.push_back(point);
            }
            polyline(points, color, false, 0.8f);
        }
    }

}// namespace chrononmotion::preview
