// ChrononMotion — the resolved frame plan (ADR-CAMERA-PIPELINE-OWNERSHIP gate 1).
//
// This is the boundary the ADR asks for: a fully materialised, versioned,
// immutable description of every frame the backend will execute. It is a value
// type on purpose — no callbacks, no pointers into a MotionScene, no authoring
// tracks, no policy that a backend would have to re-derive. Motion resolves the
// scene once; whoever executes it only reads numbers.
//
// Contract: docs/RESOLVED_FRAME_PLAN.md, contracts/chronon.resolved-frame-plan.v1.json.

#ifndef CHRONONMOTION_PLAN_RESOLVEDFRAMEPLAN_HPP
#define CHRONONMOTION_PLAN_RESOLVEDFRAMEPLAN_HPP

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace chrononmotion::motion {
    class MotionScene;
    class CameraRig;
}

namespace chrononmotion::plan {

    /// Wire identity. A consumer accepts exactly this schema string; a plan with
    /// any other value is a different contract, not a compatible one.
    inline constexpr std::string_view kResolvedFramePlanSchema =
        "chrononmotion.resolved-frame-plan.v1";

    /// Coordinate space of every position/orientation/matrix in the plan.
    /// Motion's own authoring space: right-handed, Y up, -Z forward, no unit
    /// conversion and no mirroring applied by the producer.
    inline constexpr std::string_view kResolvedFramePlanSpace =
        "chrononmotion-world/right-handed/y-up/-z-forward/motion-units";

    /// Sub-frame time is an integer multiple of 1/kSubFrameUnits of a frame, not
    /// a float. Two producers asking for the same instant therefore land on the
    /// same timestamp bit for bit, which is what lets the camera, the layers and
    /// the shutter share one calendar instead of three nearly-equal ones.
    inline constexpr std::int32_t kSubFrameUnits = 1'000'000;

    /// The single timestamp formula of this contract.
    ///
    /// `frame` may be negative (a centred shutter on frame zero opens before
    /// t=0). The result is derived from exact integers and the rational frame
    /// rate, so `seconds` never becomes the authority: it is a projection of
    /// (frame, subFrameUnits) that a consumer can recompute and verify.
    [[nodiscard]] inline double planSeconds(int frame, std::int32_t subFrameUnits,
                                            std::uint32_t fpsNumerator,
                                            std::uint32_t fpsDenominator) {
        const double frames = static_cast<double>(frame) +
                              static_cast<double>(subFrameUnits) / static_cast<double>(kSubFrameUnits);
        return frames * static_cast<double>(fpsDenominator) / static_cast<double>(fpsNumerator);
    }

    struct PlanTimestamp {
        int frame{0};
        /// Sub-frame offset in 1/1'000'000 of a frame, normalised to [0, 1e6).
        std::int32_t subFrameUnits{0};
        /// Derived from (frame, subFrameUnits, fps). Validated, never authored.
        double seconds{0.0};

        [[nodiscard]] std::int64_t absoluteUnits() const {
            return static_cast<std::int64_t>(frame) * kSubFrameUnits + subFrameUnits;
        }
        bool operator==(const PlanTimestamp&) const = default;
    };

    /// Resolved optics. Every value is what the backend executes, not what the
    /// author typed: `focalLengthMm == 0` means "field of view wins" and is
    /// carried explicitly instead of being inferred later.
    struct PlanLens {
        float focalLengthMm{0.f};
        float sensorWidthMm{0.f};
        float sensorHeightMm{0.f};
        std::array<float, 2> lensShift{0.f, 0.f};
        float fStop{0.f};
        /// Camera-space distance along the optical axis, in motion units.
        float focusDistance{0.f};
        bool operator==(const PlanLens&) const = default;
    };

    struct PlanCameraSample {
        PlanTimestamp time{};
        std::array<float, 3> position{0.f, 0.f, 0.f};
        /// Quaternion as (x, y, z, w), normalised, already aimed and rolled.
        std::array<float, 4> orientation{0.f, 0.f, 0.f, 1.f};
        float fovDeg{50.f};
        PlanLens lens{};
        bool operator==(const PlanCameraSample&) const = default;
    };

    struct PlanLayerSample {
        PlanTimestamp time{};
        /// Row-major 4x4 world matrix in `kResolvedFramePlanSpace`.
        std::array<float, 16> world{1.f, 0.f, 0.f, 0.f,
                                    0.f, 1.f, 0.f, 0.f,
                                    0.f, 0.f, 1.f, 0.f,
                                    0.f, 0.f, 0.f, 1.f};
        /// World opacity after the parent chain, in [0, 1].
        float opacity{1.f};
        /// Lifetime/visibility resolved at this timestamp.
        bool visible{true};
        bool operator==(const PlanLayerSample&) const = default;
    };

    enum class PlanLayerKind {
        Null,   ///< Controller layer: carries a transform, draws nothing.
        Text,
        Image,
        Video,
        Mesh
    };

