// ChrononMotion — keyed values over time.
//
// The sampling authority for a single channel. `AnimationClip`/`KeyframeTrack`
// (kept from threepp) describe a clip as a bag of named tracks; this is the
// per-property curve the motion compiler actually reads, with per-segment easing
// and type-correct blending.

#ifndef CHRONONMOTION_MOTION_TRACK_HPP
#define CHRONONMOTION_MOTION_TRACK_HPP

#include "chrononmotion/motion/Track_types.hpp"
#include "chrononmotion/motion/Track_blend.hpp"


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

        /// Attach an owner policy guard. Intended for MotionScene-owned tracks.
        void setMutationGuard(std::function<void(std::size_t, std::size_t)> guard) {
            mutationGuard_ = std::move(guard);
        }

        void setReadOnlyGuard(std::function<void()> guard) {
            readOnlyGuard_ = std::move(guard);
        }

        /// Insert (or replace) a key. Out-of-order times are fine: the track sorts.
        Track& add(float time, const T& value, const Easing& easing = Easing::linear()) {

            const auto it = std::lower_bound(
                    keys_.begin(), keys_.end(), time,
                    [](const Keyframe<T>& key, float t) { return key.time < t; });

            if (it != keys_.end() && it->time == time) {
                checkMutation(keys_.size(), keys_.size());
                it->value = value;
                it->easing = easing;
                it->discrete = false;
                return *this;
            }

            checkMutation(keys_.size(), keys_.size() + 1);
            keys_.insert(it, Keyframe<T>{time, value, easing, false});
            return *this;
        }

        /// Hold a value from `time` until the next key ("step" / constant hold).
        Track& addDiscrete(float time, const T& value) {

            const auto it = std::lower_bound(
                    keys_.begin(), keys_.end(), time,
                    [](const Keyframe<T>& key, float t) { return key.time < t; });

            if (it != keys_.end() && it->time == time) {
                checkMutation(keys_.size(), keys_.size());
                it->value = value;
                it->discrete = true;
                return *this;
            }

            checkMutation(keys_.size(), keys_.size() + 1);
            keys_.insert(it, Keyframe<T>{time, value, Easing::linear(), true});
            return *this;
        }

        /// Remove the key exactly at `time`, if there is one.
        bool remove(float time) {

            const auto it = std::lower_bound(
                    keys_.begin(), keys_.end(), time,
                    [](const Keyframe<T>& key, float t) { return key.time < t; });

            if (it == keys_.end() || it->time != time) return false;
            checkMutation(keys_.size(), keys_.size() - 1);
            keys_.erase(it);
            return true;
        }

        Track& clear() {

            checkMutation(keys_.size(), 0);
            keys_.clear();
            return *this;
        }

        void reserve(std::size_t count) {
            if (readOnlyGuard_) readOnlyGuard_();
            keys_.reserve(count);
        }

        [[nodiscard]] bool empty() const { return keys_.empty(); }
        [[nodiscard]] std::size_t size() const { return keys_.size(); }
        [[nodiscard]] const std::vector<Keyframe<T>>& keys() const { return keys_; }
        [[nodiscard]] std::vector<Keyframe<T>>& keys() {
            if (readOnlyGuard_) readOnlyGuard_();
            return keys_;
        }

        [[nodiscard]] const std::vector<TrackModifier>& modifiers() const { return modifiers_; }
        [[nodiscard]] std::vector<TrackModifier>& editModifiers() {
            if (readOnlyGuard_) readOnlyGuard_();
            return modifiers_;
        }

        Track& addModifier(LoopModifier modifier) {
            checkMutation(keys_.size(), keys_.size());
            modifiers_.emplace_back(modifier);
            return *this;
        }

        Track& addModifier(PosterizeModifier modifier) {
            checkMutation(keys_.size(), keys_.size());
            modifiers_.emplace_back(modifier);
            return *this;
        }

        Track& addModifier(DelayModifier modifier) {
            checkMutation(keys_.size(), keys_.size());
            modifiers_.emplace_back(modifier);
            return *this;
        }

        Track& addModifier(LinkModifier modifier) {
            checkMutation(keys_.size(), keys_.size());
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

        Track& addModifier(WindowModifier modifier) {
            checkMutation(keys_.size(), keys_.size());
            modifiers_.emplace_back(modifier);
            return *this;
        }

        Track& addModifier(InfluenceModifier modifier) {
            checkMutation(keys_.size(), keys_.size());
            modifiers_.emplace_back(modifier);
            return *this;
        }

        Track& addModifier(SmoothModifier modifier) {
            checkMutation(keys_.size(), keys_.size());
            modifiers_.emplace_back(modifier);
            return *this;
        }

        /// Hold the channel outside `[start, end]` (see `WindowModifier`).
        Track& window(float start, float end) {
            return addModifier(WindowModifier{start, end});
        }

        /// Attenuate the channel against its own value at `originTime` (see
        /// `InfluenceModifier`).
        Track& influence(float amount, float originTime = 0.f) {
            return addModifier(InfluenceModifier{amount, originTime});
        }

        /// Average the channel over a `radius`-second window (see `SmoothModifier`).
        Track& smooth(float radius, int samples = 3) {
            return addModifier(SmoothModifier{radius, samples});
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
                } else if (const auto* window = std::get_if<WindowModifier>(&modifier)) {
                    if (!std::isfinite(window->start) || !std::isfinite(window->end) ||
                        window->end < window->start) {
                        return "WindowModifier requires finite start <= end";
                    }
                } else if (const auto* influence = std::get_if<InfluenceModifier>(&modifier)) {
                    if (!std::isfinite(influence->amount) || influence->amount < 0.f ||
                        influence->amount > 1.f) {
                        return "InfluenceModifier requires an amount in [0, 1]";
                    }
                    if (!std::isfinite(influence->originTime)) {
                        return "InfluenceModifier requires a finite originTime";
                    }
                } else if (const auto* smooth = std::get_if<SmoothModifier>(&modifier)) {
                    if (!std::isfinite(smooth->radius) || smooth->radius < 0.f) {
                        return "SmoothModifier requires a finite non-negative radius";
                    }
                    if (smooth->samples < 1) return "SmoothModifier requires at least one sample";
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
        ///
        /// The modifiers are applied in two stages, and the stage a modifier
        /// belongs to is what it does, not when it was added:
        ///
        /// - **time warps** (`Loop`, `Posterize`, `Delay`, `Window`) move the
        ///   instant the channel is read at;
        /// - **value filters** (`Smooth`, `Influence`) shape the value that comes
        ///   back. `Smooth` runs innermost (it averages the raw channel) and
        ///   `Influence` outermost, in the order they were added.
        ///
        /// `Link` sits with the keys rather than with either stage: the linked
        /// channel's delta is part of what "this channel's value" means, so a
        /// smoothing pass averages the borrowed motion together with the owned one.
        ///
        /// The value the caller passes in is the channel's **seed** — the layer's
        /// static pose, which a key replaces and a link composes onto. Every buffer
        /// the filters sample into starts from that same seed, so a frozen
        /// reference is always a pose this channel could actually have produced.
        void sample(float time, T& out, const LinkResolver& resolve) const {

            // A channel with no keys and no link has nothing to say: leave the
            // caller's value alone so a layer's static pose still survives.
            if (keys_.empty() && !hasLinkModifier()) return;

            const T seed = out;
            sampleSmoothed(0, time, seed, out, resolve);
            applyValueModifiers(seed, out, resolve);
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
        ///
        /// `SmoothModifier` averages this derivative across the same window it
        /// averages the value over — exact rather than approximate, because a mean
        /// is linear and so commutes with differentiation — and `InfluenceModifier`
        /// scales the result by its amount, because a channel dialled down to zero
        /// influence is not moving whatever its keys say.
        [[nodiscard]] T velocity(float time, const LinkResolver& resolveRates) const {

            T out = velocitySmoothed(0, time, resolveRates);
            // A channel dialled down by an influence is not moving the way its keys
            // say, so its rate is dialled down by the same factor.
            const float scale = influenceScale();
            if (scale != 1.f) out = scaleRate(out, scale);
            return out;
        }

    private:
        void checkMutation(std::size_t oldCount, std::size_t newCount) const {
            if (readOnlyGuard_) readOnlyGuard_();
            if (mutationGuard_) mutationGuard_(oldCount, newCount);
        }

        std::function<void(std::size_t, std::size_t)> mutationGuard_{};
        std::function<void()> readOnlyGuard_{};

        /// True when this channel carries a cross-layer link, so an empty key list
        /// is not the same thing as "nothing to say".
        [[nodiscard]] bool hasLinkModifier() const {
            for (const auto& modifier : modifiers_) {
                if (std::holds_alternative<LinkModifier>(modifier)) return true;
            }
            return false;
        }

        [[nodiscard]] static float smoothingOffset(int sample, int samples) {

            // `samples == 1` is the raw instant; otherwise the offsets walk the
            // closed window from -radius to +radius inclusive.
            if (samples <= 1) return 0.f;
            return 2.f * (static_cast<float>(sample) / static_cast<float>(samples - 1)) - 1.f;
        }

        /// The raw channel value at `time`: time warps applied, keys interpolated,
        /// links added, and *no* value filter. `SmoothModifier` averages this over a
        /// window, so it has to be reachable without re-entering the filters.
        void sampleCore(float time, T& out, const LinkResolver& resolve) const {

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

        /// `sampleCore` with every `SmoothModifier` from `index` on applied, the
        /// first one innermost. The mean is accumulated incrementally through
        /// `blendValue`, which is the arithmetic mean for the scalar/vector types
        /// and the incremental shortest-arc mean for a quaternion — the only way to
        /// average orientations that does not go through a matrix.
        void sampleSmoothed(std::size_t index, float time, const T& seed, T& out,
                           const LinkResolver& resolve) const {

            for (std::size_t i = index; i < modifiers_.size(); ++i) {
                const auto* smooth = std::get_if<SmoothModifier>(&modifiers_[i]);
                if (!smooth || smooth->samples <= 1 || !(smooth->radius > 0.f)) continue;

                T mean{};
                for (int s = 0; s < smooth->samples; ++s) {
                    // Seeded, not default-built: a linked channel adds to the pose it
                    // was handed, so an unseeded buffer would average the borrowed
                    // delta around a pose the channel never had.
                    T value = seed;
                    sampleSmoothed(i + 1, time + smoothingOffset(s, smooth->samples) * smooth->radius,
                                   seed, value, resolve);
                    mean = s == 0 ? value
                                  : blendValue(mean, value, 1.f / static_cast<float>(s + 1));
                }
                out = mean;
                return;
            }

            out = seed;
            sampleCore(time, out, resolve);
        }

        /// The derivative of `sampleCore` with the same smoothing passes, so the
        /// smoothed channel and its reported rate can never disagree. Averaging is
        /// linear, so the mean of the derivatives *is* the derivative of the mean.
        [[nodiscard]] T velocitySmoothed(std::size_t index, float time,
                                         const LinkResolver& resolveRates) const {

            for (std::size_t i = index; i < modifiers_.size(); ++i) {
                const auto* smooth = std::get_if<SmoothModifier>(&modifiers_[i]);
                if (!smooth || smooth->samples <= 1 || !(smooth->radius > 0.f)) continue;

                T mean{};
                for (int s = 0; s < smooth->samples; ++s) {
                    const T value = velocitySmoothed(
                            i + 1, time + smoothingOffset(s, smooth->samples) * smooth->radius,
                            resolveRates);
                    mean = s == 0 ? value
                                  : blendValue(mean, value, 1.f / static_cast<float>(s + 1));
                }
                return mean;
            }

            return velocityCore(time, resolveRates);
        }

        /// The channel's own analytic rate, before the value filters.
        [[nodiscard]] T velocityCore(float time, const LinkResolver& resolveRates) const {

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

        /// Every `InfluenceModifier` composed, in the order they were added.
        ///
        /// The frozen reference is sampled through the same path and from the same
        /// `seed` as the live value, so "the pose this channel had at originTime"
        /// means the same thing in both places for a keyed channel and for one that
        /// only borrows another layer's motion.
        void applyValueModifiers(const T& seed, T& out, const LinkResolver& resolve) const {

            for (const auto& modifier : modifiers_) {
                const auto* influence = std::get_if<InfluenceModifier>(&modifier);
                if (!influence) continue;

                T frozen = seed;
                sampleSmoothed(0, influence->originTime, seed, frozen, resolve);
                out = blendValue(frozen, out, influence->amount);
            }
        }

        /// The product of every influence amount: how much of the keyed motion is
        /// still reaching the output.
        [[nodiscard]] float influenceScale() const {

            float scale = 1.f;
            for (const auto& modifier : modifiers_) {
                if (const auto* influence = std::get_if<InfluenceModifier>(&modifier)) {
                    scale *= influence->amount;
                }
            }
            return scale;
        }

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
                } else if (const auto* window = std::get_if<WindowModifier>(&modifier)) {
                    if (!std::isfinite(window->start) || !std::isfinite(window->end) ||
                        window->end < window->start) {
                        throw std::invalid_argument("Track: WindowModifier requires finite start <= end");
                    }
                    // Inside the window the time passes through unchanged; outside it
                    // snaps to the nearer edge so the channel holds its edge value
                    // instead of extrapolating. Exactly on an edge the two one-sided
                    // slopes disagree and there is no single derivative, so the
                    // finite 0 is reported — the same cusp convention LoopModifier
                    // uses at a ping-pong turnaround.
                    if (time <= window->start) {
                        time = window->start;
                        rate = 0.f;
                    } else if (time >= window->end) {
                        time = window->end;
                        rate = 0.f;
                    }
                } else if (const auto* influence = std::get_if<InfluenceModifier>(&modifier)) {
                    // A value filter, not a time warp. It is validated here because
                    // every sample and every rate goes through this loop, so the
                    // condition `modifierProblem()` reports at authoring time is the
                    // same one sampling throws — not a second definition of it.
                    if (!std::isfinite(influence->amount) || influence->amount < 0.f ||
                        influence->amount > 1.f) {
                        throw std::invalid_argument("Track: InfluenceModifier requires an amount in [0, 1]");
                    }
                    if (!std::isfinite(influence->originTime)) {
                        throw std::invalid_argument("Track: InfluenceModifier requires a finite originTime");
                    }
                } else if (const auto* smooth = std::get_if<SmoothModifier>(&modifier)) {
                    if (!std::isfinite(smooth->radius) || smooth->radius < 0.f) {
                        throw std::invalid_argument("Track: SmoothModifier requires a finite non-negative radius");
                    }
                    if (smooth->samples < 1) {
                        throw std::invalid_argument("Track: SmoothModifier requires at least one sample");
                    }
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
