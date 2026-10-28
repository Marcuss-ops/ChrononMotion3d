#ifndef CHRONONMOTION_MOTION_CAMERAFOCUSSEQUENCE_HPP
#define CHRONONMOTION_MOTION_CAMERAFOCUSSEQUENCE_HPP

#include "chrononmotion/math/Vector2.hpp"
#include "chrononmotion/math/Vector3.hpp"
#include "chrononmotion/motion/CameraRig.hpp"

#include <vector>

namespace chrononmotion::motion::camera {

    struct FocusSubjectKey {
        Vector3 anchor{0.f, 0.f, 0.f};
        float focusDepth{0.f};
        int focusFrame{0};
    };

    /// Track the camera across successive subjects while moving the focus plane
    /// along the optical axis. Template clients supply the subject anchors and
    /// editorial timing; CM3D resolves all camera/lens channels.
    struct FocusSequence {
        std::vector<FocusSubjectKey> subjects{};
        Vector2 cameraAimOffset{0.f, 0.f};
        float cameraDistance{1080.f};
        float baseFov{48.f};
        float focusFov{38.f};
        float fStop{5.6f};
        float maxBlur{10.f};
        /// 0 = plain circular bokeh (what every DOF kernel in Chronon3d
        /// implements). Any nonzero gain (or a nonzero `irisBlades`) implies
        /// shaped bokeh, which no consumer kernel supports yet: the render
        /// boundary rejects it fail-closed (reject_unsupported_dof_shape), so
        /// the sequence default must stay "not authored" instead of silently
        /// arming a lens the renderer cannot realize.
        float highlightGain{0.f};
        int focusTravelFrames{24};
        int irisBlades{0};
    };

    void applyFocusSequence(CameraRig& rig, const FocusSequence& sequence, float fps);

    /// Resolve and key optical-axis focus from the evaluated camera target.
    void applyTargetFocusTrack(CameraRig& rig, int firstFrame, int lastFrame, float fps);

    /// Rack focus across a world-Z plane while accounting for camera orientation.
    /// The plane eases from startPlaneZ to endPlaneZ after transitionFrame.
    void applyZPlaneFocusTrack(CameraRig& rig, int firstFrame, int lastFrame,
                               int transitionFrame, float startPlaneZ, float endPlaneZ,
                               float fps);

}// namespace chrononmotion::motion::camera

#endif//CHRONONMOTION_MOTION_CAMERAFOCUSSEQUENCE_HPP
