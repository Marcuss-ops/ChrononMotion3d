// Unit test: clip-sequence crossfade weights (walk -> talk style sequences).
//
// Checks that segment weights are a linear crossfade: each incoming segment
// ramps 0 -> 1 over its fadeIn window, the outgoing segment ramps 1 -> 0 over
// the same window, weights sum to 1 inside the transition, and the final
// segment fades out at its endSeconds.

#include "threepp/motion/MotionBridge.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {

using Segment = threepp::motion::MotionSceneBridge::ClipSegment;
using threepp::motion::MotionSceneBridge;

int failures = 0;

void expectNear(const char* what, float actual, float expected) {
    if (std::fabs(actual - expected) > 1e-4f) {
        ++failures;
        std::cerr << "FAIL " << what << ": expected " << expected << " got " << actual << "\n";
    }
}

} // namespace

int main() {
    // seg0: plays 0..2s, no fade-in. seg1: starts at 2s, 1s crossfade, ends at 5s.
    std::vector<Segment> segs(2);
    segs[0].startSeconds = 0.f;
    segs[0].endSeconds = 2.f;
    segs[0].fadeInSeconds = 0.f;
    segs[1].startSeconds = 2.f;
    segs[1].endSeconds = 5.f;
    segs[1].fadeInSeconds = 1.f;

    auto w = [&](std::size_t i, float t) { return MotionSceneBridge::clipSequenceWeight(segs, i, t); };

    // Before the transition: only seg0 plays.
    expectNear("t=0 seg0", w(0, 0.f), 1.f);
    expectNear("t=0 seg1", w(1, 0.f), 0.f);
    expectNear("t=1 seg0", w(0, 1.f), 1.f);
    expectNear("t=1 seg1", w(1, 1.f), 0.f);

    // Mid transition: linear crossfade, weights sum to 1.
    expectNear("t=2.25 seg0", w(0, 2.25f), 0.75f);
    expectNear("t=2.25 seg1", w(1, 2.25f), 0.25f);
    expectNear("t=2.5 seg0", w(0, 2.5f), 0.5f);
    expectNear("t=2.5 seg1", w(1, 2.5f), 0.5f);
    expectNear("t=2.75 seg0", w(0, 2.75f), 0.25f);
    expectNear("t=2.75 seg1", w(1, 2.75f), 0.75f);
    for (float t = 2.f; t <= 3.f; t += 0.05f) {
        expectNear("sum in transition", w(0, t) + w(1, t), 1.f);
    }

    // After the transition: only seg1 plays, and it fades out at endSeconds.
    expectNear("t=3 seg0", w(0, 3.f), 0.f);
    expectNear("t=3 seg1", w(1, 3.f), 1.f);
    expectNear("t=4.9 seg1", w(1, 4.9f), 1.f);
    expectNear("t=5 seg1 (end)", w(1, 5.f), 0.f);

    // Out-of-range index is inert.
    expectNear("bad index", w(7, 2.5f), 0.f);

    if (failures == 0) {
        std::cout << "clip_sequence_weights: all checks passed\n";
        return EXIT_SUCCESS;
    }
    std::cerr << failures << " check(s) failed\n";
    return EXIT_FAILURE;
}
