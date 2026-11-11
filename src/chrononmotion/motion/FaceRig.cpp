#include "chrononmotion/motion/FaceRig.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>

namespace chrononmotion::motion {
    namespace {
        [[noreturn]] void fail(const std::string& message) {
            throw std::invalid_argument("FaceRig: " + message);
        }

        const RigGeometryBinding& geometryFor(const RigProgram& program, std::uint32_t id) {
            for (const auto& geometry : program.geometry) {
                if (geometry.id == id) {
                    if (geometry.morphs.empty())
                        fail("geometry " + std::to_string(id) + " has no morph targets");
                    return geometry;
                }
            }
            fail("geometry " + std::to_string(id) + " is not in the rig program");
        }

        void validateMorphIndex(const RigGeometryBinding& geometry, std::uint32_t morph,
                                const std::string& label) {
            if (morph >= geometry.morphs.size())
                fail(label + " morph index " + std::to_string(morph) +
                     " is outside [0, " + std::to_string(geometry.morphs.size()) + ")");
        }

        float clampWeight(float value) {
            return std::clamp(value, 0.f, 1.f);
        }

        float pulse(int frame, int start, int duration, float weight) {
            if (frame < start || frame >= start + duration) return 0.f;
            if (duration <= 1) return weight;
            const float u = static_cast<float>(frame - start) /
                            static_cast<float>(duration - 1);
            return std::sin(3.14159265358979323846f * u) * weight;
        }
    }// namespace

    std::string_view visemeName(Viseme viseme) noexcept {
        switch (viseme) {
            case Viseme::Neutral: return "Neutral";
            case Viseme::AA: return "AA";
            case Viseme::E: return "E";
            case Viseme::I: return "I";
            case Viseme::O: return "O";
            case Viseme::U: return "U";
            case Viseme::M: return "M";
            case Viseme::F: return "F";
            case Viseme::L: return "L";
            case Viseme::WQ: return "WQ";
            case Viseme::TH: return "TH";
            case Viseme::S: return "S";
            case Viseme::SH: return "SH";
            default: return "Unknown";
        }
    }

    std::optional<Viseme> parseViseme(std::string_view name) noexcept {
        std::string upper;
        upper.reserve(name.size());
        for (const char value : name) {
            upper.push_back(value >= 'a' && value <= 'z' ? static_cast<char>(value - 32) : value);
        }
        if (upper == "NEUTRAL") return Viseme::Neutral;
        if (upper == "AA") return Viseme::AA;
        if (upper == "E") return Viseme::E;
        if (upper == "I") return Viseme::I;
        if (upper == "O") return Viseme::O;
        if (upper == "U") return Viseme::U;
        if (upper == "M") return Viseme::M;
        if (upper == "F") return Viseme::F;
        if (upper == "L") return Viseme::L;
        if (upper == "WQ") return Viseme::WQ;
        if (upper == "TH") return Viseme::TH;
        if (upper == "S") return Viseme::S;
        if (upper == "SH") return Viseme::SH;
        return std::nullopt;
    }

    void validateFaceRigMap(const RigProgram& program, const FaceRigMap& map) {
        if (map.geometryId == 0) fail("geometry_id must be nonzero");
        const auto& geometry = geometryFor(program, map.geometryId);
        bool any = false;
        for (std::size_t viseme = 0; viseme < kVisemeCount; ++viseme) {
            const auto& bindings = map.visemes[viseme];
            if (viseme == static_cast<std::size_t>(Viseme::Neutral)) {
                if (!bindings.empty()) fail("Neutral must not carry explicit morphs");
                continue;
            }
            std::vector<bool> used(geometry.morphs.size(), false);
            for (const auto& binding : bindings) {
                validateMorphIndex(geometry, binding.morph,
                                   std::string(visemeName(static_cast<Viseme>(viseme))));
                if (!std::isfinite(binding.weight) || binding.weight < 0.f || binding.weight > 1.f)
                    fail("viseme morph weight must be finite and in [0, 1]");
                if (used[binding.morph])
                    fail("viseme " + std::string(visemeName(static_cast<Viseme>(viseme))) +
                         " maps the same morph twice");
                used[binding.morph] = true;
                any = true;
            }
        }
        if (map.blinkMorph) {
            validateMorphIndex(geometry, *map.blinkMorph, "blink");
            any = true;
        }
        if (map.gazeLeftMorph) {
            validateMorphIndex(geometry, *map.gazeLeftMorph, "gaze-left");
            any = true;
        }
        if (map.gazeRightMorph) {
            validateMorphIndex(geometry, *map.gazeRightMorph, "gaze-right");
            any = true;
        }
        if (!any) fail("face map has no morph bindings");
    }

