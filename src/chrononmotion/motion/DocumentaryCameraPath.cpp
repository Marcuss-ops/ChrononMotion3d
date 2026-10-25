#include "chrononmotion/motion/DocumentaryCameraPath.hpp"
#include "chrononmotion/math/Finite.hpp"

#include "chrononmotion/motion/CameraFocusSequence.hpp"
#include "chrononmotion/motion/CameraFraming.hpp"
#include "chrononmotion/motion/CameraRecipe.hpp"
#include "chrononmotion/motion/CameraFraming.hpp"
#include "chrononmotion/motion/Easing.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

namespace chrononmotion::motion::camera {
namespace {

constexpr std::array<DocumentaryMove, 12> kDocumentaryMoves{
    DocumentaryMove::SnapDown, DocumentaryMove::PullbackReveal,
    DocumentaryMove::PushThroughSnapshot, DocumentaryMove::WhipToPhoto,
    DocumentaryMove::FocusDrop, DocumentaryMove::Reveal90,
    DocumentaryMove::CornerTurn, DocumentaryMove::ForegroundPhotoPass,
    DocumentaryMove::PhotoStack, DocumentaryMove::FilmstripHandoff,
    DocumentaryMove::SplitDepth, DocumentaryMove::ArchiveCraneReveal
};

float at(int frame, float fps) { return static_cast<float>(frame) / fps; }

Vector3 cameraAt(const DocumentaryCameraAnchor& anchor, float distance) {
    Vector3 normal(0.f, 0.f, 1.f);
    normal.applyQuaternion(anchor.orientation);
    normal.normalize();
    return {anchor.center.x + normal.x * distance,
            anchor.center.y + normal.y * distance,
            anchor.center.z + normal.z * distance};
}

void validate(const DocumentaryCameraPath& path, float fps) {
    const auto finiteAnchor = [](const DocumentaryCameraAnchor& a) {
        return math::isFinite(a.center) &&
               std::isfinite(a.halfWidth) && std::isfinite(a.halfHeight) &&
               a.halfWidth > 0.f && a.halfHeight > 0.f &&
               std::isfinite(a.orientation.x) && std::isfinite(a.orientation.y) &&
               std::isfinite(a.orientation.z) && std::isfinite(a.orientation.w) &&
               a.orientation.lengthSq() > 1e-8f;
    };
    if (!finiteAnchor(path.title) || !finiteAnchor(path.finalTarget) || path.snapshots.empty() ||
        !std::all_of(path.snapshots.begin(), path.snapshots.end(), finiteAnchor) ||
        !std::isfinite(path.cameraDistance) || path.cameraDistance <= 1.f ||
        !std::isfinite(path.fov) || path.fov <= 0.f || path.fov >= 180.f ||
        !std::isfinite(path.viewport.x) || !std::isfinite(path.viewport.y) ||
        path.viewport.x <= 0.f || path.viewport.y <= 0.f ||
        !std::isfinite(fps) || fps <= 0.f || path.startFrame < 0 ||
        path.handoffFrame <= path.startFrame || path.endFrame <= path.handoffFrame ||
        path.transitionFrames < 2 || path.transitionFrames > path.endFrame - path.handoffFrame) {
        throw std::invalid_argument("applyDocumentaryCameraPath: invalid anchors, optics, or timing");
    }
}

} // namespace

std::span<const DocumentaryMove> documentaryMoves() noexcept { return kDocumentaryMoves; }

std::string_view documentaryMoveId(DocumentaryMove move) noexcept {
    switch (move) {
        case DocumentaryMove::SnapDown: return "doc_title_snap_down";
        case DocumentaryMove::PullbackReveal: return "doc_title_pullback_photo_reveal";
        case DocumentaryMove::PushThroughSnapshot: return "doc_title_push_through_snapshot";
        case DocumentaryMove::WhipToPhoto: return "doc_title_whip_to_photo";
        case DocumentaryMove::FocusDrop: return "doc_title_focus_drop";
        case DocumentaryMove::Reveal90: return "doc_title_90_reveal";
        case DocumentaryMove::CornerTurn: return "doc_title_corner_turn";
        case DocumentaryMove::ForegroundPhotoPass: return "doc_title_foreground_photo_pass";
        case DocumentaryMove::PhotoStack: return "doc_title_photo_stack";
        case DocumentaryMove::FilmstripHandoff: return "doc_title_filmstrip_handoff";
        case DocumentaryMove::SplitDepth: return "doc_title_split_depth";
        case DocumentaryMove::ArchiveCraneReveal: return "doc_archive_crane_reveal";
    }
    return "doc_title_unknown";
}

void applyDocumentaryCameraPath(CameraRig& rig, const DocumentaryCameraPath& path, float fps) {
    validate(path, fps);
    const int start = path.startFrame;
    const int handoff = path.handoffFrame;
    const int end = path.endFrame;
    const int transitionLength = end - handoff;
    const int mid = handoff + transitionLength / 2;
    const int fastTransitionEnd = handoff + path.transitionFrames;
    const int revealTurnMid = handoff + path.transitionFrames / 2;
    const auto& first = path.snapshots.front();
    const auto& last = path.snapshots.back();
    const Vector3 titleCam = cameraAt(path.title, path.cameraDistance);
    float finalDistance = path.cameraDistance;
    if (path.move == DocumentaryMove::ArchiveCraneReveal ||
        path.move == DocumentaryMove::PhotoStack) {
        const float minimumDistance = path.move == DocumentaryMove::PhotoStack
            ? path.cameraDistance * 1.25f : path.cameraDistance;
        const float fitDistance = CameraFramingSolver::distanceForBounds(
            path.finalTarget.halfWidth, path.finalTarget.halfHeight, 0.f,
            path.fov, path.viewport, CameraSafeArea::full()) * 1.10f;
        finalDistance = std::max(minimumDistance, fitDistance);
    }
    const Vector3 photoCam = cameraAt(path.finalTarget, finalDistance);
    const float closeDistance = path.cameraDistance * 0.58f;

    Vector3 openingTarget = path.title.center;
    float openingDistance = path.cameraDistance;
    if (path.move == DocumentaryMove::FocusDrop) {
        openingTarget = (path.title.center + first.center) * 0.5f;
        const float verticalExtent = std::fabs(path.title.center.y - first.center.y) * 0.5f +
                                     std::max(path.title.halfHeight, first.halfHeight);
        openingDistance = std::max(path.cameraDistance,
            CameraFramingSolver::distanceForBounds(0.f, verticalExtent * 0.5f, 0.f,
                path.fov, path.viewport, CameraSafeArea::full()) * 1.06f);
    }
    const Vector3 openingCamera = path.move == DocumentaryMove::PullbackReveal
        ? cameraAt(path.title, closeDistance)
        : path.move == DocumentaryMove::FocusDrop
            ? cameraAt(DocumentaryCameraAnchor{.center = openingTarget}, openingDistance)
            : titleCam;

    std::vector<CameraTrackWaypoint> keys;
    const auto point = [&](int frame, const Vector3& position, const Vector3& target,
                           const Easing& easing = Easing::easeInOut()) {
        keys.push_back({frame, position, target, easing});
    };
    rig.setPosition(titleCam).setTarget(path.title.center).setFov(path.fov);
    point(start, openingCamera, openingTarget, Easing::linear());

    switch (path.move) {
        case DocumentaryMove::SnapDown: {
            point(handoff, cameraAt(path.title, path.cameraDistance * 0.94f), path.title.center);
            point(fastTransitionEnd, photoCam, last.center, Easing::expoOut());
            point(end, photoCam, last.center, Easing::linear());
            break;
        }
        case DocumentaryMove::PullbackReveal: {
            point(handoff, openingCamera, path.title.center);
            point(mid, cameraAt(path.title, path.cameraDistance * 1.8f), path.title.center, Easing::easeOut());
            point(end, photoCam, last.center, Easing::easeInOut());
            break;
        }
        case DocumentaryMove::PushThroughSnapshot: {
            const float coverDistance = std::max(1.f, 0.96f *
                CameraFramingSolver::coverDistanceForBounds(last.halfWidth, last.halfHeight,
                                                            path.fov, path.viewport));
            const Vector3 through = cameraAt(last, coverDistance);
            point(handoff, titleCam, path.title.center);
            point(fastTransitionEnd, through, last.center, Easing::easeIn());
            point(end, photoCam, last.center, Easing::easeOut());
            break;
        }
        case DocumentaryMove::WhipToPhoto: {
            const Vector3 acquisition(path.title.center.x - path.cameraDistance * 0.16f,
                                      path.title.center.y, path.title.center.z + path.cameraDistance);
            point(handoff, titleCam, path.title.center);
            point(handoff + 1, acquisition, path.title.center, Easing::expoOut());
            point(fastTransitionEnd, photoCam, last.center, Easing::easeOut());
            point(end, photoCam, last.center, Easing::linear());
            break;
        }
        case DocumentaryMove::FocusDrop:
            point(handoff, openingCamera, openingTarget);
            point(end, photoCam, last.center);
            rig.fStopTrack().add(at(start, fps), 3.7f, Easing::linear())
                .add(at(end, fps), 3.7f, Easing::linear());
            break;
        case DocumentaryMove::Reveal90: {
            const Vector3 side(path.title.center.x + path.cameraDistance, path.title.center.y,
                               path.title.center.z + path.cameraDistance * 0.12f);
            point(handoff, titleCam, path.title.center);
            point(revealTurnMid, side, path.title.center, Easing::easeInOut());
            point(fastTransitionEnd, photoCam, last.center, Easing::easeOut());
            point(end, photoCam, last.center);
            break;
        }
        case DocumentaryMove::CornerTurn: {
            const Vector3 corner(path.title.center.x + path.cameraDistance * 0.70f,
                                 path.title.center.y, path.title.center.z + path.cameraDistance * 0.72f);
            const Vector3 around(last.center.x + path.cameraDistance * 0.72f, last.center.y,
                                 last.center.z + path.cameraDistance * 0.70f);
            const int turn = handoff + transitionLength / 3;
            const int exit = handoff + (transitionLength * 2) / 3;
            point(handoff, titleCam, path.title.center);
            point(turn, corner, path.title.center, Easing::easeInOut());
            point(exit, around, last.center, Easing::easeInOut());
            point(end, photoCam, last.center);
            break;
        }
        case DocumentaryMove::ForegroundPhotoPass: {
            const auto& a = first;
            const int passCenter = handoff + transitionLength / 3;
            const int passExit = handoff + (transitionLength * 2) / 3;
            point(handoff, titleCam, path.title.center, Easing::linear());
            point(passCenter, titleCam, path.title.center, Easing::linear());
            point(passExit, cameraAt(a, path.cameraDistance), a.center, Easing::easeInOut());
            point(end, cameraAt(a, path.cameraDistance), a.center);
            break;
        }
        case DocumentaryMove::PhotoStack:
        case DocumentaryMove::FilmstripHandoff: {
            point(handoff, titleCam, path.title.center);
            const int travelStart = handoff + 1;
            point(travelStart, cameraAt(first, path.cameraDistance), first.center);
            for (std::size_t i = 1; i < path.snapshots.size(); ++i) {
                const int frame = travelStart + static_cast<int>(
                    (static_cast<std::size_t>(transitionLength - 1) * i) / path.snapshots.size());
                point(frame, cameraAt(path.snapshots[i], path.cameraDistance), path.snapshots[i].center,
                      path.move == DocumentaryMove::PhotoStack ? Easing::easeInOut() : Easing::linear());
            }
            if (path.move == DocumentaryMove::PhotoStack)
                point(end, photoCam, path.finalTarget.center);
            else
                point(end, photoCam, last.center, Easing::easeOut());
            break;
        }
        case DocumentaryMove::SplitDepth: {
            const Vector3 arc((path.title.center.x + last.center.x) * 0.5f - path.cameraDistance * 0.15f,
                              (path.title.center.y + last.center.y) * 0.5f,
                              path.title.center.z + path.cameraDistance * 1.1f);
            point(handoff, arc, path.title.center, Easing::easeIn());
            point(mid, arc, last.center, Easing::easeOut());
            point(end, photoCam, last.center);
            rig.fStopTrack().add(at(handoff, fps), 0.f, Easing::linear())
                .add(at(end, fps), 11.2f, Easing::easeInOut());
            break;
        }
        case DocumentaryMove::ArchiveCraneReveal: {
            const Vector3 crane(path.title.center.x, path.title.center.y - path.cameraDistance * 0.25f,
                                path.title.center.z + path.cameraDistance * 1.65f);
            const Vector3 drift((path.title.center.x + last.center.x) * 0.5f,
                                (path.title.center.y + last.center.y) * 0.5f - path.cameraDistance * 0.10f,
                                path.title.center.z + path.cameraDistance * 1.30f);
            point(handoff, crane, path.title.center, Easing::easeInOut());
            point(mid, drift, last.center, Easing::easeOut());
            point(end, photoCam, path.finalTarget.center);
            break;
        }
    }

    applyCameraTrack(rig, keys, fps);
    if (path.move == DocumentaryMove::FocusDrop)
        applyZPlaneFocusTrack(rig, start, end, handoff, path.title.center.z, last.center.z, fps);
    else
        applyTargetFocusTrack(rig, start, end, fps);
    rig.setPosition(photoCam).setTarget(path.finalTarget.center);
}

} // namespace chrononmotion::motion::camera
