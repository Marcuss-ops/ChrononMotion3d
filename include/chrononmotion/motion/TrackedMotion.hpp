#ifndef CHRONONMOTION_MOTION_TRACKEDMOTION_HPP
#define CHRONONMOTION_MOTION_TRACKEDMOTION_HPP

// TrackedMotion — ingesting externally tracked points as motion keyframes.
//
// A point tracker is pixel analysis and lives on the content side; this header
// only sees (time, position, confidence) values and turns them into the same
// keyed channel every other motion source writes. No content machinery of any
// kind crosses the boundary.

#include "chrononmotion/math/Vector3.hpp"
#include "chrononmotion/motion/Track.hpp"

#include <vector>

namespace chrononmotion::motion {

    /// One tracked point sample: time in seconds, planar position, and the
    /// tracker's confidence in [0, 1].
    struct TrackedPoint {
        float time{0.f};
        float x{0.f};
        float y{0.f};
        float confidence{1.f};
    };

    /// Builds a Track<Vector3> (z = 0) from tracked point samples. Samples
    /// below minConfidence are skipped — the track interpolates across them —
    /// so a lost patch cannot inject a spike into the motion. Keys are ordered
    /// by insertion, so callers must feed samples in time order (a tracker
    /// always produces them in that order).
    inline Track<Vector3> trackFromPoints(const std::vector<TrackedPoint>& points,
                                          float minConfidence = 0.25f) {

        Track<Vector3> track;
        for (const auto& point : points) {
            if (point.confidence < minConfidence) continue;
            track.add(point.time, Vector3(point.x, point.y, 0.f));
        }
        return track;
    }

}// namespace chrononmotion::motion

#endif//CHRONONMOTION_MOTION_TRACKEDMOTION_HPP