    [[nodiscard]] std::string_view planLayerKindName(PlanLayerKind kind);

    struct PlanLayer {
        std::uint32_t id{0};
        std::string name{};
        PlanLayerKind kind{PlanLayerKind::Null};
        /// Opaque content identity owned by Chronon; empty only for Null.
        std::string asset{};
        /// Measurement fingerprint the content rectangle was taken from.
        std::string fingerprint{};
        std::string baseColorTexture{};
        std::string metallicRoughnessTexture{};
        std::uint32_t parent{0};
        /// Resolution order: ascending, dense from zero, equal to list position.
        int order{0};
        std::vector<PlanLayerSample> samples{};
    };

    struct PlanShutter {
        float angleDegrees{180.f};
        /// Shutter window sampled at `offsetUnits.size()` instants.
        std::vector<std::int32_t> offsetUnits{};
    };

    struct ResolvedFramePlan {
        std::string schema{std::string(kResolvedFramePlanSchema)};
        std::string space{std::string(kResolvedFramePlanSpace)};
        std::string planId{};

        std::uint32_t width{0};
        std::uint32_t height{0};
        std::uint32_t fpsNumerator{0};
        std::uint32_t fpsDenominator{1};
        int firstFrame{0};
        int frameCount{0};

        /// The single calendar. Camera and layer samples reference it by index.
        std::vector<PlanTimestamp> timeline{};
        std::optional<PlanShutter> shutter{};

        std::vector<PlanCameraSample> camera{};
        std::vector<PlanLayer> layers{};

        /// FNV-1a-64 over every resolved value, stamped by the producer.
        std::uint64_t digest{0};
    };

    /// Typed rejection reasons. A backend that cannot execute a field reports
    /// one of these; nothing is silently repaired or defaulted.
    enum class FramePlanProblem {
        None,
        Schema,
        Space,
        Canvas,
        FrameRate,
        TimelineEmpty,
        TimelineSize,
        TimelineOrder,
        TimelineSubFrame,
        TimelineSeconds,
        ShutterShape,
        ShutterOffsets,
        ShutterCoverage,
        CameraCount,
        CameraTime,
        CameraPose,
        CameraOptics,
        LayerOrder,
        LayerParent,
        LayerKind,
        LayerAsset,
        LayerSamples,
        LayerTimestamp,
        LayerTransform,
        LayerOpacity,
        Digest
    };

    [[nodiscard]] std::string_view framePlanProblemName(FramePlanProblem problem);

    struct FramePlanDiagnostic {
        FramePlanProblem code{FramePlanProblem::None};
        /// Human-readable location and value ("camera[3].fov_deg is 0").
        std::string detail{};
    };

    /// Full contract check. Returns the first violation, or nullopt.
    [[nodiscard]] std::optional<FramePlanDiagnostic> verifyFramePlan(const ResolvedFramePlan& plan);

    /// Stable content digest of every resolved value. Independent of the stored
    /// `digest` field, so a consumer can detect tampering.
    [[nodiscard]] std::uint64_t computeFramePlanDigest(const ResolvedFramePlan& plan);

    struct ResolveFramePlanOptions {
        std::uint32_t width{1920};
        std::uint32_t height{1080};
        int firstFrame{0};
        int frameCount{1};
        /// A claim about the rate, not an override. `0` asks the producer to
        /// derive the rate from the scene; any other pair must match it (within
        /// 1e-4 relative) or the plan is rejected, because two rates in one plan
        /// is how a frame lands half a frame off in one consumer.
        std::uint32_t fpsNumerator{0};
        std::uint32_t fpsDenominator{1};
        /// When the scene authors a shutter, resolve its sub-samples too.
        bool includeShutterSamples{true};
        /// Controller layers carry the transforms that place their children.
        bool includeNullLayers{true};
        std::string planId{};
    };

    /// Nearest rational rate whose denominator stays small; 24 -> 24/1,
    /// 29.97 -> 30000/1001. Deterministic and independent of the platform's fmt.
    [[nodiscard]] std::pair<std::uint32_t, std::uint32_t> rationalFrameRate(float fps);

    /// Resolve a scene and its camera into the plan. The scene is evaluated at
    /// each timeline timestamp (evaluation is a pure function of scene+time), so
    /// the returned plan keeps no reference to it.
    [[nodiscard]] ResolvedFramePlan resolveFramePlan(motion::MotionScene& scene,
                                                     const motion::CameraRig& camera,
                                                     const ResolveFramePlanOptions& options = {});

    /// Deterministic JSON. Throws std::invalid_argument("resolved frame plan: ...")
    /// when `verifyFramePlan` rejects the plan.
    [[nodiscard]] std::string resolvedFramePlanToJson(const ResolvedFramePlan& plan,
                                                      int indentSpaces = 2);

} // namespace chrononmotion::plan

#endif
