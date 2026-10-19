// ChrononMotion — engine-side interaction math (see Interaction.hpp).

#include "chrononmotion/motion/Interaction.hpp"

#include <cmath>
#include <stdexcept>

namespace chrononmotion::motion {
    namespace {

        void requireFinite(float value, const char* what) {
            if (!std::isfinite(value)) throw std::invalid_argument(what);
        }

        Vector3 worldAnchor(const Matrix4& world) {
            // Column-major translation, three.js convention.
            return Vector3(world.elements[12], world.elements[13], world.elements[14]);
        }

    }// namespace

    PickHit pickLayer(const MotionScene& scene, const ScreenCamera& camera,
                      const Vector2& pixel, float maxDistancePx) {

        requireFinite(pixel.x, "interaction: cursor x must be finite");
        requireFinite(pixel.y, "interaction: cursor y must be finite");
        requireFinite(maxDistancePx, "interaction: pick radius must be finite");
        if (maxDistancePx < 0.f) throw std::invalid_argument("interaction: pick radius must be non-negative");
        if (!scene.hasEvaluated())
            throw std::invalid_argument("interaction: pick on an unevaluated scene (evaluate() first)");

        PickHit best;
        for (const Layer& layer : scene.layers()) {
            const LayerState& state = scene.state(layer.id);
            if (!state.visible) continue;
            const Vector3 anchor = worldAnchor(state.world);
            const float depth = viewDepth(anchor, camera.pose);
            if (!(depth > 0.f)) continue;
            const Vector2 projected = projectToScreen(anchor, camera.pose, camera.intrinsics);
            const float dx = projected.x - pixel.x;
            const float dy = projected.y - pixel.y;
            const float distance = std::sqrt(dx * dx + dy * dy);
            if (!(distance <= maxDistancePx)) continue;
            const bool nearer = distance < best.screenDistancePx - 1e-6f;
            const bool tied = !nearer && std::fabs(distance - best.screenDistancePx) <= 1e-6f;
            const bool frontOnTie = tied && (!best.hit || depth < best.viewDepth - 1e-6f);
            const bool idOnTie = tied && best.hit && std::fabs(depth - best.viewDepth) <= 1e-6f && layer.id < best.id;
            if (!best.hit || nearer || frontOnTie || idOnTie) {
                best.hit = true;
                best.id = layer.id;
                best.screenDistancePx = distance;
                best.viewDepth = depth;
            }
        }
        return best;
    }

    Vector3 dragWorldDelta(const ScreenCamera& camera, float pointViewDepth,
                           const Vector2& screenDeltaPx) {

        requireFinite(pointViewDepth, "interaction: drag depth must be finite");
        requireFinite(screenDeltaPx.x, "interaction: drag dx must be finite");
        requireFinite(screenDeltaPx.y, "interaction: drag dy must be finite");
        if (!(pointViewDepth > 0.f))
            throw std::invalid_argument("interaction: drag depth must be positive (point is behind the camera)");
        const Vector2 viewport = camera.intrinsics.viewport;
        requireFinite(viewport.x, "interaction: viewport width must be finite");
        requireFinite(viewport.y, "interaction: viewport height must be finite");
        if (!(viewport.x > 0.f) || !(viewport.y > 0.f))
            throw std::invalid_argument("interaction: viewport must be positive");
        requireFinite(camera.intrinsics.fovDegrees, "interaction: FOV must be finite");

        // The camera-plane inverse of the pinhole projection: world units per
        // NDC at this depth, split across the (possibly non-square) viewport.
        // This is the point-motion dual of translatePoseForScreenDelta, which
        // moves the camera opposite to land a point; here the point itself
        // moves, so the sign stays with the cursor.
        const float halfFov = camera.intrinsics.fovDegrees * 0.5f * 3.14159265358979323846f / 180.f;
        const float tangent = std::tan(halfFov);
        if (!(tangent > 1e-6f)) throw std::invalid_argument("interaction: FOV must admit a projection");
        const float worldPerNdc = pointViewDepth / (1.f / tangent);
        const float aspect = viewport.x / viewport.y;
        const float ndcDX = (screenDeltaPx.x / viewport.x) * 2.f;
        const float ndcDY = -(screenDeltaPx.y / viewport.y) * 2.f;

        Quaternion q = camera.pose.orientation;
        if (q.lengthSq() > 0.f) q.normalize();
        Vector3 right(1.f, 0.f, 0.f);
        right.applyQuaternion(q);
        Vector3 up(0.f, 1.f, 0.f);
        up.applyQuaternion(q);

        Vector3 delta = right;
        delta.multiplyScalar(worldPerNdc * aspect * ndcDX);
        Vector3 lift = up;
        lift.multiplyScalar(worldPerNdc * ndcDY);
        delta.add(lift);
        return delta;
    }

