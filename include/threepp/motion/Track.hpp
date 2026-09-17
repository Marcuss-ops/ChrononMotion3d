// ChrononMotion — keyed values over time.
//
// The sampling authority for a single channel. `AnimationClip`/`KeyframeTrack`
// (kept from threepp) describe a clip as a bag of named tracks; this is the
// per-property curve the motion compiler actually reads, with per-segment easing
// and type-correct blending.

#ifndef THREEPP_MOTION_TRACK_HPP
#define THREEPP_MOTION_TRACK_HPP

#include "threepp/motion/Easing.hpp"

#include "threepp/math/Quaternion.hpp"
#include "threepp/math/Vector3.hpp"

#include <algorithm>
#include <cstddef>
#include <initializer_list>
#include <vector>

namespace threepp::motion {

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

        [[nodiscard]] float startTime() const { return keys_.empty() ? 0.f : keys_.front().time; }
        [[nodiscard]] float endTime() const { return keys_.empty() ? 0.f : keys_.back().time; }
        [[nodiscard]] float duration() const { return keys_.empty() ? 0.f : endTime() - startTime(); }

        /// Sample into `out`. An empty track leaves `out` untouched, which is how
        /// a layer's static pose survives where a channel has no keys.
        void sample(float time, T& out) const {

            if (keys_.empty()) return;

            if (time <= keys_.front().time) {
                out = keys_.front().value;
                return;
            }
            if (time >= keys_.back().time) {
                out = keys_.back().value;
                return;
            }

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

            if (a.discrete) {
                out = a.value;
                return;
            }

            const float span = b.time - a.time;
            const float normalized = span > 0.f ? std::clamp((time - a.time) / span, 0.f, 1.f) : 1.f;
            out = blendValue(a.value, b.value, a.easing(normalized));
        }

        [[nodiscard]] T sample(float time) const {

            T out{};
            sample(time, out);
            return out;
        }

    private:
        std::vector<Keyframe<T>> keys_;
    };

}// namespace threepp::motion

#endif//THREEPP_MOTION_TRACK_HPP
