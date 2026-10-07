// ChrononMotion — the layer.
//
// A layer is identity + lifetime + a content reference + a transform that is
// *sampled from tracks at a time* rather than mutated by a render loop. The same
// type carries a text asset, an image asset and a null/controller pivot; a Null
// layer is simply a layer with no content.

#ifndef CHRONONMOTION_MOTION_LAYER_HPP
#define CHRONONMOTION_MOTION_LAYER_HPP

#include "chrononmotion/core/BufferGeometry.hpp"
#include "chrononmotion/core/Object3D.hpp"
#include "chrononmotion/materials/Descriptors.hpp"
#include "chrononmotion/motion/Content.hpp"
#include "chrononmotion/motion/Track.hpp"
#include "chrononmotion/motion/Transform.hpp"
#include "chrononmotion/motion/Constraints.hpp"
#include "chrononmotion/motion/Procedural.hpp"
#include "chrononmotion/motion/Precomp.hpp"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace chrononmotion::motion {

    class MotionScene;

    /// The frame range a layer is alive for. `outFrame < 0` means open-ended, so a
    /// layer with no declared out point stays visible to the end of the timeline.
    struct FrameRange {
        int inFrame{0};
        int outFrame{-1};

        [[nodiscard]] static FrameRange all() { return FrameRange{0, -1}; }

        [[nodiscard]] static FrameRange between(int inFrame, int outFrame) {

            if (inFrame < 0 || outFrame < inFrame) {
                throw std::invalid_argument("FrameRange: expected 0 <= inFrame <= outFrame");
            }
            return FrameRange{inFrame, outFrame};
        }

        [[nodiscard]] bool openEnded() const { return outFrame < 0; }

        [[nodiscard]] bool contains(int frame) const {

            if (frame < inFrame) return false;
            return openEnded() || frame <= outFrame;
        }

        /// Length in frames, or -1 for an open-ended range.
        [[nodiscard]] int duration() const {

            return openEnded() ? -1 : std::max(0, outFrame - inFrame);
        }
    };

    /// Deterministic mapping from scene frames to a layer's local timeline.
    struct TimeRemap {
        float offsetFrames{0.f};
        float stretch{1.f};
        bool reverse{false};
        int loopStart{0};
        int loopEnd{-1};
        bool pingPong{false};
        bool freeze{false};

        [[nodiscard]] float mapFrame(float frame) const;
        [[nodiscard]] bool valid() const;
    };

    /// The per-property animation channels of a layer.
    ///
    /// Every channel is independently keyed: a layer can animate only `opacity`,
    /// or only `position`, and the un-keyed channels keep the layer's static
    /// pose. That is what lets one preset shape a whole transform and another
    /// touch a single axis.
    struct LayerTracks {
        Track<Vector3> position;
        Track<Vector3> anchor;
        Track<Vector3> scale;
        Track<Vector3> rotation;      ///< Rotation X/Y/Z, XYZ Euler, radians.
        Track<Quaternion> orientation;///< 3D orientation, sampled by shortest-path slerp.
        Track<float> opacity;

        [[nodiscard]] bool empty() const;
        [[nodiscard]] float startTime() const;
        [[nodiscard]] float endTime() const;
    };

    /// A timed handoff into another coordinate space. The first switch starts
    /// from the layer's authored parentId; each later switch selects a new one.
    struct SpaceSwitchPoint {
        float seconds{0.f};
        std::uint32_t parentId{0};
    };

    /// The renderer-facing half of a mesh layer.
    ///
    /// Chronon still owns the model's bytes; this side carries only the opaque
    /// asset identity plus the data-only geometry/material handles a compiler
    /// may hand to a renderer. Nothing here is decoded, measured or rasterized.
    struct MeshAttachment {
        /// Opaque identity of the Chronon-owned model asset.
        std::string assetId{};

        /// Optional in-tree geometry (a Box/Plane/Sphere builder) or explicitly
        /// attached glTF primitive. Inline RenderPlan lowering requires geometry;
        /// assetId-only attachments are intentionally not rendered by that adapter.
        std::shared_ptr<BufferGeometry> geometry{};

        MaterialDescriptor material{MaterialDescriptor::unlit(0)};

        [[nodiscard]] bool empty() const { return assetId.empty() && !geometry; }
    };

    /// A layer: identity, parent, content, lifetime and the animated transform.
    class Layer : public Object3D {
    public:
        using Id = std::uint32_t;

        /// "No parent" / "the scene root".
        static constexpr Id kRoot = 0;

        Id id{kRoot};
        Id parentId{kRoot};
        MotionScene* authoringOwner{nullptr};
        ContentRef content{};
        /// Present for a mesh layer: asset identity + renderer-facing handles.
        MeshAttachment mesh{};
        CompositionRef precomp{};
        FrameRange lifetime{};
        TimeRemap timeRemap{};
        /// Optional scene-seconds -> local-frame curve; when keyed, it replaces
        /// the static TimeRemap mapping for this layer.
        Track<float> timeRemapTrack{};
        /// False for a 2.5D plane layer. See `flat()`.
        bool is3D{true};
        bool castsShadow{false};
        bool acceptsShadow{true};
        float shadowOpacity{1.f};
        float shadowBlur{0.f};
        Transform transform{};
        LayerTracks tracks{};
        std::vector<ConstraintSpec> constraints{};
        std::vector<DriverSpec> drivers{};
        std::vector<SpaceSwitchPoint> spaceSwitches{};

        Layer() = default;
        Layer(Id id, std::string name);

        /// Copy the authoring state into a **fresh, detached** node.
        ///
        /// `Object3D` identity and graph links belong to the node instance, so a
        /// copy is a new node carrying the same motion — never a second parent
        /// link and never a shared matrix. That is what makes `addLayer(layer)`
        /// on an already-built layer safe.
        Layer(const Layer& source);
        Layer(Layer&& source) noexcept = default;

        /// Move the authoring state into *this* node, keeping this node's
        /// `Object3D` identity and graph position. This is how a scene absorbs a
        /// newly authored layer without moving graph nodes around.
        Layer& replaceAuthoring(Layer&& source);

        /// A null/controller layer owns no content: it exists to drive the layers
        /// parented under it (rotation rig, scale rig, camera rig).
        [[nodiscard]] bool isNull() const { return content.empty() && mesh.empty(); }
        [[nodiscard]] bool isContent() const { return !isNull(); }
        [[nodiscard]] bool isMesh() const { return !mesh.empty(); }

        [[nodiscard]] bool visibleAt(int frame) const { return lifetime.contains(frame); }
        [[nodiscard]] bool animated() const { return !tracks.empty(); }

        /// The pose at `seconds`. Channels with keys override the static pose.
        /// A pure function of (this, seconds).
        void sample(float seconds, Transform& out) const;
        [[nodiscard]] Transform sample(float seconds) const;

        /// Sampling with cross-layer link resolution. A channel carrying a
        /// `LinkModifier` cannot be sampled without one — that is the whole
        /// point of the modifier — so the two-argument form above is the
        /// standalone contract and this is the composed one.
        void sample(float seconds, Transform& out, const LinkResolver& resolve) const;
        [[nodiscard]] Transform sample(float seconds, const LinkResolver& resolve) const;

        /// The layer's own keyed motion at `seconds`: the exact derivative of
        /// this layer's position/rotation/scale/opacity tracks.
        ///
        /// Deliberately LOCAL. It does not see drivers, constraints, time remap,
        /// anchor composition or the parent chain, so for a parented or driven
        /// layer this is the timeline's contribution only. Use
        /// `MotionScene::motion()` when the world-space rate of the pixels is what
        /// is needed. The difference between the two is exactly the amount of
        /// motion the composition is adding on top of the keys.
        [[nodiscard]] LayerMotion keyedMotion(float seconds) const;

        /// The same derivative with links resolved. `resolveRates` must return
        /// the linked channel's **derivative**, exactly as `Track::velocity`.
        [[nodiscard]] LayerMotion keyedMotion(float seconds, const LinkResolver& resolveRates) const;

        /// The latest key represented by this layer, in seconds.
        [[nodiscard]] float endTime() const;
        [[nodiscard]] float mapFrame(float sceneFrame, float sceneSeconds,
                                     const LinkResolver& resolve = {}) const;

        // --- fluent authoring helpers, each returning *this
        Layer& named(std::string value);
        Layer& childOf(Id parent);
        Layer& positionedAt(const Vector3& value);
        Layer& anchoredAt(const Vector3& value);
        Layer& scaled(const Vector3& value);
        Layer& scaled(float uniform);
        Layer& rotated(const Vector3& eulerRadians);
        Layer& oriented(const Quaternion& value);
        Layer& opacityAt(float value);
        Layer& alive(FrameRange range);
        Layer& alive(int inFrame, int outFrame);
        Layer& contentOf(ContentRef value);
        Layer& withMesh(MeshAttachment value);

        /// Mark the layer as a 2.5D plane rather than a fully 3D object. A
        /// `MotionPlane` does this when it is applied: the layer is positioned in
        /// 3D, but the thing it draws is a flat quad facing the camera.
        Layer& flat();

        ~Layer() override = default;
    };

}// namespace chrononmotion::motion

#endif//CHRONONMOTION_MOTION_LAYER_HPP
