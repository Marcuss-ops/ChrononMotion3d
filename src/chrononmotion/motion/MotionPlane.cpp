#include "chrononmotion/motion/MotionPlane.hpp"

#include "chrononmotion/math/MathUtils.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace chrononmotion::motion {

    MotionPlane MotionPlane::fitted(const ContentRef& content, float width) {

        if (width <= 0.f || !std::isfinite(width)) {
            throw std::invalid_argument("MotionPlane::fitted: width must be finite and positive");
        }
        if (content.metrics.naturalSize.x <= 0.f || content.metrics.naturalSize.y <= 0.f ||
            !std::isfinite(content.metrics.naturalSize.x) || !std::isfinite(content.metrics.naturalSize.y)) {
            throw std::invalid_argument("MotionPlane::fitted: content natural size must be finite and positive");
        }

        MotionPlane plane;
        plane.anchor = content.metrics.anchor;
        plane.sourceFingerprint = content.metrics.fingerprint;
        plane.fittedWidth = width;

        const float naturalWidth = content.metrics.naturalSize.x;
        const float naturalHeight = content.metrics.naturalSize.y;

        if (naturalWidth > 0.f && naturalHeight > 0.f) {
            plane.size.set(width, width * (naturalHeight / naturalWidth));
        } else {
            plane.size.set(width, width);
        }

        return plane;
    }

    MotionPlane MotionPlane::sized(const ContentRef& content, const Vector2& size) {

        if (size.x <= 0.f || size.y <= 0.f || !std::isfinite(size.x) || !std::isfinite(size.y)) {
            throw std::invalid_argument("MotionPlane::sized: size must be finite and positive");
        }

        MotionPlane plane;
        plane.anchor = content.metrics.anchor;
        plane.sourceFingerprint = content.metrics.fingerprint;
        plane.size = size;
        return plane;
    }

    Vector3 MotionPlane::anchorPoint() const {

        return Vector3((anchor.x - 0.5f) * size.x,
                       (anchor.y - 0.5f) * size.y,
                       0.f);
    }

    std::array<Vector3, 4> MotionPlane::corners() const {

        const float halfWidth = size.x * 0.5f;
        const float halfHeight = size.y * 0.5f;

        return {
                Vector3(-halfWidth, -halfHeight, 0.f),
                Vector3(halfWidth, -halfHeight, 0.f),
                Vector3(halfWidth, halfHeight, 0.f),
                Vector3(-halfWidth, halfHeight, 0.f)};
    }

    Layer& MotionPlane::applyTo(Layer& layer) const {

        layer.transform.anchor = anchorPoint();
        layer.transform.position.z = depth;
        layer.flat();
        return layer;
    }

    bool MotionPlane::derivedFrom(const ContentRef& content) const {

        return !sourceFingerprint.empty() && sourceFingerprint == content.metrics.fingerprint;
    }

    bool MotionPlane::refresh(Layer& layer, const ContentRef& remeasured) {

        // A plane built by hand carries no source, and an un-fingerprinted ref
        // cannot say that anything changed.
        if (sourceFingerprint.empty() || !remeasured.metrics.measured()) return false;

        // A re-measurement is about the same asset. Pointing the layer at a
        // different asset replaces its content; it does not re-derive a pivot.
        if (remeasured != layer.content) return false;

        // Same measurement: nothing to derive, so nothing is written.
        if (sourceFingerprint == remeasured.metrics.fingerprint) return false;

        const float depth = this->depth;
        const Vector2 size = this->size;

        *this = fittedWidth > 0.f ? fitted(remeasured, fittedWidth)
                                  : sized(remeasured, size);
        this->depth = depth;

        layer.content = remeasured;
        applyTo(layer);
        return true;
    }

    PerspectiveView::PerspectiveView(PerspectiveCamera& camera, Vector2 viewport)
        : camera_(&camera), viewport_(viewport) {}

    void PerspectiveView::setViewport(Vector2 viewport) {

        viewport_ = viewport;
    }

    float PerspectiveView::focalPixels() const {

        const float halfFov = math::degToRad(camera_->getEffectiveFOV()) * 0.5f;
        const float tangent = std::tan(halfFov);

        return tangent > 1e-6f ? (viewport_.y * 0.5f) / tangent : 0.f;
    }

    float PerspectiveView::pixelsPerUnit(float distance) const {

        return distance > 1e-6f ? focalPixels() / distance : 0.f;
    }

    bool PerspectiveView::project(const Vector3& world, Vector2& pixels, Vector3& ndc) const {

        Vector3 cameraSpace = world;
        cameraSpace.applyMatrix4(camera_->matrixWorldInverse);

        const float distance = -cameraSpace.z;
        if (distance <= camera_->nearPlane) return false;

        ndc = world;
        ndc.project(*camera_);

        pixels.set((ndc.x * 0.5f + 0.5f) * viewport_.x,
                   (0.5f - ndc.y * 0.5f) * viewport_.y);

        return true;
    }

    ScreenPlane PerspectiveView::projectPlane(const MotionPlane& plane, const Matrix4& world) const {

        ScreenPlane result;

        Vector2 minPixels{std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
        Vector2 maxPixels{std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest()};

        for (const Vector3& corner : plane.corners()) {
            Vector3 worldCorner = corner;
            worldCorner.applyMatrix4(world);

            Vector2 pixels;
            Vector3 ndc;
            if (!project(worldCorner, pixels, ndc)) return result;

            minPixels.x = std::min(minPixels.x, pixels.x);
            minPixels.y = std::min(minPixels.y, pixels.y);
            maxPixels.x = std::max(maxPixels.x, pixels.x);
            maxPixels.y = std::max(maxPixels.y, pixels.y);
        }

        result.center.set((minPixels.x + maxPixels.x) * 0.5f,
                          (minPixels.y + maxPixels.y) * 0.5f);
        result.size.set(maxPixels.x - minPixels.x,
                        maxPixels.y - minPixels.y);

        Vector3 centerWorld{0.f, 0.f, 0.f};
        centerWorld.applyMatrix4(world);

        Vector3 centerCameraSpace = centerWorld;
        centerCameraSpace.applyMatrix4(camera_->matrixWorldInverse);

        result.distance = -centerCameraSpace.z;
        result.pixelsPerUnit = pixelsPerUnit(result.distance);
        result.visible = true;

        return result;
    }

    Vector2 PerspectiveView::parallaxShift(const Vector3& cameraDelta, float distance) const {

        const float scale = pixelsPerUnit(distance);

        // The camera moving right slides the plane left; the camera moving up
        // slides the plane down in a y-down pixel space. `scale` shrinks with
        // distance, which is the automatic parallax.
        return Vector2(-cameraDelta.x * scale, cameraDelta.y * scale);
    }

    Vector2 PerspectiveView::parallaxShift(const Vector3& cameraDelta, const ScreenPlane& plane) const {

        return plane.visible ? parallaxShift(cameraDelta, plane.distance) : Vector2(0.f, 0.f);
    }

}// namespace chrononmotion::motion
