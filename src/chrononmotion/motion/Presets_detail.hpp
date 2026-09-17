// ChrononMotion — the preset helpers, shared by the preset families.
//
// Internal to the library: this is the vocabulary every family keys with, not
// part of the public surface (that is Presets.hpp). It exists because the
// presets are split by family, and the families have to key their curves
// *identically* — a per-family `keyVector3` is exactly how one of them would
// quietly grow a different end-of-segment convention.
//
//   restOf()      the pose a preset starts from
//   uniform()     a Vector3 with the same value on all three axes
//   keyVector3()  two keys: the shaped curve into the target, then a flat hold

#ifndef CHRONONMOTION_MOTION_PRESETS_DETAIL_HPP
#define CHRONONMOTION_MOTION_PRESETS_DETAIL_HPP

#include "chrononmotion/motion/Presets.hpp"

namespace chrononmotion::motion::presets::detail {

    /// The pose a preset starts from: the layer as it is sampled just before the
    /// preset writes its first key.
    [[nodiscard]] inline Transform restOf(const Layer& layer, int inFrame, float fps) {

        return layer.sample(frameToTime(inFrame, fps));
    }

    [[nodiscard]] inline Vector3 uniform(float value) {

        return Vector3(value, value, value);
    }

    /// The shape of a keyed move: the key at `t0` owns the easing that reaches
    /// `t1`, and the key at `t1` is flat, so a preset starts a move without
    /// inheriting a curve from whatever key precedes it.
    inline void keyVector3(Track<Vector3>& track, float t0, float t1,
                           const Vector3& from, const Vector3& to, const Easing& easing) {

        track.add(t0, from, easing);
        track.add(t1, to, Easing::linear());
    }

}// namespace chrononmotion::motion::presets::detail

#endif//CHRONONMOTION_MOTION_PRESETS_DETAIL_HPP
