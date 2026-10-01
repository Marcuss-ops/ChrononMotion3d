// ChrononMotion — Camera Motion V1: authoritative shot-to-shot system.
//
// Architecture:
//   ShotAnchor -> CameraShot -> CameraTransition -> CameraRig -> position/target/FOV/focus/roll
//
// 15 authoritative motion recipes:
//   1.  camera_focus_push
//   2.  camera_rack_focus_duo
//   3.  camera_dolly_focus_handoff
//   4.  camera_lateral_subject_to_subject
//   5.  camera_orbit_90_reveal
//   6.  camera_corner_turn_reveal
//   7.  camera_detach_turn_reacquire
//   8.  camera_foreground_pass_reveal
//   9.  camera_depth_peel
//   10. camera_arc_between_subjects
//   11. camera_whip_pan_handoff
//   12. camera_crane_up_reveal
//   13. camera_push_through_overlay
//   14. camera_parallax_gallery
//   15. camera_focus_orbit_settle

#ifndef CHRONONMOTION_MOTION_CAMERAMOTION_HPP
#define CHRONONMOTION_MOTION_CAMERAMOTION_HPP

#include "chrononmotion/motion/CameraRig.hpp"
#include "chrononmotion/motion/Easing.hpp"
#include "chrononmotion/motion/Layer.hpp"

