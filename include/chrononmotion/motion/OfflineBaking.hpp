// ChrononMotion — deterministic offline baking utilities.
//
// These authoring-time solvers produce ordinary transforms, tracks and paths.
// They are never called by Chronon3D's frame/render path.
#ifndef CHRONONMOTION_MOTION_OFFLINE_BAKING_HPP
#define CHRONONMOTION_MOTION_OFFLINE_BAKING_HPP

#include "chrononmotion/math/Box2.hpp"
#include "chrononmotion/math/Vector2.hpp"
#include "chrononmotion/motion/Content.hpp"
#include "chrononmotion/motion/Track.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace chrononmotion::motion {

    class MotionScene;
    class Layer;

    struct VerletChainSpec {
        std::vector<Vector2> joints{}; // Initial pose; joints.front() is pinned.
        Vector2 gravity{0.f, 0.f};
        float damping{0.02f};
        unsigned iterations{8};
        float fps{60.f};
    };

    struct BakedVerletChain {
        // samples[frame][joint], and one position/rotation track per joint/segment.
        std::vector<std::vector<Vector2>> samples{};
        std::vector<Track<Vector3>> jointPositions{};
        std::vector<Track<Vector3>> segmentRotations{};
    };

    [[nodiscard]] BakedVerletChain bakeVerletChain(const VerletChainSpec& spec,
                                                    int firstFrame, int lastFrame);

    struct RigidBody2D {
        std::uint32_t id{0};
        Vector2 position{};
        Vector2 velocity{};
        float radius{1.f};
        float mass{1.f}; // mass <= 0 makes a body immovable
        float restitution{0.5f};
        float friction{0.1f};
    };

    struct RigidBodyWorld2D {
        Vector2 gravity{0.f, -9.81f};
        float floorY{0.f};
        float left{-10000.f};
        float right{10000.f};
        float fps{60.f};
        unsigned solverIterations{4};
    };

    struct BakedRigidBodies2D {
        std::vector<std::uint32_t> ids{};
        // samples[frame][body] follows the input body's order.
        std::vector<std::vector<Vector2>> samples{};
        std::vector<Track<Vector3>> positionTracks{};
    };

    [[nodiscard]] BakedRigidBodies2D bakeRigidBodies2D(
        std::vector<RigidBody2D> bodies, const RigidBodyWorld2D& world,
        int firstFrame, int lastFrame);

    struct IK2Result {
        Vector2 elbow{};
        Vector2 end{};
        float shoulderRotation{0.f}; // radians, parent-relative
        float elbowRotation{0.f};    // radians, relative bend
        bool targetReachable{false};
    };

    /// Solve a planar two-segment arm. Unreachable targets are clamped to the
    /// nearest point on the reachable annulus; result.targetReachable reports it.
    [[nodiscard]] IK2Result solveTwoSegmentIK2D(const Vector2& root,
                                                 const Vector2& target,
                                                 float upperLength,
                                                 float lowerLength,
                                                 float parentRotation = 0.f,
                                                 bool bendPositive = true);

    struct IK3Result {
        std::vector<Vector3> joints{}; // Root first, solved end effector last.
        bool targetReachable{false};
    };

    /// Deterministic FABRIK solve for an open 3D chain. `initialJoints` defines
    /// the root and fixed segment lengths; `pole` is a world-space point that
    /// selects the preferred bend plane. Unreachable targets clamp to the
    /// nearest reachable radius and are reported in targetReachable.
    [[nodiscard]] IK3Result solveIKChain3D(std::span<const Vector3> initialJoints,
                                           const Vector3& target,
                                           const Vector3& pole,
                                           unsigned iterations = 32,
                                           float tolerance = 1e-4f);

    struct IK3BakeSpec {
        std::vector<Vector3> initialJoints{};
        Track<Vector3> targetTrack{}; // Unlinked target positions, sampled in seconds.
        Vector3 pole{};               // World-space pole point.
        float fps{60.f};
        unsigned iterations{32};
        float tolerance{1e-4f};
    };

    struct BakedIKChain3D {
        // samples[frame][joint]; one world-position track for each joint.
        std::vector<std::vector<Vector3>> samples{};
        std::vector<Track<Vector3>> jointPositions{};
        std::vector<bool> targetReachable{};
    };

    /// Solve from the same bind/rest pose at every requested frame (not the
    /// previous sample), so random-access seeks and repeated bakes agree.
    [[nodiscard]] BakedIKChain3D bakeIKChain3D(const IK3BakeSpec& spec,
                                               int firstFrame, int lastFrame);

    enum class AudioFeature { Rms, Bass, Mid, Treble, Onset };
    enum class AudioDriverProperty { Position, Rotation, Scale, Opacity };
    struct AudioAnalysisSpec {
        std::uint32_t sampleRate{48000};
        std::uint32_t channels{1};
        std::size_t windowSize{1024};
        std::size_t hopSize{512};
        float bassBelowHz{250.f};
        float midBelowHz{2000.f};
        float onsetThreshold{1.5f};
        float rmsThreshold{0.01f}; // Ignore pre-existing silence below this envelope level.
    };
    struct AudioFeatureSample {
        float seconds{0.f};
        float rms{0.f};
        float bass{0.f};
        float mid{0.f};
        float treble{0.f};
        float onset{0.f};
        [[nodiscard]] float value(AudioFeature feature) const noexcept;
    };
    struct AudioDriverSpec {
        AudioFeature feature{AudioFeature::Rms};
        float multiplier{1.f};
        float bias{0.f};
        float smoothing{0.f}; // exponential smoothing time constant, seconds
    };

    /// Analyze interleaved, normalized floating-point PCM. No file I/O or decoder
    /// is part of ChrononMotion. Band levels use a deterministic windowed DFT.
    [[nodiscard]] std::vector<AudioFeatureSample> analyzePcmAudio(
        std::span<const float> interleavedPcm, const AudioAnalysisSpec& spec);
    [[nodiscard]] Track<float> bakeAudioDriverTrack(
        std::span<const AudioFeatureSample> features, const AudioDriverSpec& driver,
        float fps, int firstFrame, int lastFrame);
    /// Bake one selected feature onto one layer channel. Vector channels are
    /// sampled from their existing track and receive an additive axis driver;
    /// opacity is likewise additive. The selected layer track is replaced by
    /// the dense frame-sampled result for the requested range.
    void bakeAudioDriverToLayer(Layer& layer, std::span<const AudioFeatureSample> features,
                                const AudioDriverSpec& driver, AudioDriverProperty property,
                                const Vector3& axis, float fps, int firstFrame, int lastFrame);

    enum class FlowAxis { Row, Column };
    enum class FlowMainAlignment { Start, Center, End, SpaceBetween };
    enum class FlowCrossAlignment { Start, Center, End, Stretch };
    struct FlowInsets { float top{0.f}; float right{0.f}; float bottom{0.f}; float left{0.f}; };
    struct FlowSpec {
        FlowAxis axis{FlowAxis::Row};
        Vector2 containerSize{}; // zero on an axis means intrinsic content size
        FlowInsets padding{};
        float gap{0.f};
        FlowMainAlignment mainAlignment{FlowMainAlignment::Start};
        FlowCrossAlignment crossAlignment{FlowCrossAlignment::Start};
    };
    struct FlowItem {
        std::uint32_t id{0};
        Vector2 measuredSize{};
        Vector2 anchor{0.5f, 0.5f};
        std::string measurementFingerprint{};
    };
    struct FlowItemResult {
        std::uint32_t id{0};
        Vector2 position{}; // absolute position of the content anchor
        Vector2 anchor{};   // local anchor in content units
        Vector2 size{};
        Box2 bounds{};      // absolute top-left to bottom-right bounds
    };
    struct FlowResult {
        std::vector<FlowItemResult> items{};
        Box2 bounds{};
    };
    [[nodiscard]] FlowResult solveFlowLayout(const FlowSpec& spec,
                                              std::span<const FlowItem> items,
                                              const Vector2& origin = Vector2{});
    /// Refresh the content measurement (including its measurement fingerprint),
    /// rerun layout, and write the resolved anchor positions/sizes into matching
    /// layers' static transforms. An empty fingerprint explicitly marks the new
    /// measurement as unverified rather than retaining a stale fingerprint.
    [[nodiscard]] FlowResult refreshFlowLayout(const FlowSpec& spec,
                                                std::span<const FlowItem> items,
                                                MotionScene& scene,
                                                const Vector2& origin = Vector2{});

    enum class BakedPathCommandType { MoveTo, CubicTo, Close };
    struct BakedPathCommand {
        BakedPathCommandType type{BakedPathCommandType::MoveTo};
        Vector2 point{};
        Vector2 control1{};
        Vector2 control2{};
    };
    using BakedPath = std::vector<BakedPathCommand>;

    /// Parse SVG M/L/H/V/C/S/Q/T/A/Z commands, including relative forms, and
    /// normalize all drawing segments to cubic Béziers for topology-compatible
    /// morphing. Invalid or unsupported input fails with std::invalid_argument.
    [[nodiscard]] BakedPath parseSvgPath(std::string_view pathData);
    [[nodiscard]] BakedPath interpolateBakedPaths(const BakedPath& from,
                                                   const BakedPath& to,
                                                   float amount);
    [[nodiscard]] std::vector<BakedPath> bakeSvgPathMorph(
        std::string_view from, std::string_view to, int firstFrame, int lastFrame);

    struct RepeaterSpec {
        std::uint32_t copies{1};
        Vector2 position{0.f, 0.f}; // per-copy translation
        Vector2 scale{1.f, 1.f};    // per-copy scale
        float rotationRadians{0.f}; // per-copy rotation
        Vector2 anchor{};
    };
    [[nodiscard]] std::vector<BakedPath> expandPathRepeater(
        const BakedPath& path, const RepeaterSpec& repeater,
        std::uint32_t maximumCopies = 10000);

} // namespace chrononmotion::motion

#endif // CHRONONMOTION_MOTION_OFFLINE_BAKING_HPP