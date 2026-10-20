#ifndef CHRONONMOTION_MOTION_CAMERASEQUENCE_HPP
#define CHRONONMOTION_MOTION_CAMERASEQUENCE_HPP

#include "chrononmotion/math/Vector2.hpp"
#include "chrononmotion/math/Vector3.hpp"
#include "chrononmotion/motion/CameraRig.hpp"

#include <vector>

namespace chrononmotion::motion::camera {

    enum class Handoff {
        PushThrough, LateralSwipe, ArcCarry, OrbitHandoff,
        RiseAndLand, FocusRack, PullBackReveal, WhipReframe
    };

    struct SequenceSubject {
        Vector3 center{0.f, 0.f, 0.f};
        float halfWidth{480.f};
        float halfHeight{270.f};
        float fov{55.f};
    };

    struct SequenceBeat {
        SequenceSubject subject{};
        int hold{90};
    };

    struct CameraSequence {
        std::vector<SequenceBeat> beats{};
        Handoff handoff{Handoff::PushThrough};
        float intensity{1.f};
        int travelFrames{24};
        int inFrame{0};
    };

    [[nodiscard]] float framingDistance(const SequenceSubject& subject,
                                        const Vector2& viewport, float fill = 0.62f);
    void applySequence(CameraRig& rig, const CameraSequence& sequence,
                       float fps, const Vector2& viewport);

}// namespace chrononmotion::motion::camera

#endif//CHRONONMOTION_MOTION_CAMERASEQUENCE_HPP
