// ChrononMotion — presets that move a layer *along a trajectory*, and the
// hierarchy presets that key a group through its controller.
//
// A path preset does not key a curve: it samples the path and keys the sampled
// points, so the easing an author sees is segment-wise and the arc-length
// parameter is the path's own. A group preset keys the parent and the children
// through the same primitives, which is why a reveal cannot drift from the
// transform model — there is nothing here but tracks.

#include "Presets_detail.hpp"

#include <algorithm>

namespace chrononmotion::motion::presets {

    using detail::keyVector3;
    using detail::restOf;

    void arcMotion(Layer& layer, const Vector3& to, const Vector3& apex, int inFrame, int duration, float fps) {

        const Transform rest = restOf(layer, inFrame, fps);
        const MotionPath path = MotionPath::quadraticBezier(rest.position, apex, to);
        pathMotion(layer, path, inFrame, duration, fps, false, 16);
    }

    void arcEntrance(Layer& layer, const Vector3& to, const Vector3& apex,
                     int inFrame, int duration, float fps) {
        arcMotion(layer, to, apex, inFrame, duration, fps);
    }

    void pathMotion(Layer& layer, const MotionPath& path, int inFrame, int duration, float fps,
                    bool orient, unsigned int steps) {

        const float t0 = frameToTime(inFrame, fps);
        const float t1 = frameToTime(inFrame + duration, fps);
        const unsigned int segments = std::max(steps, 2u);

        for (unsigned int i = 0; i <= segments; ++i) {
            const float progress = static_cast<float>(i) / static_cast<float>(segments);
            const float time = t0 + (t1 - t0) * progress;

            layer.tracks.position.add(time, path.sampleUniform(progress), Easing::linear());

            if (orient) {
                layer.tracks.orientation.add(time, path.orientToPath(progress), Easing::linear());
            }
        }
    }

    void staggeredEntrance(const std::vector<Layer*>& layers, Direction direction,
                           int inFrame, int stagger, int duration, float distance, float fps) {

        for (std::size_t i = 0; i < layers.size(); ++i) {
            if (layers[i] == nullptr) continue;
            const int start = inFrame + static_cast<int>(i) * stagger;
            fadeSlide(*layers[i], direction, start, duration, distance, fps);
        }
    }

    void parentGroupReveal(Layer& parent, const std::vector<Layer*>& children,
                           int inFrame, int duration, float fps) {

        const Transform rest = restOf(parent, inFrame, fps);
        const float t0 = frameToTime(inFrame, fps);
        const float t1 = frameToTime(inFrame + duration, fps);

        const Vector3 folded(rest.scale.x * 0.85f, rest.scale.y * 0.85f, rest.scale.z * 0.85f);
        keyVector3(parent.tracks.scale, t0, t1, folded, rest.scale, Easing::overshoot());
        parent.tracks.opacity.add(t0, 0.f, Easing::easeOut());
        parent.tracks.opacity.add(t1, rest.opacity, Easing::linear());

        const int childWindow = std::max(1, duration / 2);
        const int childCount = static_cast<int>(children.size());

        for (int i = 0; i < childCount; ++i) {
            if (children[static_cast<std::size_t>(i)] == nullptr) continue;
            const int start = inFrame + (childWindow * i) / std::max(1, childCount);
            fadeIn(*children[static_cast<std::size_t>(i)], start, childWindow, fps);
        }
    }

}// namespace chrononmotion::motion::presets
