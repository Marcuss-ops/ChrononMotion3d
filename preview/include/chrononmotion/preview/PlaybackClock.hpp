// ChrononMotion preview — the application playback clock.
//
// The motion engine already has the authority for "what is frame N": integer
// frame plus integer sub-frame units, never a float PTS. This class is only the
// presentation clock that maps wall time onto that calendar. Scrub and seek
// continue to use the same frame/sub-frame representation as ResolvedFramePlan.

#ifndef CHRONONMOTION_PREVIEW_PLAYBACKCLOCK_HPP
#define CHRONONMOTION_PREVIEW_PLAYBACKCLOCK_HPP

#include "chrononmotion/plan/ResolvedFramePlan.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <stdexcept>
#include <string>

namespace chrononmotion::preview {

    struct FrameRate {
        std::uint32_t numerator{24};
        std::uint32_t denominator{1};

        [[nodiscard]] double value() const {
            return static_cast<double>(numerator) / static_cast<double>(denominator);
        }

        [[nodiscard]] bool valid() const {
            return numerator != 0 && denominator != 0;
        }
    };

    /// Nearest rational rate whose denominator stays small. Kept local so the
    /// preview layer owns no dependency on the plan resolver's internals.
    [[nodiscard]] inline FrameRate frameRateFromDouble(double fps) {
        if (!std::isfinite(fps) || fps <= 0.0)
            throw std::invalid_argument("PlaybackClock: fps must be finite and positive");
        constexpr std::uint32_t candidates[] = {1, 2, 3, 4, 5, 10, 100, 1000, 1001, 10000};
        std::uint32_t bestNum = 0;
        std::uint32_t bestDen = 1;
        double bestError = std::numeric_limits<double>::infinity();
        for (std::uint32_t den : candidates) {
            const double num = std::round(fps * static_cast<double>(den));
            if (num <= 0.0 || num > static_cast<double>(std::numeric_limits<std::uint32_t>::max()))
                continue;
            const double error = std::fabs(num / static_cast<double>(den) - fps);
            if (error < bestError) {
                bestError = error;
                bestNum = static_cast<std::uint32_t>(num);
                bestDen = den;
            }
        }
        if (bestNum == 0) throw std::invalid_argument("PlaybackClock: fps is not representable");
        return {bestNum, bestDen};
    }

    struct FramePosition {
        int frame{0};
        /// Sub-frame offset in 1/1'000'000 of a frame, normalised to [0, 1e6).
        std::int32_t subFrameUnits{0};
        /// Derived from (frame, subFrameUnits, fps). Never the authority.
        double seconds{0.0};

        [[nodiscard]] std::int64_t absoluteUnits() const {
            return static_cast<std::int64_t>(frame) * plan::kSubFrameUnits + subFrameUnits;
        }

        bool operator==(const FramePosition&) const = default;
    };

    class PlaybackClock {
    public:
        explicit PlaybackClock(FrameRate fps = {24, 1},
                               int firstFrame = 0,
                               int frameCount = 1)
            : fps_(fps), firstFrame_(firstFrame), frameCount_(frameCount) {
            validateRate(fps_);
            validateRange(firstFrame_, frameCount_);
            playheadUnits_ = static_cast<long double>(firstFrame_) * plan::kSubFrameUnits;
        }

        explicit PlaybackClock(double fps,
                               int firstFrame = 0,
                               int frameCount = 1)
            : PlaybackClock(frameRateFromDouble(fps), firstFrame, frameCount) {}

        // --- transport
        void play() { playing_ = true; }
        void pause() { playing_ = false; }
        void toggle() { playing_ = !playing_; }
        [[nodiscard]] bool playing() const { return playing_; }

        /// Rate multiplier. Negative rates play backwards; zero freezes the
        /// playhead without changing the play/pause state.
        void setRate(double rate) {
            if (!std::isfinite(rate))
                throw std::invalid_argument("PlaybackClock: rate must be finite");
            if (std::fabs(rate) > 64.0)
                throw std::invalid_argument("PlaybackClock: |rate| must be at most 64");
            rate_ = rate;
        }
        [[nodiscard]] double rate() const { return rate_; }

        void setLoop(bool loop) { looping_ = loop; }
        [[nodiscard]] bool looping() const { return looping_; }