#include "chrononmotion/math/Matrix4.hpp"
#include "chrononmotion/math/Quaternion.hpp"
#include "chrononmotion/math/Vector2.hpp"
#include "chrononmotion/math/Vector3.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace chrononmotion::motion {

    class MotionScene;

    // ------------------------------------------------------------- ShotAnchor

    enum class AnchorKind {
        WorldPoint,
        Layer,
        Image,
        Text,
        Entity,
        MapPoint,
        WebCard,
        Shape
    };

    /// An anchor point in the scene: an entity layer (image, text, card, shape, etc.)
    /// with an optional local-space offset, or an arbitrary world-space point.
    struct ShotAnchor {
        AnchorKind kind{AnchorKind::WorldPoint};
        Layer::Id layerId{Layer::kRoot};
        Vector3 localPoint{0.f, 0.f, 0.f};
        Vector3 worldPoint{0.f, 0.f, 0.f};

        ShotAnchor() = default;

        explicit ShotAnchor(const Vector3& world, AnchorKind k = AnchorKind::WorldPoint)
            : kind(k), worldPoint(world) {}

        ShotAnchor(Layer::Id id, const Vector3& local = Vector3(0.f, 0.f, 0.f), AnchorKind k = AnchorKind::Layer)
            : kind(k), layerId(id), localPoint(local) {}

        ShotAnchor(const Layer& layer, const Vector3& local = Vector3(0.f, 0.f, 0.f), AnchorKind k = AnchorKind::Layer)
            : kind(k), layerId(layer.id), localPoint(local) {}

        // Semantic factory methods
        [[nodiscard]] static ShotAnchor world(const Vector3& p) { return ShotAnchor(p, AnchorKind::WorldPoint); }
        [[nodiscard]] static ShotAnchor layer(Layer::Id id, const Vector3& local = Vector3(0.f, 0.f, 0.f)) {
            return ShotAnchor(id, local, AnchorKind::Layer);
        }
        [[nodiscard]] static ShotAnchor image(Layer::Id id, const Vector3& local = Vector3(0.f, 0.f, 0.f)) {
            return ShotAnchor(id, local, AnchorKind::Image);
        }
        [[nodiscard]] static ShotAnchor text(Layer::Id id, const Vector3& local = Vector3(0.f, 0.f, 0.f)) {
            return ShotAnchor(id, local, AnchorKind::Text);
        }
        [[nodiscard]] static ShotAnchor entity(Layer::Id id, const Vector3& local = Vector3(0.f, 0.f, 0.f)) {
            return ShotAnchor(id, local, AnchorKind::Entity);
        }
        [[nodiscard]] static ShotAnchor mapPoint(Layer::Id id, const Vector3& local = Vector3(0.f, 0.f, 0.f)) {
            return ShotAnchor(id, local, AnchorKind::MapPoint);
        }
        [[nodiscard]] static ShotAnchor webCard(Layer::Id id, const Vector3& local = Vector3(0.f, 0.f, 0.f)) {
            return ShotAnchor(id, local, AnchorKind::WebCard);
        }
        [[nodiscard]] static ShotAnchor shape(Layer::Id id, const Vector3& local = Vector3(0.f, 0.f, 0.f)) {
            return ShotAnchor(id, local, AnchorKind::Shape);
        }

        /// Resolve the 3D world position of this anchor at time `seconds`.
        [[nodiscard]] Vector3 resolve(const MotionScene* scene = nullptr, float seconds = 0.f) const;
        [[nodiscard]] Vector3 resolve(const Layer* l, float seconds = 0.f) const;
    };

    // ------------------------------------------------------------- CameraShot

    enum class Framing {
        Custom,
        ExtremeCloseUp,
        CloseUp,
        MediumCloseUp,
        Medium,
        MediumFull,
        Wide,
        ExtremeWide
    };

    enum class CameraFocusMode {
        Manual,
        TargetDistance,
        BlendTargets,
        ForegroundSubject
    };

    /// Distance multiplier or default world distance for named framing.
    [[nodiscard]] inline float framingDistance(Framing framing) {
        switch (framing) {
            case Framing::ExtremeCloseUp: return 2.5f;
            case Framing::CloseUp:        return 5.0f;
            case Framing::MediumCloseUp:  return 7.5f;
            case Framing::Medium:         return 10.0f;
            case Framing::MediumFull:     return 14.0f;
            case Framing::Wide:           return 18.0f;
            case Framing::ExtremeWide:    return 28.0f;
            case Framing::Custom:         return 10.0f;
        }
        return 10.0f;
    }

    /// Authoring description of how a subject/anchor is framed by the camera.
    struct CameraShot {
        ShotAnchor target{};
        Framing framing{Framing::Medium};
        float distance{10.f}; // If <= 0, uses framingDistance(framing)
        float fov{60.f};
        CameraFocusMode focusMode{CameraFocusMode::TargetDistance};
        float focusDistance{0.f};
        float aperture{0.f};
        float fStop{0.f};
        float roll{0.f};      // radians
        float azimuth{0.f};   // radians yaw around target (0 = front along +Z)
        float elevation{0.f}; // radians pitch above/below target (>0 = high angle)

        /// Authored aspect ratio of the delivery viewport (width / height).
        /// 0 means "not authored": the rig's own aspect wins. Portrait formats
        /// (aspect < 1) shrink the horizontal coverage at constant vertical
        /// FOV, so the optical distance is compensated to keep the subject
        /// inside the narrower axis — the multi-aspect framing law.
        float aspect{0.f};

        /// Optional safety margin (0..0.9) of the framing axis spared on each
        /// side, consumed by the aspect compensation. 0 disables it.
        float safeMargin{0.f};

        CameraShot& withTarget(const ShotAnchor& a) { target = a; return *this; }
        CameraShot& withFraming(Framing f) { framing = f; distance = framingDistance(f); return *this; }
        CameraShot& withDistance(float d) { distance = d; return *this; }
        CameraShot& withFov(float deg) { fov = deg; return *this; }
        CameraShot& withFocus(CameraFocusMode mode, float dist = 0.f, float ap = 0.f, float fstop = 0.f) {
            focusMode = mode;
            focusDistance = dist;
            aperture = ap;
            fStop = fstop;
            return *this;
        }
        CameraShot& withAngles(float azimRad, float elevRad) { azimuth = azimRad; elevation = elevRad; return *this; }
        CameraShot& withRoll(float rad) { roll = rad; return *this; }

        /// Author the framing for a delivery aspect ratio (width / height),
        /// with an optional safe-area margin fraction (e.g. 0.12 for TikTok UI
        /// zones). See `CameraShot::aspect`.
        CameraShot& withAspect(float widthOverHeight, float margin01 = 0.f) {
            aspect = widthOverHeight;
            safeMargin = margin01;
            return *this;
        }

        /// Sample the camera pose required to achieve this shot at `seconds`.
        [[nodiscard]] CameraPose evaluatePose(const MotionScene* scene = nullptr, float seconds = 0.f) const;
    };

    // --------------------------------------------------- CameraFocusResolver

    class CameraFocusResolver {
    public:
        [[nodiscard]] static float resolveFocus(CameraFocusMode mode,
                                               const Vector3& cameraPos,
                                               const Vector3& targetA,
                                               const Vector3& targetB,
                                               float blendProgress = 0.f,
                                               float manualDistance = 0.f);

        [[nodiscard]] static float resolveFocus(CameraFocusMode mode,
                                               const Vector3& cameraPos,
                                               const Vector3& target,
                                               float manualDistance = 0.f);
    };

    // ------------------------------------------------------ CameraTransition

    enum class TransitionPath {
        Linear,
        Arc,
        Orbit,
        Dolly,
        Corner,
        Spline
    };

    enum class OrientationMode {
        LookAtTarget,
        InterpolateOrientation,
        Whip,
        DetachReacquire
    };

    struct CameraTransition {
        CameraShot fromShot{};
        CameraShot toShot{};
        int inFrame{0};
        int duration{30};
        TransitionPath path{TransitionPath::Linear};
        OrientationMode orientationMode{OrientationMode::LookAtTarget};
        CameraFocusMode focusMode{CameraFocusMode::TargetDistance};
        Easing easing{Easing::easeInOut()};

        // Specific path parameters
        float cornerRadius{2.f};
        Vector3 cornerPoint{0.f, 0.f, 0.f};
        Vector3 arcApex{0.f, 0.f, 0.f};
        Vector3 orbitPivot{0.f, 0.f, 0.f};
        float orbitAngleRad{1.57079632679f}; // 90 degrees

        /// Apply this transition into the camera rig's tracks.
        void apply(CameraRig& rig, const MotionScene* scene = nullptr, float fps = 30.f) const;
    };

    // ------------------------------------------------- 15 Authoritative Recipes

    /// 1. camera_focus_push: Progressive push onto subject; DOF increases, target centered.
    void cameraFocusPush(CameraRig& rig, const ShotAnchor& subject, int inFrame, int duration,
                         float pushDistance, float startAperture = 0.f, float endAperture = 12.f,
                         float fps = 30.f, const Easing& easing = Easing::easeInOut(),
                         const MotionScene* scene = nullptr);

    /// 2. camera_rack_focus_duo: Pure rack focus between two visible subjects; camera stays in place.
    void cameraRackFocusDuo(CameraRig& rig, const ShotAnchor& subjectA, const ShotAnchor& subjectB,
                            int inFrame, int duration, float aperture = 14.f, float fps = 30.f,
                            const Easing& easing = Easing::easeInOut(),
                            const MotionScene* scene = nullptr);

    /// 3. camera_dolly_focus_handoff: Dolly sideways/forward while transferring focus from A to B.
    void cameraDollyFocusHandoff(CameraRig& rig, const ShotAnchor& subjectA, const ShotAnchor& subjectB,
                                 int inFrame, int duration, const Vector3& dollyDelta = Vector3(2.f, 0.f, -1.f),
                                 float aperture = 10.f, float fps = 30.f,
                                 const Easing& easing = Easing::easeInOut(),
                                 const MotionScene* scene = nullptr);

    /// 4. camera_lateral_subject_to_subject: Horizontal 3D camera travel between subjects A and B.
    void cameraLateralSubjectToSubject(CameraRig& rig, const ShotAnchor& subjectA, const ShotAnchor& subjectB,
                                       int inFrame, int duration, float fps = 30.f,
                                       const Easing& easing = Easing::easeInOut(),
                                       const MotionScene* scene = nullptr);

    /// 5. camera_orbit_90_reveal: 90 degree orbit around pivot, revealing subject B on the other side.
    void cameraOrbit90Reveal(CameraRig& rig, const ShotAnchor& subjectA, const ShotAnchor& subjectB,
                             const Vector3& pivot, int inFrame, int duration, float fps = 30.f,
                             const Easing& easing = Easing::easeInOut(),
                             const MotionScene* scene = nullptr);

    /// 6. camera_corner_turn_reveal: Move forward, turn 90 deg around rounded corner, find subject B.
    void cameraCornerTurnReveal(CameraRig& rig, const ShotAnchor& subjectA, const ShotAnchor& subjectB,
                                const Vector3& cornerPoint, int inFrame, int duration,
                                float cornerRadius = 2.f, float fps = 30.f,
                                const Easing& easing = Easing::easeInOut(),
                                const MotionScene* scene = nullptr);

    /// 7. camera_detach_turn_reacquire: Locked on A, detaches, turns freely, acquires and locks onto B.
    void cameraDetachTurnReacquire(CameraRig& rig, const ShotAnchor& subjectA, const ShotAnchor& subjectB,
                                   int inFrame, int duration, float fps = 30.f,
                                   const Easing& easing = Easing::easeInOut(),
                                   const MotionScene* scene = nullptr);

    /// 8. camera_foreground_pass_reveal: Camera passes close foreground A to reveal background subject B.
    void cameraForegroundPassReveal(CameraRig& rig, const ShotAnchor& foregroundA, const ShotAnchor& subjectB,
                                    int inFrame, int duration,
                                    const Vector3& lateralPassOffset = Vector3(3.f, 0.f, -4.f),
                                    float aperture = 14.f, float fps = 30.f,
                                    const Easing& easing = Easing::easeInOut(),
                                    const MotionScene* scene = nullptr);

    /// 9. camera_depth_peel: Camera travels along Z through sequentially stacked subjects.
    void cameraDepthPeel(CameraRig& rig, const std::vector<ShotAnchor>& subjects,
                         int inFrame, int duration, float pushPerSubject = 4.f,
                         float aperture = 12.f, float fps = 30.f,
                         const Easing& easing = Easing::easeInOut(),
                         const MotionScene* scene = nullptr);

    /// 10. camera_arc_between_subjects: Elegant curved arc travel between subjects A and B.
    void cameraArcBetweenSubjects(CameraRig& rig, const ShotAnchor& subjectA, const ShotAnchor& subjectB,
                                  int inFrame, int duration, float arcBowDistance = 3.f,
                                  float fps = 30.f, const Easing& easing = Easing::easeInOut(),
                                  const MotionScene* scene = nullptr);

    /// 11. camera_whip_pan_handoff: High-speed rotational whip from A to B with zero residual drift.
    void cameraWhipPanHandoff(CameraRig& rig, const ShotAnchor& subjectA, const ShotAnchor& subjectB,
                              int inFrame, int duration, float fps = 30.f,
                              const MotionScene* scene = nullptr);

    /// 12. camera_crane_up_reveal: Low close-up on A cranes up and back with widening FOV to reveal context.
    void cameraCraneUpReveal(CameraRig& rig, const ShotAnchor& subjectA, const ShotAnchor& contextAnchor,
                             int inFrame, int duration, float craneUp = 6.f, float craneBack = 8.f,
                             float startFov = 40.f, float endFov = 65.f, float fps = 30.f,
                             const Easing& easing = Easing::easeInOut(),
                             const MotionScene* scene = nullptr);

    /// 13. camera_push_through_overlay: Camera pushes through overlay/card A into scene B.
    void cameraPushThroughOverlay(CameraRig& rig, const ShotAnchor& cardA, const ShotAnchor& sceneB,
                                  int inFrame, int duration, float passThroughDistance = 12.f,
                                  float fps = 30.f, const Easing& easing = Easing::easeInOut(),
                                  const MotionScene* scene = nullptr);

    /// 14. camera_parallax_gallery: Lateral travel past cards at staggered depths with dynamic focus.
    void cameraParallaxGallery(CameraRig& rig, const std::vector<ShotAnchor>& cards,
                               int inFrame, int duration, float cameraZOffset = 8.f,
                               float aperture = 10.f, float fps = 30.f,
                               const MotionScene* scene = nullptr);

    /// 15. camera_focus_orbit_settle: Frontal view with subtle orbit sweep (-10 to +15 deg) settling on subject.
    void cameraFocusOrbitSettle(CameraRig& rig, const ShotAnchor& subject,
                                int inFrame, int duration,
                                float yawSweepDeg = 15.f, float pitchSweepDeg = 6.f,
                                float pushDistance = 2.f, float fps = 30.f,
                                const MotionScene* scene = nullptr);

    // ------------------------------------------------- 15 NEW Ultra-Cinematic Recipes (V2 Suite)

    /// 16. cameraVertigoDollyZoom: Hitchcock vertigo effect; camera dollies while FOV counter-adjusts so subject size is constant.
    void cameraVertigoDollyZoom(CameraRig& rig, const ShotAnchor& subject, int inFrame, int duration,
                                float startDistance, float endDistance, float startFov = 45.f,
                                float fps = 30.f, const Easing& easing = Easing::easeInOut(),
                                const MotionScene* scene = nullptr);

    /// 17. cameraBulletTimeOrbitFreeze: High-speed 360-degree cylindrical orbital sweep around frozen subject in mid-air.
    void cameraBulletTimeOrbitFreeze(CameraRig& rig, const ShotAnchor& subject, int inFrame, int duration,
                                     float radius = 7.f, float height = 1.2f, float sweepDegrees = 360.f,
                                     float aperture = 16.f, float fps = 30.f, const Easing& easing = Easing::easeInOut(),
                                     const MotionScene* scene = nullptr);

    /// 18. cameraFpvDroneDiveRoll: Dynamic FPV dive from high altitude with 45-60 deg axial banking roll and low surface recovery.
    void cameraFpvDroneDiveRoll(CameraRig& rig, const ShotAnchor& approachAnchor, const ShotAnchor& targetSubject,
                                int inFrame, int duration, float diveElevation = 10.f, float maxBankRollDeg = 55.f,
                                float fps = 30.f, const Easing& easing = Easing::easeInOut(),
                                const MotionScene* scene = nullptr);

    /// 19. cameraSplitDiopterDualPlane: Emulates optical split-diopter framing with dual-plane focus handoff & lateral micro-dolly.
    void cameraSplitDiopterDualPlane(CameraRig& rig, const ShotAnchor& foregroundLeft, const ShotAnchor& backgroundRight,
                                     int inFrame, int duration, float aperture = 18.f, float fps = 30.f,
                                     const Easing& easing = Easing::easeInOut(), const MotionScene* scene = nullptr);

    /// 20. cameraSpiralHelixAscend: Double-helix orbital elevator; camera spirals upward around central vertical hierarchy.
    void cameraSpiralHelixAscend(CameraRig& rig, const ShotAnchor& centerPillar, int inFrame, int duration,
                                 float climbHeight = 10.f, float radius = 8.f, float totalTurns = 1.25f,
                                 float fps = 30.f, const Easing& easing = Easing::easeInOut(),
                                 const MotionScene* scene = nullptr);

    /// 21. cameraFlyUnderReveal: Drops below floor level, skims underneath foreground entity, pitches up dramatically to reveal B.
    void cameraFlyUnderReveal(CameraRig& rig, const ShotAnchor& foregroundObstacle, const ShotAnchor& heroTarget,
                              int inFrame, int duration, float subFloorDepth = -3.5f, float fps = 30.f,
                              const Easing& easing = Easing::easeInOut(), const MotionScene* scene = nullptr);

    /// 22. cameraAnamorphicBarrelSweep: Cinematic 2.39:1 widescreen pan with anamorphic focal length adjustments and wide lateral sweep.
    void cameraAnamorphicBarrelSweep(CameraRig& rig, const ShotAnchor& anchorLeft, const ShotAnchor& anchorRight,
                                     int inFrame, int duration, float sweepSpanX = 14.f, float fps = 30.f,
                                     const Easing& easing = Easing::easeInOut(), const MotionScene* scene = nullptr);

    /// 23. cameraCorkscrewThroughRing: 360-degree axial corkscrew roll while plunging forward through a satellite cluster ring.
    void cameraCorkscrewThroughRing(CameraRig& rig, const ShotAnchor& ringCenter, const ShotAnchor& interiorCore,
                                    int inFrame, int duration, float plungeDistance = 14.f, float fps = 30.f,
                                    const Easing& easing = Easing::easeInOut(), const MotionScene* scene = nullptr);

    /// 24. cameraPendulumSwingReveal: Physics-based heavy jib arm pendulum swing; swoops from top-left, curves through nadir to top-right.
    void cameraPendulumSwingReveal(CameraRig& rig, const ShotAnchor& startAnchor, const ShotAnchor& endAnchor,
                                   int inFrame, int duration, float pendulumDrop = 4.5f, float fps = 30.f,
                                   const Easing& easing = Easing::easeInOut(), const MotionScene* scene = nullptr);

    /// 25. cameraCrashZoomSnap: Tarantino / Edgar Wright hyper-fast snap push with chromatic velocity burst and micro-spring settle.
    void cameraCrashZoomSnap(CameraRig& rig, const ShotAnchor& heroSubject, int inFrame, int duration,
                             float startDistance = 12.f, float endDistance = 2.8f, float fps = 30.f,
                             const MotionScene* scene = nullptr);

    /// 26. cameraInfinityMirrorDive: Exponential acceleration plunge along Z through receding translucent portal frames.
    void cameraInfinityMirrorDive(CameraRig& rig, const std::vector<ShotAnchor>& portalFrames, int inFrame, int duration,
                                  float totalZTravel = 24.f, float fps = 30.f, const Easing& easing = Easing::easeInOut(),
                                  const MotionScene* scene = nullptr);

    /// 27. cameraOverTheShoulderDialogue: Reverse-angle dialogue move; frames B past A's shoulder, then sweeps 180 deg to frame A past B.
    void cameraOverTheShoulderDialogue(CameraRig& rig, const ShotAnchor& subjectA, const ShotAnchor& subjectB,
                                       int inFrame, int duration, float shoulderOffset = 2.2f, float fps = 30.f,
                                       const Easing& easing = Easing::easeInOut(), const MotionScene* scene = nullptr);

    /// 28. cameraBirdsEyeGodViewDescent: Starts at 90 deg top-down nadir architectural view, dives down and pitches up to eye-level.
    void cameraBirdsEyeGodViewDescent(CameraRig& rig, const ShotAnchor& groundSubject, int inFrame, int duration,
                                      float godViewHeight = 22.f, float eyeLevelDistance = 7.5f, float fps = 30.f,
                                      const Easing& easing = Easing::easeInOut(), const MotionScene* scene = nullptr);

    /// 29. cameraSlalomCardWeave: Carves alternating slalom S-curves between staggered left/right monolithic cards with roll banking.
    void cameraSlalomCardWeave(CameraRig& rig, const std::vector<ShotAnchor>& slalomGates, int inFrame, int duration,
                               float slalomAmplitude = 4.5f, float maxBankRollDeg = 18.f, float fps = 30.f,
                               const Easing& easing = Easing::easeInOut(), const MotionScene* scene = nullptr);

    /// 30. cameraHeroLowAngleMonolithOrbit: Low floor-level perspective looking upward (+28 deg pitch) orbiting a towering monolith.
    void cameraHeroLowAngleMonolithOrbit(CameraRig& rig, const ShotAnchor& monolith, int inFrame, int duration,
                                         float radius = 6.5f, float floorLevelY = -2.8f, float orbitSweepDeg = 65.f,
                                         float upwardPitchDeg = 26.f, float fps = 30.f,
                                         const Easing& easing = Easing::easeInOut(), const MotionScene* scene = nullptr);

    // ------------------------------------------------ Snake-case Official Aliases

    inline void camera_focus_push(CameraRig& r, const ShotAnchor& s, int f, int d, float p, float sa = 0.f, float ea = 12.f, float fps = 30.f, const Easing& e = Easing::easeInOut(), const MotionScene* sc = nullptr) {
        cameraFocusPush(r, s, f, d, p, sa, ea, fps, e, sc);
    }
    inline void camera_rack_focus_duo(CameraRig& r, const ShotAnchor& a, const ShotAnchor& b, int f, int d, float ap = 14.f, float fps = 30.f, const Easing& e = Easing::easeInOut(), const MotionScene* sc = nullptr) {
        cameraRackFocusDuo(r, a, b, f, d, ap, fps, e, sc);
    }
    inline void camera_dolly_focus_handoff(CameraRig& r, const ShotAnchor& a, const ShotAnchor& b, int f, int d, const Vector3& dd = Vector3(2.f, 0.f, -1.f), float ap = 10.f, float fps = 30.f, const Easing& e = Easing::easeInOut(), const MotionScene* sc = nullptr) {
        cameraDollyFocusHandoff(r, a, b, f, d, dd, ap, fps, e, sc);
    }
    inline void camera_lateral_subject_to_subject(CameraRig& r, const ShotAnchor& a, const ShotAnchor& b, int f, int d, float fps = 30.f, const Easing& e = Easing::easeInOut(), const MotionScene* sc = nullptr) {
        cameraLateralSubjectToSubject(r, a, b, f, d, fps, e, sc);
    }
    inline void camera_orbit_90_reveal(CameraRig& r, const ShotAnchor& a, const ShotAnchor& b, const Vector3& p, int f, int d, float fps = 30.f, const Easing& e = Easing::easeInOut(), const MotionScene* sc = nullptr) {
        cameraOrbit90Reveal(r, a, b, p, f, d, fps, e, sc);
    }
    inline void camera_corner_turn_reveal(CameraRig& r, const ShotAnchor& a, const ShotAnchor& b, const Vector3& c, int f, int d, float rad = 2.f, float fps = 30.f, const Easing& e = Easing::easeInOut(), const MotionScene* sc = nullptr) {
        cameraCornerTurnReveal(r, a, b, c, f, d, rad, fps, e, sc);
    }
    inline void camera_detach_turn_reacquire(CameraRig& r, const ShotAnchor& a, const ShotAnchor& b, int f, int d, float fps = 30.f, const Easing& e = Easing::easeInOut(), const MotionScene* sc = nullptr) {
        cameraDetachTurnReacquire(r, a, b, f, d, fps, e, sc);
    }
    inline void camera_foreground_pass_reveal(CameraRig& r, const ShotAnchor& a, const ShotAnchor& b, int f, int d, const Vector3& off = Vector3(3.f, 0.f, -4.f), float ap = 14.f, float fps = 30.f, const Easing& e = Easing::easeInOut(), const MotionScene* sc = nullptr) {
        cameraForegroundPassReveal(r, a, b, f, d, off, ap, fps, e, sc);
    }
    inline void camera_depth_peel(CameraRig& r, const std::vector<ShotAnchor>& s, int f, int d, float push = 4.f, float ap = 12.f, float fps = 30.f, const Easing& e = Easing::easeInOut(), const MotionScene* sc = nullptr) {
        cameraDepthPeel(r, s, f, d, push, ap, fps, e, sc);
    }
    inline void camera_arc_between_subjects(CameraRig& r, const ShotAnchor& a, const ShotAnchor& b, int f, int d, float bow = 3.f, float fps = 30.f, const Easing& e = Easing::easeInOut(), const MotionScene* sc = nullptr) {
        cameraArcBetweenSubjects(r, a, b, f, d, bow, fps, e, sc);
    }
    inline void camera_whip_pan_handoff(CameraRig& r, const ShotAnchor& a, const ShotAnchor& b, int f, int d, float fps = 30.f, const MotionScene* sc = nullptr) {
        cameraWhipPanHandoff(r, a, b, f, d, fps, sc);
    }
    inline void camera_crane_up_reveal(CameraRig& r, const ShotAnchor& a, const ShotAnchor& ctx, int f, int d, float up = 6.f, float back = 8.f, float sfov = 40.f, float efov = 65.f, float fps = 30.f, const Easing& e = Easing::easeInOut(), const MotionScene* sc = nullptr) {
        cameraCraneUpReveal(r, a, ctx, f, d, up, back, sfov, efov, fps, e, sc);
    }
    inline void camera_push_through_overlay(CameraRig& r, const ShotAnchor& a, const ShotAnchor& b, int f, int d, float dist = 12.f, float fps = 30.f, const Easing& e = Easing::easeInOut(), const MotionScene* sc = nullptr) {
        cameraPushThroughOverlay(r, a, b, f, d, dist, fps, e, sc);
    }
    inline void camera_parallax_gallery(CameraRig& r, const std::vector<ShotAnchor>& cards, int f, int d, float zoff = 8.f, float ap = 10.f, float fps = 30.f, const MotionScene* sc = nullptr) {
        cameraParallaxGallery(r, cards, f, d, zoff, ap, fps, sc);
    }
    inline void camera_focus_orbit_settle(CameraRig& r, const ShotAnchor& s, int f, int d, float yaw = 15.f, float pitch = 6.f, float push = 2.f, float fps = 30.f, const MotionScene* sc = nullptr) {
        cameraFocusOrbitSettle(r, s, f, d, yaw, pitch, push, fps, sc);
    }

    // V2 Aliases
    inline void camera_vertigo_dolly_zoom(CameraRig& r, const ShotAnchor& s, int f, int d, float sd, float ed, float sf = 45.f, float fps = 30.f, const Easing& e = Easing::easeInOut(), const MotionScene* sc = nullptr) {
        cameraVertigoDollyZoom(r, s, f, d, sd, ed, sf, fps, e, sc);
    }
    inline void camera_bullet_time_orbit_freeze(CameraRig& r, const ShotAnchor& s, int f, int d, float rad = 7.f, float h = 1.2f, float sw = 360.f, float ap = 16.f, float fps = 30.f, const Easing& e = Easing::easeInOut(), const MotionScene* sc = nullptr) {
        cameraBulletTimeOrbitFreeze(r, s, f, d, rad, h, sw, ap, fps, e, sc);
    }
    inline void camera_fpv_drone_dive_roll(CameraRig& r, const ShotAnchor& a, const ShotAnchor& t, int f, int d, float div = 10.f, float rll = 55.f, float fps = 30.f, const Easing& e = Easing::easeInOut(), const MotionScene* sc = nullptr) {
        cameraFpvDroneDiveRoll(r, a, t, f, d, div, rll, fps, e, sc);
    }
    inline void camera_split_diopter_dual_plane(CameraRig& r, const ShotAnchor& fl, const ShotAnchor& br, int f, int d, float ap = 18.f, float fps = 30.f, const Easing& e = Easing::easeInOut(), const MotionScene* sc = nullptr) {
        cameraSplitDiopterDualPlane(r, fl, br, f, d, ap, fps, e, sc);
    }
    inline void camera_spiral_helix_ascend(CameraRig& r, const ShotAnchor& cp, int f, int d, float ch = 10.f, float rad = 8.f, float turns = 1.25f, float fps = 30.f, const Easing& e = Easing::easeInOut(), const MotionScene* sc = nullptr) {
        cameraSpiralHelixAscend(r, cp, f, d, ch, rad, turns, fps, e, sc);
    }
    inline void camera_fly_under_reveal(CameraRig& r, const ShotAnchor& fg, const ShotAnchor& ht, int f, int d, float dep = -3.5f, float fps = 30.f, const Easing& e = Easing::easeInOut(), const MotionScene* sc = nullptr) {
        cameraFlyUnderReveal(r, fg, ht, f, d, dep, fps, e, sc);
    }
    inline void camera_anamorphic_barrel_sweep(CameraRig& r, const ShotAnchor& al, const ShotAnchor& ar, int f, int d, float sp = 14.f, float fps = 30.f, const Easing& e = Easing::easeInOut(), const MotionScene* sc = nullptr) {
        cameraAnamorphicBarrelSweep(r, al, ar, f, d, sp, fps, e, sc);
    }
    inline void camera_corkscrew_through_ring(CameraRig& r, const ShotAnchor& rc, const ShotAnchor& ic, int f, int d, float pl = 14.f, float fps = 30.f, const Easing& e = Easing::easeInOut(), const MotionScene* sc = nullptr) {
        cameraCorkscrewThroughRing(r, rc, ic, f, d, pl, fps, e, sc);
    }
    inline void camera_pendulum_swing_reveal(CameraRig& r, const ShotAnchor& sa, const ShotAnchor& ea, int f, int d, float dr = 4.5f, float fps = 30.f, const Easing& e = Easing::easeInOut(), const MotionScene* sc = nullptr) {
        cameraPendulumSwingReveal(r, sa, ea, f, d, dr, fps, e, sc);
    }
    inline void camera_crash_zoom_snap(CameraRig& r, const ShotAnchor& hs, int f, int d, float sd = 12.f, float ed = 2.8f, float fps = 30.f, const MotionScene* sc = nullptr) {
        cameraCrashZoomSnap(r, hs, f, d, sd, ed, fps, sc);
    }
    inline void camera_infinity_mirror_dive(CameraRig& r, const std::vector<ShotAnchor>& pf, int f, int d, float zt = 24.f, float fps = 30.f, const Easing& e = Easing::easeInOut(), const MotionScene* sc = nullptr) {
        cameraInfinityMirrorDive(r, pf, f, d, zt, fps, e, sc);
    }
    inline void camera_over_the_shoulder_dialogue(CameraRig& r, const ShotAnchor& sa, const ShotAnchor& sb, int f, int d, float sh = 2.2f, float fps = 30.f, const Easing& e = Easing::easeInOut(), const MotionScene* sc = nullptr) {
        cameraOverTheShoulderDialogue(r, sa, sb, f, d, sh, fps, e, sc);
    }
    inline void camera_birds_eye_god_view_descent(CameraRig& r, const ShotAnchor& gs, int f, int d, float gh = 22.f, float ed = 7.5f, float fps = 30.f, const Easing& e = Easing::easeInOut(), const MotionScene* sc = nullptr) {
        cameraBirdsEyeGodViewDescent(r, gs, f, d, gh, ed, fps, e, sc);
    }
    inline void camera_slalom_card_weave(CameraRig& r, const std::vector<ShotAnchor>& sg, int f, int d, float amp = 4.5f, float bnk = 18.f, float fps = 30.f, const Easing& e = Easing::easeInOut(), const MotionScene* sc = nullptr) {
        cameraSlalomCardWeave(r, sg, f, d, amp, bnk, fps, e, sc);
    }
    inline void camera_hero_low_angle_monolith_orbit(CameraRig& r, const ShotAnchor& m, int f, int d, float rad = 6.5f, float fy = -2.8f, float sw = 65.f, float pitch = 26.f, float fps = 30.f, const Easing& e = Easing::easeInOut(), const MotionScene* sc = nullptr) {
        cameraHeroLowAngleMonolithOrbit(r, m, f, d, rad, fy, sw, pitch, fps, e, sc);
    }

    namespace presets {
        using motion::cameraFocusPush;
        using motion::cameraRackFocusDuo;
        using motion::cameraDollyFocusHandoff;
        using motion::cameraLateralSubjectToSubject;
        using motion::cameraOrbit90Reveal;
        using motion::cameraCornerTurnReveal;
        using motion::cameraDetachTurnReacquire;
        using motion::cameraForegroundPassReveal;
        using motion::cameraDepthPeel;
        using motion::cameraArcBetweenSubjects;
        using motion::cameraWhipPanHandoff;
        using motion::cameraCraneUpReveal;
        using motion::cameraPushThroughOverlay;
        using motion::cameraParallaxGallery;
        using motion::cameraFocusOrbitSettle;

        using motion::cameraVertigoDollyZoom;
        using motion::cameraBulletTimeOrbitFreeze;
        using motion::cameraFpvDroneDiveRoll;
        using motion::cameraSplitDiopterDualPlane;
        using motion::cameraSpiralHelixAscend;
        using motion::cameraFlyUnderReveal;
        using motion::cameraAnamorphicBarrelSweep;
        using motion::cameraCorkscrewThroughRing;
        using motion::cameraPendulumSwingReveal;
        using motion::cameraCrashZoomSnap;
        using motion::cameraInfinityMirrorDive;
        using motion::cameraOverTheShoulderDialogue;
        using motion::cameraBirdsEyeGodViewDescent;
        using motion::cameraSlalomCardWeave;
        using motion::cameraHeroLowAngleMonolithOrbit;

        using motion::camera_focus_push;
        using motion::camera_rack_focus_duo;
        using motion::camera_dolly_focus_handoff;
        using motion::camera_lateral_subject_to_subject;
        using motion::camera_orbit_90_reveal;
        using motion::camera_corner_turn_reveal;
        using motion::camera_detach_turn_reacquire;
        using motion::camera_foreground_pass_reveal;
        using motion::camera_depth_peel;
        using motion::camera_arc_between_subjects;
        using motion::camera_whip_pan_handoff;
        using motion::camera_crane_up_reveal;
        using motion::camera_push_through_overlay;
        using motion::camera_parallax_gallery;
        using motion::camera_focus_orbit_settle;
    }

}// namespace chrononmotion::motion

#endif//CHRONONMOTION_MOTION_CAMERAMOTION_HPP