    void validateFacePerformance(const FacePerformance& performance, float fps) {
        if (!std::isfinite(fps) || fps <= 0.f) fail("fps must be finite and positive");
        if (performance.firstFrame < 0) fail("first_frame must be nonnegative");
        if (performance.lastFrame < performance.firstFrame)
            fail("last_frame must be greater than or equal to first_frame");
        if (performance.lastFrame > 1'000'000) fail("last_frame exceeds the frame ceiling");
        if (performance.visemes.empty() && performance.blinks.empty() && performance.gaze.empty())
            fail("performance has no cues");

        int previous = std::numeric_limits<int>::min();
        for (const auto& cue : performance.visemes) {
            if (cue.frame <= previous) fail("viseme cues must be strictly increasing by frame");
            previous = cue.frame;
            if (cue.frame < performance.firstFrame || cue.frame > performance.lastFrame)
                fail("viseme cue frame is outside the performance range");
            if (cue.viseme == Viseme::Neutral || cue.viseme == Viseme::Count)
                fail("a viseme cue must name a non-neutral viseme");
            if (!std::isfinite(cue.weight) || cue.weight < 0.f || cue.weight > 1.f)
                fail("viseme cue weight must be finite and in [0, 1]");
        }
        for (const auto& cue : performance.blinks) {
            if (cue.frame < performance.firstFrame || cue.frame > performance.lastFrame)
                fail("blink cue frame is outside the performance range");
            if (cue.durationFrames < 1) fail("blink duration must be at least one frame");
            if (static_cast<long long>(cue.frame) + cue.durationFrames - 1 >
                static_cast<long long>(performance.lastFrame))
                fail("blink cue extends past the performance range");
            if (!std::isfinite(cue.weight) || cue.weight < 0.f || cue.weight > 1.f)
                fail("blink weight must be finite and in [0, 1]");
        }
        for (const auto& cue : performance.gaze) {
            if (cue.frame < performance.firstFrame || cue.frame > performance.lastFrame)
                fail("gaze cue frame is outside the performance range");
            if (cue.durationFrames < 1) fail("gaze duration must be at least one frame");
            if (static_cast<long long>(cue.frame) + cue.durationFrames - 1 >
                static_cast<long long>(performance.lastFrame))
                fail("gaze cue extends past the performance range");
            if (!std::isfinite(cue.horizontal) || cue.horizontal < -1.f || cue.horizontal > 1.f)
                fail("gaze horizontal must be finite and in [-1, 1]");
            if (!std::isfinite(cue.weight) || cue.weight < 0.f || cue.weight > 1.f)
                fail("gaze weight must be finite and in [0, 1]");
        }
    }

