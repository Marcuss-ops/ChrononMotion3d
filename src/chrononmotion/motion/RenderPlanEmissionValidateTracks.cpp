// Baked-track write contract of the emitter.

#include "RenderPlanEmissionInternal.hpp"

#include <chronon3d/render_plan/color_utils.hpp>
#include <chronon3d/effects/effect_param_animation.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cmath>
#include <limits>
#include <cstddef>
#include <initializer_list>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace chrononmotion::motion::detail {
using chronon3d::render_plan::AnimationKeyframePlan;
using chronon3d::render_plan::AnimationPlan;
using chronon3d::render_plan::AnimationTrackPlan;
using chronon3d::render_plan::CameraPlan;
using chronon3d::render_plan::FitMode;
using chronon3d::render_plan::InlineMeshPlan;
using chronon3d::render_plan::LayerPlan;
using chronon3d::render_plan::LayerStylePlan;
using chronon3d::render_plan::LayerType;
using chronon3d::render_plan::OutputFormat;
using chronon3d::render_plan::PathOperatorKindPlan;
using chronon3d::render_plan::RenderBudget;
using chronon3d::render_plan::RenderPlan;
using chronon3d::render_plan::ShapeContentPlan;
using chronon3d::render_plan::ShapeContentTypePlan;
using chronon3d::render_plan::ShapePathCommandPlan;
using chronon3d::render_plan::ShapePathCommandTypePlan;

        /// Everything a track may carry that the lowering bakes away instead of
        /// publishing: a baked track is linear samples with no per-key timing and
        /// no driver, so one of these showing up means the plan came from
        /// somewhere this writer has not been taught.
        [[nodiscard]] std::string trackProblem(const std::string& owner, const AnimationTrackPlan& track) {

            if (!isKnownEasing(track.easing)) {
                return owner + " declares easing '" + track.easing + "', which the plan contract does not define";
            }
            if (track.bezier_curve) return owner + " carries a track-level bezier curve";
            if (track.spring) return owner + " carries a track spring";
            if (track.loop_mode != "none") return owner + " carries loop mode '" + track.loop_mode + "'";
            if (track.wiggle || track.oscillate || track.random_hold || track.posterize_time ||
                track.data_feed || track.audio_react) {
                return owner + " carries an animation driver, which the lowering bakes into samples";
            }
            for (const AnimationKeyframePlan& key : track.keyframes) {
                if (key.easing || key.interpolation || key.easing_xy || key.bezier_curve || key.spring ||
                    key.roving || key.temporal_in_dx || key.temporal_in_dy ||
                    key.temporal_out_dx || key.temporal_out_dy) {
                    return owner + " carries per-key timing overrides, which the lowering bakes into samples";
                }
            }
            return {};
        }

        [[nodiscard]] std::string animationProblem(const std::string& owner, const AnimationPlan& animation) {

            for (const AnimationTrackPlan& track : animation.tracks) {
                const std::string problem = trackProblem(owner + " track '" + track.property + "'", track);
                if (!problem.empty()) return problem;
            }
            return {};
        }


} // namespace chrononmotion::motion::detail
