#ifndef CHRONONMOTION_MOTION_DOCUMENTARYCAMERAPATH_HPP
#define CHRONONMOTION_MOTION_DOCUMENTARYCAMERAPATH_HPP

#include "chrononmotion/math/Quaternion.hpp"
#include "chrononmotion/math/Vector2.hpp"
#include "chrononmotion/math/Vector3.hpp"
#include "chrononmotion/motion/CameraRig.hpp"

#include <vector>

namespace chrononmotion::motion::camera {

enum class DocumentaryMove {
    SnapDown, PullbackReveal, PushThroughSnapshot, WhipToPhoto, FocusDrop,
    Reveal90, CornerTurn, ForegroundPhotoPass, PhotoStack, FilmstripHandoff,
    SplitDepth, ArchiveCraneReveal
};

struct DocumentaryCameraAnchor {
    Vector3 center{0.f, 0.f, 0.f};
    Quaternion orientation{};
    float halfWidth{300.f};
    float halfHeight{120.f};
};

/// Scene-resolved input for a documentary handoff. CM3D owns the trajectory,
/// camera timing, and focus evaluation; clients retain only editorial layers.
struct DocumentaryCameraPath {
    DocumentaryMove move{DocumentaryMove::SnapDown};
    DocumentaryCameraAnchor title{};
    std::vector<DocumentaryCameraAnchor> snapshots{};
    DocumentaryCameraAnchor finalTarget{};
    float cameraDistance{1080.f};
    float fov{50.f};
    Vector2 viewport{1920.f, 1080.f};
    int startFrame{0};
    int handoffFrame{45};
    int endFrame{100};
    int transitionFrames{8};
};

void applyDocumentaryCameraPath(CameraRig& rig, const DocumentaryCameraPath& path, float fps);

} // namespace chrononmotion::motion::camera

#endif
