// ChrononMotion — internal helpers of the resolved frame plan contract.
//
// One renderer, used by both the JSON document and the content digest, so the
// bytes a consumer parses and the bytes the digest covers cannot diverge.

#ifndef CHRONONMOTION_PLAN_FRAMEPLANINTERNAL_HPP
#define CHRONONMOTION_PLAN_FRAMEPLANINTERNAL_HPP

#include "chrononmotion/plan/ResolvedFramePlan.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace chrononmotion::plan::detail {

    /// Render the plan. With `includeDigest` false the output is the canonical
    /// form the digest is taken over (indent is the caller's and does not change
    /// the digest, which always uses indent 0).
    [[nodiscard]] std::string renderFramePlanText(const ResolvedFramePlan& plan, int indent,
                                                  bool includeDigest);

    /// FNV-1a-64 over the canonical form. A plain hash is enough here: it is a
    /// change detector for one producer/consumer pair, not a signature.
    [[nodiscard]] std::uint64_t fnv1a64(std::string_view bytes);

    /// The canonical (indent-free, digest-free) text of a plan.
    [[nodiscard]] inline std::string canonicalFramePlanText(const ResolvedFramePlan& plan) {
        return renderFramePlanText(plan, 0, false);
    }

} // namespace chrononmotion::plan::detail

#endif
