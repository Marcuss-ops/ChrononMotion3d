// ChrononMotion — keyed values over time.
//
// The sampling authority for a single channel. `AnimationClip`/`KeyframeTrack`
// (kept from threepp) describe a clip as a bag of named tracks; this is the
// per-property curve the motion compiler actually reads, with per-segment easing
// and type-correct blending.

#ifndef CHRONONMOTION_MOTION_TRACK_HPP
#define CHRONONMOTION_MOTION_TRACK_HPP

#include "chrononmotion/motion/Easing.hpp"

#include "chrononmotion/math/Quaternion.hpp"
#include "chrononmotion/math/Vector3.hpp"
#include "chrononmotion/math/Color.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
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

    /// Blend two keyed values with the right geometry for their type.
    ///
    /// Quaternions slerp along the shortest arc (`slerpQuaternions` negates the
    /// antipodal operand), so an orientation track never takes the long way
    /// through ±180°.
    [[nodiscard]] inline float blendValue(float a, float b, float t) {

        return a + (b - a) * t;
    }

    [[nodiscard]] inline Vector3 blendValue(const Vector3& a, const Vector3& b, float t) {

        Vector3 out;
        out.lerpVectors(a, b, t);
        return out;
    }

    [[nodiscard]] inline Quaternion blendValue(const Quaternion& a, const Quaternion& b, float t) {

        Quaternion out;
        out.slerpQuaternions(a, b, t);
        return out;
    }

    [[nodiscard]] inline Color blendValue(const Color& a, const Color& b, float t) {

        Color out;
        out.lerpColors(a, b, t);
        return out;
    }


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

    using TrackModifier = std::variant<LoopModifier, PosterizeModifier, DelayModifier, LinkModifier>;

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

    /// A value keyed over time.
    ///
    /// Keys are kept sorted and unique by time. Each key owns the Easing used to
    /// reach the *next* key, so a curve can be shaped per segment. `sample()` is
    /// a pure function of time — no cached index, no clock, no mutation — so the
    /// same time always yields the same value, in any call order.
    template<class T>
    class Track {
    public:
        Track() = default;

        Track(std::initializer_list<Keyframe<T>> keys) {

            for (const auto& key : keys) {
                add(key.time, key.value, key.easing);
            }
        }

        /// Insert (or replace) a key. Out-of-order times are fine: the track sorts.
        Track& add(float time, const T& value, const Easing& easing = Easing::linear()) {

            const auto it = std::lower_bound(
                    keys_.begin(), keys_.end(), time,
                    [](const Keyframe<T>& key, float t) { return key.time < t; });

            if (it != keys_.end() && it->time == time) {
                it->value = value;
                it->easing = easing;
                it->discrete = false;
                return *this;
            }

            keys_.insert(it, Keyframe<T>{time, value, easing, false});
            return *this;
        }

        /// Hold a value from `time` until the next key ("step" / constant hold).
        Track& addDiscrete(float time, const T& value) {

            const auto it = std::lower_bound(
                    keys_.begin(), keys_.end(), time,
                    [](const Keyframe<T>& key, float t) { return key.time < t; });

            if (it != keys_.end() && it->time == time) {
                it->value = value;
                it->discrete = true;
                return *this;
            }

            keys_.insert(it, Keyframe<T>{time, value, Easing::linear(), true});
            return *this;
        }

        /// Remove the key exactly at `time`, if there is one.
        bool remove(float time) {

            const auto it = std::lower_bound(
                    keys_.begin(), keys_.end(), time,
                    [](const Keyframe<T>& key, float t) { return key.time < t; });

            if (it == keys_.end() || it->time != time) return false;
            keys_.erase(it);
            return true;
        }

        Track& clear() {

            keys_.clear();
            return *this;
        }

        void reserve(std::size_t count) { keys_.reserve(count); }

        [[nodiscard]] bool empty() const { return keys_.empty(); }
        [[nodiscard]] std::size_t size() const { return keys_.size(); }
        [[nodiscard]] const std::vector<Keyframe<T>>& keys() const { return keys_; }
        [[nodiscard]] std::vector<Keyframe<T>>& keys() { return keys_; }
        [[nodiscard]] const std::vector<TrackModifier>& modifiers() const { return modifiers_; }

        Track& addModifier(LoopModifier modifier) {
            modifiers_.emplace_back(modifier);
            return *this;
        }

        Track& addModifier(PosterizeModifier modifier) {
            modifiers_.emplace_back(modifier);
            return *this;
        }

        Track& addModifier(DelayModifier modifier) {
            modifiers_.emplace_back(modifier);
            return *this;
        }

        Track& addModifier(LinkModifier modifier) {
            modifiers_.emplace_back(modifier);
            return *this;
        }

        Track& loop(float start, float end, LoopMode mode = LoopMode::Repeat) {
            return addModifier(LoopModifier{start, end, mode});
        }

        Track& posterize(float samplesPerSecond) {
            return addModifier(PosterizeModifier{samplesPerSecond});
        }

        Track& delay(float seconds) {
            return addModifier(DelayModifier{seconds});
        }

        Track& link(std::uint32_t layerId, TrackChannel channel, float originTime = 0.f) {
            return addModifier(LinkModifier{layerId, channel, originTime});
        }

        /// Empty when every modifier is well-formed, otherwise the first problem
        /// found. Sampling throws the same condition; this lets authoring report
        /// it before a frame is ever evaluated.
        [[nodiscard]] std::string modifierProblem() const {

            for (const auto& modifier : modifiers_) {
                if (const auto* loop = std::get_if<LoopModifier>(&modifier)) {
                    if (!std::isfinite(loop->start) || !std::isfinite(loop->end) || !(loop->end > loop->start)) {
                        return "LoopModifier requires finite start < end";
                    }
                } else if (const auto* posterize = std::get_if<PosterizeModifier>(&modifier)) {
                    if (!std::isfinite(posterize->samplesPerSecond) || !(posterize->samplesPerSecond > 0.f)) {
                        return "PosterizeModifier requires a finite positive samples-per-second";
                    }
                } else if (const auto* delay = std::get_if<DelayModifier>(&modifier)) {
                    if (!std::isfinite(delay->seconds)) return "DelayModifier requires finite seconds";
                } else if (const auto* link = std::get_if<LinkModifier>(&modifier)) {
                    if (!std::isfinite(link->originTime)) return "LinkModifier requires a finite originTime";
                }
            }
            return {};
        }

        [[nodiscard]] float startTime() const { return keys_.empty() ? 0.f : keys_.front().time; }
        [[nodiscard]] float endTime() const { return keys_.empty() ? 0.f : keys_.back().time; }
        [[nodiscard]] float duration() const { return keys_.empty() ? 0.f : endTime() - startTime(); }

        /// Sample into `out`. An empty track leaves `out` untouched, which is how
        /// a layer's static pose survives where a channel has no keys.
        void sample(float time, T& out) const {
            sample(time, out, {});
        }

        /// Sample with a resolver for cross-layer links. A standalone track with
        /// no LinkModifier does not need a resolver.
        void sample(float time, T& out, const LinkResolver& resolve) const {

            const float mappedTime = mapTime(time);
            if (keys_.empty()) {
                applyLinks(mappedTime, out, resolve);
                return;
            }

            if (mappedTime <= keys_.front().time) {
                out = keys_.front().value;
                applyLinks(mappedTime, out, resolve);
                return;
            }
            if (mappedTime >= keys_.back().time) {
                out = keys_.back().value;
                applyLinks(mappedTime, out, resolve);
                return;
            }

            std::size_t lo = 0;
            std::size_t hi = keys_.size() - 1;
            while (hi - lo > 1) {
                const std::size_t mid = (lo + hi) / 2;
                if (keys_[mid].time <= mappedTime) {
                    lo = mid;
                } else {
                    hi = mid;
                }
            }

            const Keyframe<T>& a = keys_[lo];
            const Keyframe<T>& b = keys_[hi];

            if (a.discrete) {
                out = a.value;
                applyLinks(mappedTime, out, resolve);
                return;
            }

            const float span = b.time - a.time;
            const float normalized = span > 0.f ? std::clamp((mappedTime - a.time) / span, 0.f, 1.f) : 1.f;
            out = blendValue(a.value, b.value, a.easing(normalized));
            applyLinks(mappedTime, out, resolve);
        }

        [[nodiscard]] T sample(float time) const {

            T out{};
            sample(time, out);
            return out;
        }

        [[nodiscard]] T sample(float time, const LinkResolver& resolve) const {
            T out{};
            sample(time, out, resolve);
            return out;
        }

        /// d(value)/dt at `time`, in units per second.
        ///
        /// The chain rule for one keyed segment:
        ///
        ///     dV/dt = easing'(u) * (V1 - V0) / span
        ///
        /// where u is the normalized position inside the segment and span is its
        /// duration in seconds. Dividing by the span is what makes the same keys
        /// report the same physical speed however far apart the keys are.
        ///
        /// Defined for every `T` `blendValue` accepts. A held (discrete) segment
        /// has no continuous path, so it reports zero; use `MotionScene::motion()`
        /// when a finite difference across the jump is wanted instead.
        ///
        /// Outside the keyed range the value is held and so is its derivative: the
        /// track does not extrapolate, and neither does this.
        [[nodiscard]] T velocity(float time) const {
            return velocity(time, {});
        }

        /// The analytic derivative, with cross-layer links.
        ///
        /// `resolveRates` must return the linked channel's **derivative** at the
        /// requested time (not its value): the relative link contributes exactly
        /// the source's rate, scaled by the chain's own time derivative.
        [[nodiscard]] T velocity(float time, const LinkResolver& resolveRates) const {

            float rate = 1.f;
            time = mapTime(time, &rate);

            T out{};
            if (!keys_.empty() && rate != 0.f &&
                time > keys_.front().time && time < keys_.back().time) {

                std::size_t lo = 0;
                std::size_t hi = keys_.size() - 1;
                while (hi - lo > 1) {
                    const std::size_t mid = (lo + hi) / 2;
                    if (keys_[mid].time <= time) {
                        lo = mid;
                    } else {
                        hi = mid;
                    }
                }

                const Keyframe<T>& a = keys_[lo];
                const Keyframe<T>& b = keys_[hi];
                const float span = b.time - a.time;
                if (!a.discrete && span > 0.f) {
                    const float u = std::clamp((time - a.time) / span, 0.f, 1.f);
                    out = scaledDifference(a.value, b.value, a.easing.derivative(u) / span * rate);
                }
            }

            for (const auto& modifier : modifiers_) {
                if (const auto* linkModifier = std::get_if<LinkModifier>(&modifier)) {
                    if (!resolveRates) {
                        throw std::invalid_argument("Track::velocity: LinkModifier requires a scene resolver");
                    }
                    const LinkValue linked = resolveRates(linkModifier->layerId, linkModifier->channel, time);
                    out = addValue(out, scaleRate(linkedValue<T>(linked), rate));
                }
            }
            return out;
        }

    private:
        [[nodiscard]] float mapTime(float time, float* derivative = nullptr) const {
            float rate = 1.f;
            for (const auto& modifier : modifiers_) {
                if (const auto* loop = std::get_if<LoopModifier>(&modifier)) {
                    if (!std::isfinite(loop->start) || !std::isfinite(loop->end) || !(loop->end > loop->start)) {
                        throw std::invalid_argument("Track: LoopModifier requires finite start < end");
                    }
                    const float length = loop->end - loop->start;
                    const float period = loop->mode == LoopMode::PingPong ? 2.f * length : length;
                    float phase = std::fmod(time - loop->start, period);
                    if (phase < 0.f) phase += period;
                    if (loop->mode == LoopMode::PingPong && phase > length) {
                        time = loop->end - (phase - length);
                        rate = -rate;
                    } else {
                        time = loop->start + phase;
                    }
                    // A reflected pass reverses the direction of travel. Exactly
                    // on a cusp the two one-sided slopes disagree and there is no
                    // single derivative, so report the finite 0 rather than pick
                    // a side: a repeat loop has no cusp and keeps its rate.
                    if (loop->mode == LoopMode::PingPong &&
                        (std::abs(phase) < 1e-6f || std::abs(phase - length) < 1e-6f)) rate = 0.f;
                } else if (const auto* posterize = std::get_if<PosterizeModifier>(&modifier)) {
                    if (!std::isfinite(posterize->samplesPerSecond) || !(posterize->samplesPerSecond > 0.f)) {
                        throw std::invalid_argument("Track: PosterizeModifier requires a finite positive rate");
                    }
                    time = std::floor(time * posterize->samplesPerSecond) / posterize->samplesPerSecond;
                    rate = 0.f;
                } else if (const auto* delay = std::get_if<DelayModifier>(&modifier)) {
                    if (!std::isfinite(delay->seconds)) throw std::invalid_argument("Track: DelayModifier requires finite seconds");
                    time -= delay->seconds;
                }
            }
            if (derivative) *derivative = rate;
            return time;
        }

        void applyLinks(float time, T& value, const LinkResolver& resolve) const {
            for (const auto& modifier : modifiers_) {
                if (const auto* link = std::get_if<LinkModifier>(&modifier)) {
                    if (!resolve) throw std::invalid_argument("Track::sample: LinkModifier requires a scene resolver");
                    const T current = linkedValue<T>(resolve(link->layerId, link->channel, time));
                    const T origin = linkedValue<T>(resolve(link->layerId, link->channel, link->originTime));
                    value = addValue(value, difference(current, origin));
                }
            }
        }

        /// Pull `T` out of a resolved link, rejecting a channel of another type.
        template<class U>
        [[nodiscard]] static U linkedValue(const LinkValue& value) {

            if constexpr (std::is_same_v<U, float> || std::is_same_v<U, Vector3> ||
                          std::is_same_v<U, Quaternion>) {
                if (const U* typed = std::get_if<U>(&value)) return *typed;
                throw std::invalid_argument("Track: linked channel has a different type than this track");
            } else {
                throw std::invalid_argument("Track: this channel type cannot be linked");
            }
        }

        /// Multiply a rate by the chain's time derivative, in the same geometry
        /// `blendValue` uses. A rate is a free vector in the channel's own space,
        /// so only a quaternion rate needs the angular path.
        [[nodiscard]] static T scaleRate(const T& value, float scale) {

            T out;
            if constexpr (std::is_same_v<T, Quaternion>) {
                sampleScaled(T{0.f, 0.f, 0.f, 1.f}, value, scale, out);
            } else if constexpr (std::is_same_v<T, Vector3>) {
                out = value;
                out.multiplyScalar(scale);
            } else if constexpr (std::is_same_v<T, Color>) {
                out.r = value.r * scale;
                out.g = value.g * scale;
                out.b = value.b * scale;
            } else {
                out = value * scale;
            }
            return out;
        }

        [[nodiscard]] static T difference(const T& a, const T& b) {
            if constexpr (std::is_same_v<T, float>) {
                return a - b;
            } else if constexpr (std::is_same_v<T, Vector3>) {
                T out = a;
                out.sub(b);
                return out;
            } else if constexpr (std::is_same_v<T, Quaternion>) {
                T out = b;
                out.invert();
                return out.multiply(a);
            } else if constexpr (std::is_same_v<T, Color>) {
                // Color carries r/g/b only — this fork's Color has no alpha.
                T out;
                out.r = a.r - b.r;
                out.g = a.g - b.g;
                out.b = a.b - b.b;
                return out;
            } else {
                static_assert(std::is_same_v<T, void>, "Track link does not support this value type");
            }
        }

        [[nodiscard]] static T addValue(const T& a, const T& b) {
            if constexpr (std::is_same_v<T, float>) {
                return a + b;
            } else if constexpr (std::is_same_v<T, Vector3>) {
                T out = a;
                out.add(b);
                return out;
            } else if constexpr (std::is_same_v<T, Quaternion>) {
                T out = a;
                return out.multiply(b);
            } else if constexpr (std::is_same_v<T, Color>) {
                // Color carries r/g/b only — this fork's Color has no alpha.
                T out;
                out.r = a.r + b.r;
                out.g = a.g + b.g;
                out.b = a.b + b.b;
                return out;
            } else {
                static_assert(std::is_same_v<T, void>, "Track link does not support this value type");
            }
        }

        /// (b - a) * rate, by the same geometry `blendValue` uses, so velocity is
        /// expressed in the channel's own space (a scalar for float, a vector for
        /// Vector3) rather than forcing every channel through a matrix.
        [[nodiscard]] static T scaledDifference(const T& a, const T& b, float rate) {

            T out;
            sampleScaled(a, b, rate, out);
            return out;
        }

        static void sampleScaled(const T& a, const T& b, float rate, T& out) {

            if constexpr (std::is_same_v<T, float>) {
                out = (b - a) * rate;
            } else if constexpr (std::is_same_v<T, Vector3>) {
                out = b;
                out.sub(a);
                out.multiplyScalar(rate);
            } else if constexpr (std::is_same_v<T, Quaternion>) {
                // Angular rate about the shortest arc from a to b. The same
                // double-cover flip as MotionScene::motion(), so a small step
                // never reads as a full turn.
                out.copy(a);
                out.invert();
                Quaternion step = out.multiply(b);
                if (step.w < 0.f) {
                    step.x = -step.x;
                    step.y = -step.y;
                    step.z = -step.z;
                    step.w = -step.w;
                }
                const float sinHalf = std::sqrt(
                        step.x * step.x + step.y * step.y + step.z * step.z);
                const float angle = 2.f * std::atan2(sinHalf, step.w);
                if (angle > 1e-8f && sinHalf > 1e-8f) {
                    const Vector3 axis{step.x / sinHalf, step.y / sinHalf, step.z / sinHalf};
                    out.setFromAxisAngle(axis, angle * rate);
                } else {
                    out.set(0.f, 0.f, 0.f, 1.f);
                }
            } else if constexpr (std::is_same_v<T, Color>) {
                // Color carries r/g/b only — this fork's Color has no alpha.
                out.r = (b.r - a.r) * rate;
                out.g = (b.g - a.g) * rate;
                out.b = (b.b - a.b) * rate;
            } else {
                out = b;
                out.sub(a);
                out.multiplyScalar(rate);
            }
        }

        std::vector<Keyframe<T>> keys_;
        std::vector<TrackModifier> modifiers_;
    };

}// namespace chrononmotion::motion

#endif//CHRONONMOTION_MOTION_TRACK_HPP