    float dragViewRotation(const ScreenCamera&, const Vector2& pivotPx,
                           const Vector2& fromPx, const Vector2& toPx) {

        for (const float v : {pivotPx.x, pivotPx.y, fromPx.x, fromPx.y, toPx.x, toPx.y})
            requireFinite(v, "interaction: rotation drag points must be finite");
        const float fromX = fromPx.x - pivotPx.x;
        const float fromY = fromPx.y - pivotPx.y;
        const float toX = toPx.x - pivotPx.x;
        const float toY = toPx.y - pivotPx.y;
        if (fromX * fromX + fromY * fromY <= 1e-12f || toX * toX + toY * toY <= 1e-12f)
            throw std::invalid_argument("interaction: rotation drag is undefined at the pivot");
        // Screen Y runs down while the view-axis angle runs counter-clockwise
        // seen from the camera: negate so a clockwise cursor drag reads as a
        // clockwise (negative) rotation about the view axis.
        return -(std::atan2(toY, toX) - std::atan2(fromY, fromX));
    }

    float dragScaleFactor(const Vector2& pivotPx, const Vector2& fromPx, const Vector2& toPx) {

        for (const float v : {pivotPx.x, pivotPx.y, fromPx.x, fromPx.y, toPx.x, toPx.y})
            requireFinite(v, "interaction: scale drag points must be finite");
        const float fromX = fromPx.x - pivotPx.x;
        const float fromY = fromPx.y - pivotPx.y;
        const float toX = toPx.x - pivotPx.x;
        const float toY = toPx.y - pivotPx.y;
        const float fromR = std::sqrt(fromX * fromX + fromY * fromY);
        const float toR = std::sqrt(toX * toX + toY * toY);
        if (!(fromR > 1e-6f)) throw std::invalid_argument("interaction: scale drag starts at the pivot");
        const float factor = toR / fromR;
        requireFinite(factor, "interaction: scale factor is not finite");
        return factor;
    }

    ScrubSession::ScrubSession(MotionScene& scene) : scene_(&scene) {
        seek(0);
    }

    int ScrubSession::endFrame() const {
        return std::max(0, scene_->contentEndFrame());
    }

    float ScrubSession::seconds() const {
        return static_cast<float>(frame_) / scene_->fps();
    }

    void ScrubSession::seek(int frame) {
        if (frame < 0) throw std::invalid_argument("interaction: scrub frame must be non-negative");
        frame_ = std::min(frame, endFrame());
        scene_->evaluate(frame_);
    }

    void ScrubSession::step(int delta) {
        const int end = endFrame();
        int next = frame_ + delta;
        if (loop_) {
            const int span = loopOut_ - loopIn_ + 1;
            while (next > loopOut_) next -= span;
            while (next < loopIn_) next += span;
        } else {
            if (next < 0) next = 0;
            if (next > end) next = end;
        }
        frame_ = next;
        scene_->evaluate(frame_);
    }

    void ScrubSession::setLoop(int inFrame, int outFrame) {
        if (inFrame < 0 || outFrame <= inFrame)
            throw std::invalid_argument("interaction: loop needs 0 <= in < out");
        loop_ = true;
        loopIn_ = inFrame;
        loopOut_ = outFrame;
        if (frame_ < loopIn_ || frame_ > loopOut_) seek(loopIn_);
    }

    void ScrubSession::clearLoop() {
        loop_ = false;
    }

    void ScrubSession::play() {
        playing_ = true;
    }

    void ScrubSession::pause() {
        playing_ = false;
    }

    void ScrubSession::advance() {
        if (!playing_) return;
        const int next = frame_ + 1;
        if (loop_) {
            seek(next > loopOut_ ? loopIn_ : next);
            return;
        }
        if (next > endFrame()) {
            seek(endFrame());
            pause();
            return;
        }
        seek(next);
    }

}// namespace chrononmotion::motion
