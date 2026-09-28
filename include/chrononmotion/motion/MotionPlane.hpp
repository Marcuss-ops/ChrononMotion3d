// ChrononMotion — 2.5D planes.
//
// A plane is the content-sized quad a layer lives on. Text and image planes are
// the *same type*: the only thing that varies between a text layer and an image
// layer is the ContentRef they carry, so position/anchor/scale/rotation/opacity
// and the parallax math cannot drift apart between the two.

#ifndef CHRONONMOTION_MOTION_MOTIONPLANE_HPP
#define CHRONONMOTION_MOTION_MOTIONPLANE_HPP

#include "chrononmotion/motion/Content.hpp"
#include "chrononmotion/motion/Layer.hpp"

#include "chrononmotion/cameras/PerspectiveCamera.hpp"
#include "chrononmotion/math/Matrix4.hpp"
#include "chrononmotion/math/Vector2.hpp"
#include "chrononmotion/math/Vector3.hpp"

#include <array>
#include <string>

namespace chrononmotion::motion {

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

        /// The measurement this plane's size and anchor were derived from: the
        /// fingerprint of the ContentRef handed to `fitted`/`sized`. Empty when
        /// the plane was built by hand, which is the one case where a
        /// re-measurement cannot be detected.
        std::string sourceFingerprint{};

        /// The world width a `fitted` plane was built at, or 0 when the size was
        /// given explicitly. Kept because re-deriving the aspect ratio needs the
        /// width the author asked for, not the size it produced.
        float fittedWidth{0.f};

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

        /// Whether this plane's size and anchor came from `content`'s measurement.
        [[nodiscard]] bool derivedFrom(const ContentRef& content) const;

        /// Re-derive the plane from a *re-measurement* of the same asset and
        /// write it back to the layer, then report whether anything changed.
        ///
        /// This is the closing half of "the anchor is set, not derived": a
        /// re-measured asset used to keep the pivot (and, for a fitted plane, the
        /// aspect ratio) of the previous measurement, because nothing bound the
        /// numbers to the content they measured. `remeasured` is Chronon's fresh
        /// measurement of the same asset, fingerprint included.
        ///
        /// Returns false — and touches nothing — when there is nothing to derive:
        /// a plane built by hand (no source fingerprint), a ref that carries no
        /// measurement, a `remeasured` that names a *different* asset (re-pointing
        /// a layer at another asset is a different operation), or an unchanged
        /// fingerprint.
        ///
        /// The layer's `anchor` and `scale` *tracks* still win over what this
        /// writes, exactly as they do over `applyTo`: a keyed channel overrides
        /// the static pose.
        [[nodiscard]] bool refresh(Layer& layer, const ContentRef& remeasured);
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

        /// World-space dimensions (width, height) of the camera frustum at `distance`
        /// in front of the camera, scaled by an optional `overscan` factor.
        /// Essential for sizing background quads and environment planes so they
        /// completely cover the camera view.
        [[nodiscard]] Vector2 frustumCoverageSize(float distance, float overscan = 1.0f) const;

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

}// namespace chrononmotion::motion

#endif//CHRONONMOTION_MOTION_MOTIONPLANE_HPP
