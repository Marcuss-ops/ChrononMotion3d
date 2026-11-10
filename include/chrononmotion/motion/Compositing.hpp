// ChrononMotion — the compositing order.
//
// Space and time belong here; pixels do not. What this file owns is the one
// editorial decision a compositor must not make for itself: in which order the
// visible layers are painted, back to front.
//
// Two modes, both deterministic:
//
//   Stack  the authored order (layer 0 first, later layers in front). This is
//          the After Effects 2D layer stack, and it is the default so every
//          plan emitted before this contract existed keeps its exact bytes.
//
//   Depth  the drawable layers are sorted by the camera-space depth of their
//          anchor, far to near. This is the After Effects 3D composition rule
//          that makes "the object in front, the text behind it" a fact of the
//          scene instead of a fact of the order the layers were inserted in.
//
// The sort is per layer, on the anchor point — the same granularity AE uses for
// 3D layer ordering. Two intersecting quads are still resolved per pixel by the
// renderer; that is a different contract and is not claimed here.

#ifndef CHRONONMOTION_MOTION_COMPOSITING_HPP
#define CHRONONMOTION_MOTION_COMPOSITING_HPP

#include "chrononmotion/motion/Layer.hpp"

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace chrononmotion::motion {

    class MotionScene;
    struct CameraPose;

    /// How a scene orders its visible layers for compositing.
    enum class CompositeMode : std::uint8_t {
        Stack, ///< Authored order, back to front (After Effects 2D stack).
        Depth  ///< Camera-space depth of the anchor, far to near (AE 3D comp).
    };

    [[nodiscard]] std::string_view compositeModeName(CompositeMode mode);

    /// Strict parser for the scene file vocabulary. Only the names this contract
    /// emits are accepted; anything else returns nullopt and the caller rejects.
    [[nodiscard]] std::optional<CompositeMode> parseCompositeMode(std::string_view name);

    /// One paintable layer in the resolved stack.
    struct CompositeDraw {
        Layer::Id layerId{0};
        /// Position in the authored layer list. The stable tie-break: at equal
        /// depth the later layer stays in front, exactly as it does in Stack
        /// mode.
        int stackIndex{0};
        /// Camera-space depth of the layer anchor. Positive is in front of the
        /// camera; zero or negative means at/behind the camera.
        float viewDepth{0.f};
    };

    /// A resolved, back-to-front draw list. Null controllers and layers outside
    /// their lifetime are absent: they place or hide things, they never paint.
    struct CompositeStack {
        CompositeMode mode{CompositeMode::Stack};
        std::vector<CompositeDraw> backToFront{};

        [[nodiscard]] bool empty() const { return backToFront.empty(); }
        [[nodiscard]] std::size_t size() const { return backToFront.size(); }
    };

    /// Resolve the stack from the scene's **already evaluated** state and a
    /// resolved world camera pose.
    ///
    /// Precondition: `scene.evaluate(...)` has run at the instant the pose was
    /// sampled. Reading `worldMatrix()` without an evaluation would silently
    /// resolve the previous frame, so that case is rejected instead.
    ///
    /// In `Depth` mode a layer at or behind the camera sorts behind everything
    /// else (it paints first and is then covered); a non-finite anchor or pose
    /// is rejected, never skipped.
    [[nodiscard]] CompositeStack resolveCompositeStack(const MotionScene& scene,
                                                       const CameraPose& pose);

}// namespace chrononmotion::motion

#endif//CHRONONMOTION_MOTION_COMPOSITING_HPP
