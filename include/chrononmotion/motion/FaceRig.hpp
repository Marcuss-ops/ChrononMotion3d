// ChrononMotion — optional morph-only face performance.
//
// The body rig stays the existing RigGraph/RigProgram authority. A FaceRigMap
// names morph targets that already exist in a RigGeometryBinding; applying a
// performance only writes ordinary morphTracks, which RigProgramEvaluator
// samples before skinning/deformers. C3D therefore receives baked positions and
// normals and never needs a runtime morph or a facial bone evaluator.
#ifndef CHRONONMOTION_MOTION_FACE_RIG_HPP
#define CHRONONMOTION_MOTION_FACE_RIG_HPP

#include "chrononmotion/motion/RigBaking.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace chrononmotion::motion {

    // A deliberately small, engine-side vocabulary. It is not FACS and it does
    // not recognize speech; a TTS/aligner owns the timing that produces cues.
    enum class Viseme : std::uint8_t {
        Neutral = 0,
        AA,
        E,
        I,
        O,
        U,
        M,
        F,
        L,
        WQ,
        TH,
        S,
        SH,
        Count
    };
    inline constexpr std::size_t kVisemeCount = static_cast<std::size_t>(Viseme::Count);

    [[nodiscard]] std::string_view visemeName(Viseme viseme) noexcept;
    [[nodiscard]] std::optional<Viseme> parseViseme(std::string_view name) noexcept;

    struct FaceMorphBinding {
        std::uint32_t morph{0};
        float weight{1.f};
    };

    // Morph indices are indices into the selected RigGeometryBinding::morphs,
    // not glTF target names: the narrow motion reader intentionally does not
    // carry names. Neutral must not be mapped; it is the implicit zero pose.
    struct FaceRigMap {
        std::uint32_t geometryId{0};
        std::array<std::vector<FaceMorphBinding>, kVisemeCount> visemes{};
        std::optional<std::uint32_t> blinkMorph{};
        std::optional<std::uint32_t> gazeLeftMorph{};
        std::optional<std::uint32_t> gazeRightMorph{};
    };

    struct VisemeCue {
        int frame{0};
        Viseme viseme{Viseme::Neutral};
        float weight{1.f};
    };

    struct BlinkCue {
        int frame{0};
        int durationFrames{4};
        float weight{1.f};
    };

    struct GazeCue {
        int frame{0};
        int durationFrames{8};
        // -1 looks left, +1 looks right; 0 is neutral.
        float horizontal{0.f};
        float weight{1.f};
    };

    struct FacePerformance {
        std::vector<VisemeCue> visemes{};
        std::vector<BlinkCue> blinks{};
        std::vector<GazeCue> gaze{};
        int firstFrame{0};
        int lastFrame{-1};
    };

    struct FaceRigOptions {
        // The blend into a cue ends at the cue frame; the cue itself is exact.
        float coarticulationSeconds{0.08f};
        bool holdLastViseme{true};
    };

    struct FaceRigResult {
        std::size_t morphTracks{0};
        std::size_t visemeCues{0};
        std::size_t blinkCues{0};
        std::size_t gazeCues{0};
        std::uint64_t signature{0};
    };

    // Both functions throw std::invalid_argument with a "FaceRig:" prefix.
    void validateFaceRigMap(const RigProgram& program, const FaceRigMap& map);
    void validateFacePerformance(const FacePerformance& performance, float fps);

    // Replaces the mapped morph tracks for [firstFrame, lastFrame] with dense,
    // deterministic keys at the program's frame rate. Unmapped morph tracks are
    // left untouched. The caller must construct the RigProgramEvaluator after
    // this call; existing evaluators are snapshots by design.
    [[nodiscard]] FaceRigResult applyFacePerformance(
        RigProgram& program, const FaceRigMap& map,
        const FacePerformance& performance, float fps,
        const FaceRigOptions& options = {});

} // namespace chrononmotion::motion

#endif // CHRONONMOTION_MOTION_FACE_RIG_HPP
