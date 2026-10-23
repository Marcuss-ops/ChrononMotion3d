// ChrononMotion — shared declarations for motion tracks.
#ifndef CHRONONMOTION_MOTION_TRACK_TYPES_HPP
#define CHRONONMOTION_MOTION_TRACK_TYPES_HPP

#include "chrononmotion/motion/Easing.hpp"
#include "chrononmotion/math/Quaternion.hpp"
#include "chrononmotion/math/Vector3.hpp"
#include "chrononmotion/math/Color.hpp"

#include <cstdint>
#include <functional>
#include <initializer_list>
#include <variant>
#include <vector>

namespace chrononmotion::motion {


    /// First-order motion of a layer, in the channel's own units per second.
    ///
    /// Two ways to get it, and they answer different questions:
    ///
    /// - A finite difference is what a compositor wants: the velocity of the layer
    ///   that actually rendered, including constraints, drivers, time remap and
    ///   the parent chain. It is exact for the instants it was sampled at, and
    ///   costs extra scene evaluations.
    /// - An analytic derivative is what chaining and sync want: the exact
    ///   derivative of a keyed track at a continuous instant, with no sampling
    ///   error and no extra evaluation. It sees only the layer's own keys.
    ///
    /// The gap between the two is the motion that comes from something other than
    /// keys. Where they disagree, the composition is driving the layer, not its
    /// timeline. `finiteDifference` records which one produced a given value.
    ///
    /// Declared next to `Track` rather than next to `MotionScene` so `Layer` can
    /// name it without an include cycle.
    struct LayerMotion {
        Vector3 positionVelocity{};   ///< d(position)/dt
        Vector3 linearVelocity{};     ///< same value, for consumers that read "linear" as a convention
        Vector3 angularVelocity{};    ///< d(orientation)/dt, rad/s about each axis
        float opacityVelocity{0.f};   ///< d(opacity)/dt, per second
        Vector3 acceleration{};       ///< finite difference only; d2(position)/dt2
        bool finiteDifference{false}; ///< false when derived from keys alone
    };

    /// A keyframe: a value at a time, plus the curve used to reach the next key.
    /// An aggregate on purpose — a track can be written as a literal list.
    template<class T>
    struct Keyframe {
        float time{0.f};
        T value{};
        Easing easing{};
        bool discrete{false};
    };

    /// Every keyed channel a layer owns. A `LinkModifier` names the source
    /// channel it reads with this.
    enum class TrackChannel {
        Position,
        Anchor,
        Scale,
        Rotation,
        Orientation,
        Opacity,
        TimeRemap
    };

    enum class LoopMode {
        Repeat,
        PingPong
    };

    /// Sample the interval [start, end) repeatedly; PingPong reflects each second pass.
    struct LoopModifier {
        float start{0.f};
        float end{1.f};
        LoopMode mode{LoopMode::Repeat};
    };

    /// Quantize sample time to a fixed number of samples per second.
    struct PosterizeModifier {
        float samplesPerSecond{24.f};
    };

    /// Shift the sample time by seconds (positive values create a delay).
    struct DelayModifier {
        float seconds{0.f};
    };

    /// Add another layer/channel's change relative to its value at originTime.
    struct LinkModifier {
        std::uint32_t layerId{0};
        TrackChannel channel{TrackChannel::Position};
        float originTime{0.f};
    };

    /// Limit the channel to a time window.
    ///
    /// Inside `[start, end]` the sample time passes through untouched, outside it
    /// is clamped to the nearer end, so the channel *holds* its edge value instead
    /// of extrapolating. It is the channel-level work area: it limits when a move
    /// happens without editing a single key, and it is the complement of
    /// `LoopModifier` (which repeats a window instead of holding outside it).
    ///
    /// A `start == end` window is a single instant: the channel holds the value it
    /// has there, everywhere.
    struct WindowModifier {
        float start{0.f};
        float end{0.f};
    };

    /// Attenuate a channel against its own value at `originTime`.
    ///
    /// `amount == 1` is the untouched channel, `amount == 0` freezes it at the
    /// value it had at `originTime`, and in between is the blend of the two. It is
    /// how a move is dialled down (or cross-faded in) without touching its keys.
    ///
    /// Unlike the time-warp modifiers this is a *value* filter, so it scales the
    /// channel's rate by the same `amount`: a channel faded out to zero influence
    /// reports zero velocity rather than the velocity it no longer has.
    struct InfluenceModifier {
        float amount{1.f};
        float originTime{0.f};
    };

    /// Temporal smoothing: average the channel over `[t - radius, t + radius]`.
    ///
    /// `samples` evenly spaced instants across that window are averaged in
    /// insertion order (a box kernel, so the smoothing lags nothing), and the
    /// window is symmetric so a symmetric move stays symmetric. This is how a
    /// jittery or over-keyed channel is softened to a readable move without
    /// deleting keys.
    ///
    /// The mean of a sum is the sum of the means, so the smoothed channel's
    /// derivative is the mean of the raw channel's derivative — which is exactly
    /// what `velocity()` reports. `samples == 1` (or `radius <= 0`) is the raw
    /// sample at `t`.
    struct SmoothModifier {
        float radius{0.f};
        int samples{3};
    };

    using TrackModifier = std::variant<LoopModifier, PosterizeModifier, DelayModifier, LinkModifier,
                                      WindowModifier, InfluenceModifier, SmoothModifier>;

    /// What a link resolves to: the linked channel's value, or its derivative.
    /// The alternative carried is the channel's own type — a Vector3 channel
    /// resolves to `Vector3`, `Orientation` to `Quaternion`, `Opacity` to
    /// `float` — so a mismatched link is a typed error instead of a silent cast.
    using LinkValue = std::variant<float, Vector3, Quaternion>;

    /// Resolves `(layerId, channel, time)` to that channel's value.
    ///
    /// `time` is in the requesting track's own domain and is already the
    /// chain's *mapped* time, so a looped or delayed track links to the source
    /// at the same warped instant it samples its own keys at. For `velocity()`
    /// the same function shape carries the channel's **derivative** instead;
    /// each method states which one it wants.
    ///
    /// A track with no `LinkModifier` never needs one; an empty resolver on a
    /// linked track is an authoring error and throws.
    using LinkResolver = std::function<LinkValue(std::uint32_t, TrackChannel, float)>;


} // namespace chrononmotion::motion

#endif // CHRONONMOTION_MOTION_TRACK_TYPES_HPP