    FaceRigResult applyFacePerformance(RigProgram& program, const FaceRigMap& map,
                                        const FacePerformance& performance, float fps,
                                        const FaceRigOptions& options) {
        validateFaceRigMap(program, map);
        validateFacePerformance(performance, fps);
        if (!std::isfinite(options.coarticulationSeconds) || options.coarticulationSeconds < 0.f)
            fail("coarticulation_seconds must be finite and nonnegative");

        RigGeometryBinding* geometry = nullptr;
        for (auto& candidate : program.geometry) {
            if (candidate.id == map.geometryId) {
                geometry = &candidate;
                break;
            }
        }
        if (!geometry) fail("geometry disappeared during face validation");
        const std::size_t morphCount = geometry->morphs.size();

        std::vector<std::vector<float>> peaks(
            kVisemeCount, std::vector<float>(morphCount, 0.f));
        std::vector<bool> owned(morphCount, false);
        for (std::size_t viseme = 0; viseme < kVisemeCount; ++viseme) {
            for (const auto& binding : map.visemes[viseme]) {
                peaks[viseme][binding.morph] = binding.weight;
                owned[binding.morph] = true;
            }
        }
        if (map.blinkMorph) owned[*map.blinkMorph] = true;
        if (map.gazeLeftMorph) owned[*map.gazeLeftMorph] = true;
        if (map.gazeRightMorph) owned[*map.gazeRightMorph] = true;

        const int coarticulationFrames = std::max(
            1, static_cast<int>(std::llround(options.coarticulationSeconds * fps)));
        const auto weightsFor = [&](int frame) {
            std::vector<float> result(morphCount, 0.f);
            int current = -1;
            int next = -1;
            for (std::size_t i = 0; i < performance.visemes.size(); ++i) {
                if (performance.visemes[i].frame <= frame) {
                    current = static_cast<int>(i);
                } else {
                    next = static_cast<int>(i);
                    break;
                }
            }

            // The blend window is [cue - coarticulationFrames, cue): both the
            // first cue and every later cue are anticipated, so a cue frame is
            // exact and no frame jumps from one viseme to another.
            if (next >= 0) {
                const auto& target = performance.visemes[static_cast<std::size_t>(next)];
                if (target.frame - frame < coarticulationFrames) {
                    const float amount = 1.f - static_cast<float>(target.frame - frame) /
                                                   static_cast<float>(coarticulationFrames);
                    const auto& targetPeaks = peaks[static_cast<std::size_t>(target.viseme)];
                    if (current < 0) {
                        for (std::size_t morph = 0; morph < morphCount; ++morph)
                            result[morph] = targetPeaks[morph] * target.weight * amount;
                    } else {
                        const auto& source = performance.visemes[static_cast<std::size_t>(current)];
                        const auto& sourcePeaks = peaks[static_cast<std::size_t>(source.viseme)];
                        for (std::size_t morph = 0; morph < morphCount; ++morph) {
                            const float before = sourcePeaks[morph] * source.weight;
                            const float after = targetPeaks[morph] * target.weight;
                            result[morph] = before + (after - before) * amount;
                        }
                    }
                    return result;
                }
            }

            if (current < 0) return result;
            const auto& cue = performance.visemes[static_cast<std::size_t>(current)];
            const auto& currentPeaks = peaks[static_cast<std::size_t>(cue.viseme)];
            for (std::size_t morph = 0; morph < morphCount; ++morph)
                result[morph] = currentPeaks[morph] * cue.weight;
            return result;
        };

        std::vector<Track<float>> generated(morphCount);
        std::uint64_t signature = 1469598103934665603ULL;
        const auto mix = [&signature](std::uint64_t value) {
            signature ^= value;
            signature *= 1099511628211ULL;
        };
        mix(static_cast<std::uint64_t>(performance.firstFrame));
        mix(static_cast<std::uint64_t>(performance.lastFrame));
        mix(static_cast<std::uint64_t>(std::llround(fps * 1000.f)));
        mix(static_cast<std::uint64_t>(morphCount));

        for (int frame = performance.firstFrame; frame <= performance.lastFrame; ++frame) {
            auto values = weightsFor(frame);

            float blink = 0.f;
            for (const auto& cue : performance.blinks)
                blink = std::max(blink, pulse(frame, cue.frame, cue.durationFrames, cue.weight));
            if (map.blinkMorph) values[*map.blinkMorph] += blink;

            for (const auto& cue : performance.gaze) {
                const float amount = pulse(frame, cue.frame, cue.durationFrames, cue.weight);
                const float left = std::max(0.f, cue.horizontal) * amount;
                const float right = std::max(0.f, -cue.horizontal) * amount;
                if (map.gazeLeftMorph) values[*map.gazeLeftMorph] += left;
                if (map.gazeRightMorph) values[*map.gazeRightMorph] += right;
            }

            const float seconds = static_cast<float>(frame) / fps;
            for (std::size_t morph = 0; morph < morphCount; ++morph) {
                if (!owned[morph]) continue;
                values[morph] = clampWeight(values[morph]);
                generated[morph].add(seconds, values[morph], Easing::linear());
                std::uint32_t bits = 0;
                std::memcpy(&bits, &values[morph], sizeof(bits));
                mix(static_cast<std::uint64_t>(frame));
                mix(static_cast<std::uint64_t>(morph));
                mix(bits);
            }
        }

        if (geometry->morphTracks.size() < morphCount)
            geometry->morphTracks.resize(morphCount);
        std::size_t written = 0;
        for (std::size_t morph = 0; morph < morphCount; ++morph) {
            if (!owned[morph]) continue;
            geometry->morphTracks[morph] = std::move(generated[morph]);
            ++written;
        }
        return FaceRigResult{
            written,
            performance.visemes.size(),
            performance.blinks.size(),
            performance.gaze.size(),
            signature,
        };
    }

}// namespace chrononmotion::motion