        [[nodiscard]] FrameRate frameRate() const { return fps_; }
        [[nodiscard]] int firstFrame() const { return firstFrame_; }
        [[nodiscard]] int frameCount() const { return frameCount_; }
        [[nodiscard]] int lastFrame() const { return firstFrame_ + frameCount_ - 1; }

        void setRange(int firstFrame, int frameCount) {
            validateRange(firstFrame, frameCount);
            firstFrame_ = firstFrame;
            frameCount_ = frameCount;
            seek(static_cast<int>(std::floor(playheadUnits_ / plan::kSubFrameUnits)),
                 static_cast<int>(playheadUnits_ - std::floor(playheadUnits_ / plan::kSubFrameUnits) *
                                                           plan::kSubFrameUnits));
        }

        /// Seek to an exact frame/sub-frame. In loop mode the value wraps into
        /// the range; otherwise it clamps. The sub-frame is normalised first.
        void seek(int frame, std::int32_t subFrameUnits = 0) {
            const std::int64_t units = normalizeUnits(
                    static_cast<std::int64_t>(frame) * plan::kSubFrameUnits + subFrameUnits);
            playheadUnits_ = clampOrWrap(units);
        }

        void seekSeconds(double seconds) {
            if (!std::isfinite(seconds))
                throw std::invalid_argument("PlaybackClock: seconds must be finite");
            const long double units = static_cast<long double>(seconds) *
                                      static_cast<long double>(fps_.numerator) /
                                      static_cast<long double>(fps_.denominator) *
                                      static_cast<long double>(plan::kSubFrameUnits);
            playheadUnits_ = clampOrWrap(static_cast<std::int64_t>(std::llround(units)));
        }

        /// Advance from an explicit wall-clock delta. This is the testable
        /// entry point; `tick()` is the convenience wrapper around steady_clock.
        FramePosition advance(std::chrono::nanoseconds wallDelta) {
            return advanceSeconds(std::chrono::duration<double>(wallDelta).count());
        }

        FramePosition advanceSeconds(double wallSeconds) {
            if (!std::isfinite(wallSeconds) || wallSeconds < 0.0)
                throw std::invalid_argument("PlaybackClock: wall delta must be finite and non-negative");
            if (!playing_ || rate_ == 0.0 || wallSeconds == 0.0) return position();

            const long double deltaUnits = static_cast<long double>(wallSeconds) *
                                           static_cast<long double>(fps_.numerator) /
                                           static_cast<long double>(fps_.denominator) *
                                           static_cast<long double>(plan::kSubFrameUnits) *
                                           static_cast<long double>(rate_);
            playheadUnits_ += deltaUnits;
            playheadUnits_ = static_cast<long double>(clampOrWrap(static_cast<std::int64_t>(std::llround(playheadUnits_))));
            return position();
        }

        /// Advance using the real monotonic clock. The first call establishes
        /// the baseline and returns the current position without advancing.
        FramePosition tick() {
            const auto now = std::chrono::steady_clock::now();
            if (!hasTickBaseline_) {
                lastTick_ = now;
                hasTickBaseline_ = true;
                return position();
            }
            const auto delta = now - lastTick_;
            lastTick_ = now;
            return advance(delta);
        }

        [[nodiscard]] FramePosition position() const {
            std::int64_t units = static_cast<std::int64_t>(std::llround(playheadUnits_));
            units = clampOrWrap(units);
            const std::int64_t frame = floorDiv(units, plan::kSubFrameUnits);
            const std::int64_t sub = units - frame * plan::kSubFrameUnits;
            return FramePosition{static_cast<int>(frame),
                                 static_cast<std::int32_t>(sub),
                                 secondsOf(frame, sub)};
        }

        [[nodiscard]] double seconds() const { return position().seconds; }

        // --- fixed-step accumulator
        void setFixedStep(double seconds) {
            if (!std::isfinite(seconds) || seconds <= 0.0)
                throw std::invalid_argument("PlaybackClock: fixed step must be finite and positive");
            fixedStep_ = seconds;
        }
        [[nodiscard]] double fixedStep() const { return fixedStep_; }

