#include "FramePlanInternal.hpp"

namespace chrononmotion::plan {

    std::uint64_t computeFramePlanDigest(const ResolvedFramePlan& plan) {

        // The digest is taken over the canonical text *without* the digest field,
        // so it is a function of the resolved values only and a consumer can
        // recompute it from what it received.
        return detail::fnv1a64(detail::canonicalFramePlanText(plan));
    }

}// namespace chrononmotion::plan
