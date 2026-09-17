// ChrononMotion — 2.5D planes.
//
// A plane is the content-sized quad a layer lives on. Text and image planes are
// the *same type*: the only thing that varies between a text layer and an image
// layer is the ContentRef they carry, so position/anchor/scale/rotation/opacity
// and the parallax math cannot drift apart between the two.

#ifndef THREEPP_MOTION_MOTIONPLANE_HPP
#define THREEPP_MOTION_MOTIONPLANE_HPP

#include "threepp/motion/Content.hpp"
#include "threepp/motion/Layer.hpp"

#include "threepp/cameras/PerspectiveCamera.hpp"
#include "threepp/math/Matrix4.hpp"
#include "threepp/math/Vector2.hpp"
#include "threepp/math/Vector3.hpp"

#include <array>

namespace threepp::motion {

    /// A content-sized quad in 3D space, facing a perspective camera.
    struct MotionPlane {
        /// World-space size of the plane's box (motion units at scale 1).
        Vector2 size{1.f, 1.f};

        /// Normalized pivot inside the box: (0,0) bottom-left … (1,1) top-right.
        /// (0.5,0.5) is the center — the default for both text and image.
        Vector2 anchor{0.5f, 0.5f};

        /// Z depth of the plane. A camera on +Z looking down -Z therefore sees
        /// larger values as nearer.
        float depth{0.f};

        /// Build the plane for a content asset at a chosen world width, keeping
        /// its aspect ratio. Works identically for text and image content.
        [[nodiscard]] static MotionPlane fitted(const ContentRef& content, float width);

        /// Build the plane for a content asset with an explicit world size.
        [[nodiscard]] static MotionPlane sized(const ContentRef& content, const Vector2& size);

        /// The pivot in layer-local units, ready for `Transform::anchor`.
        [[nodiscard]] Vector3 anchorPoint() const;

        /// The four corners of the box in the layer's local space (z = 0).
        [[nodiscard]] std::array<Vector3, 4> corners() const;

        /// Write the plane's geometry onto a layer: anchor + depth. The layer's
        /// animated position/scale stay the authority for motion afterwards.
        Layer& applyTo(Layer& layer) const;
    };

    /// A plane projected into viewport pixels.
    struct ScreenPlane {
        Vector2 center{0.f, 0.f};     ///< viewport pixels, origin top-left
        Vector2 size{0.f, 0.f};       ///< projected bounding box, pixels
        float distance{0.f};          ///< distance in front of the camera
        float pixelsPerUnit{0.f};     ///< world units -> pixels at this depth
        bool visible{false};
    };

    /// A perspective camera plus a viewport: the window a plane is seen through.
    class PerspectiveView {
    public:
        PerspectiveView(PerspectiveCamera& camera, Vector2 viewport);

        [[nodiscard]] PerspectiveCamera& camera() const { return *camera_; }
        [[nodiscard]] const Vector2& viewport() const { return viewport_; }
        void setViewport(Vector2 viewport);

        /// Pixels per world unit at distance 1 in front of the camera.
        [[nodiscard]] float focalPixels() const;

        /// Pixels per world unit for something `distance` in front of the camera.
        [[nodiscard]] float pixelsPerUnit(float distance) const;

        /// Project a world point. Returns false when it is at or behind the near
        /// plane; `pixels` is a y-down viewport coordinate.
        bool project(const Vector3& world, Vector2& pixels, Vector3& ndc) const;

        /// Project a plane through this view, using the layer's world matrix.
        [[nodiscard]] ScreenPlane projectPlane(const MotionPlane& plane, const Matrix4& world) const;

        /// Automatic parallax: the screen shift of a plane at `distance` when the
        /// camera translates by `cameraDelta`. `pixelsPerUnit` shrinks with
        /// distance, so a far plane moves less than a near one.
        [[nodiscard]] Vector2 parallaxShift(const Vector3& cameraDelta, float distance) const;
        [[nodiscard]] Vector2 parallaxShift(const Vector3& cameraDelta, const ScreenPlane& plane) const;

    private:
        PerspectiveCamera* camera_;
        Vector2 viewport_;
    };

}// namespace threepp::motion

#endif//THREEPP_MOTION_MOTIONPLANE_HPP
