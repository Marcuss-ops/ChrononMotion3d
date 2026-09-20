// ChrononMotion — the layer.
//
// A layer is identity + lifetime + a content reference + a transform that is
// *sampled from tracks at a time* rather than mutated by a render loop. The same
// type carries a text asset, an image asset and a null/controller pivot; a Null
// layer is simply a layer with no content.

#ifndef CHRONONMOTION_MOTION_LAYER_HPP
#define CHRONONMOTION_MOTION_LAYER_HPP

#include "chrononmotion/motion/Content.hpp"
#include "chrononmotion/motion/Track.hpp"
#include "chrononmotion/motion/Transform.hpp"
#include "chrononmotion/motion/Constraints.hpp"
#include "chrononmotion/motion/Procedural.hpp"
#include "chrononmotion/motion/Precomp.hpp"

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace chrononmotion::motion {

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

    /// A layer: identity, parent, content, lifetime and the animated transform.
    class Layer {
    public:
        using Id = std::uint32_t;

        /// "No parent" / "the scene root".
        static constexpr Id kRoot = 0;

        Id id{kRoot};
        std::string name{};
        Id parentId{kRoot};
        ContentRef content{};
        CompositionRef precomp{};
        FrameRange lifetime{};
        TimeRemap timeRemap{};
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

        Layer() = default;
        Layer(Id id, std::string name);

        /// A null/controller layer owns no content: it exists to drive the layers
        /// parented under it (rotation rig, scale rig, camera rig).
        [[nodiscard]] bool isNull() const { return content.empty(); }
        [[nodiscard]] bool isContent() const { return !content.empty(); }

        [[nodiscard]] bool visibleAt(int frame) const { return lifetime.contains(frame); }
        [[nodiscard]] bool animated() const { return !tracks.empty(); }

        /// The pose at `seconds`. Channels with keys override the static pose.
        /// A pure function of (this, seconds).
        void sample(float seconds, Transform& out) const;
        [[nodiscard]] Transform sample(float seconds) const;

        /// The latest key represented by this layer, in seconds.
        [[nodiscard]] float endTime() const;

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

        /// Mark the layer as a 2.5D plane rather than a fully 3D object. A
        /// `MotionPlane` does this when it is applied: the layer is positioned in
        /// 3D, but the thing it draws is a flat quad facing the camera.
        Layer& flat();
    };

}// namespace chrononmotion::motion

#endif//CHRONONMOTION_MOTION_LAYER_HPP