        /// Add `wallSeconds` to the fixed-step accumulator and invoke `step`
        /// once per completed step. The callback receives the step duration;
        /// the return value is the number of steps consumed. This accumulator
        /// is independent of the playhead and is meant for simulation/UI ticks.
        std::size_t advanceFixed(double wallSeconds,
                                 const std::function<void(double)>& step) {
            if (!std::isfinite(wallSeconds) || wallSeconds < 0.0)
                throw std::invalid_argument("PlaybackClock: wall delta must be finite and non-negative");
            if (!step) throw std::invalid_argument("PlaybackClock: fixed-step callback is empty");
            if (!playing_ || rate_ == 0.0) return 0;

            fixedAccumulator_ += static_cast<long double>(wallSeconds) * static_cast<long double>(rate_);
            std::size_t count = 0;
            // Guard against a pathological rate/step combination. The cap is
            // deliberately high; normal previews never reach it.
            constexpr std::size_t kMaxSteps = 1'000'000;
            while (fixedAccumulator_ >= fixedStep_ && count < kMaxSteps) {
                fixedAccumulator_ -= fixedStep_;
                step(fixedStep_);
                ++count;
            }
            if (count == kMaxSteps) fixedAccumulator_ = 0.0L;
            return count;
        }

        void resetFixedAccumulator() { fixedAccumulator_ = 0.0L; }

        /// Stable diagnostic string for tests and the overlay.
        [[nodiscard]] std::string describe() const {
            const FramePosition p = position();
            char buffer[128];
            std::snprintf(buffer, sizeof(buffer), "%d+%d/%d @ %.3f fps x%.3f%s",
                          p.frame, p.subFrameUnits, plan::kSubFrameUnits,
                          fps_.value(), rate_, looping_ ? " loop" : "");
            return buffer;
        }

    private:
        static void validateRate(FrameRate fps) {
            if (!fps.valid())
                throw std::invalid_argument("PlaybackClock: frame rate must have non-zero numerator and denominator");
        }

        static void validateRange(int firstFrame, int frameCount) {
            if (frameCount < 1)
                throw std::invalid_argument("PlaybackClock: frame count must be at least one");
            if (static_cast<std::int64_t>(firstFrame) + frameCount - 1 >
                static_cast<std::int64_t>(std::numeric_limits<int>::max()))
                throw std::invalid_argument("PlaybackClock: frame range overflows int");
        }

        static std::int64_t floorDiv(std::int64_t value, std::int64_t divisor) {
            std::int64_t q = value / divisor;
            const std::int64_t r = value % divisor;
            if (r != 0 && ((r < 0) != (divisor < 0))) --q;
            return q;
        }

        static std::int64_t normalizeUnits(std::int64_t units) {
            const std::int64_t frame = floorDiv(units, plan::kSubFrameUnits);
            return frame * plan::kSubFrameUnits +
                   (units - frame * plan::kSubFrameUnits);
        }

        [[nodiscard]] std::int64_t startUnits() const {
            return static_cast<std::int64_t>(firstFrame_) * plan::kSubFrameUnits;
        }
        [[nodiscard]] std::int64_t endUnits() const {
            return static_cast<std::int64_t>(firstFrame_ + frameCount_) * plan::kSubFrameUnits;
        }

        std::int64_t clampOrWrap(std::int64_t units) const {
            const std::int64_t start = startUnits();
            const std::int64_t end = endUnits();
            if (looping_) {
                const std::int64_t length = end - start;
                if (length <= 0) return start;
                std::int64_t offset = (units - start) % length;
                if (offset < 0) offset += length;
                return start + offset;
            }
            if (units < start) return start;
            // Inclusive last frame: the exact end is represented as the last
            // frame's final sub-frame unit, never as the next frame.
            if (units >= end) return end - 1;
            return units;
        }

        double secondsOf(std::int64_t frame, std::int64_t subFrameUnits) const {
            const long double frames = static_cast<long double>(frame) +
                                       static_cast<long double>(subFrameUnits) /
                                               static_cast<long double>(plan::kSubFrameUnits);
            return static_cast<double>(frames * static_cast<long double>(fps_.denominator) /
                                       static_cast<long double>(fps_.numerator));
        }

        FrameRate fps_{24, 1};
        int firstFrame_{0};
        int frameCount_{1};
        long double playheadUnits_{0.0L};
        double rate_{1.0};
        bool playing_{false};
        bool looping_{false};
        double fixedStep_{1.0 / 60.0};
        long double fixedAccumulator_{0.0L};
        std::chrono::steady_clock::time_point lastTick_{};
        bool hasTickBaseline_{false};
    };

}// namespace chrononmotion::preview

#endif//CHRONONMOTION_PREVIEW_PLAYBACKCLOCK_HPP
