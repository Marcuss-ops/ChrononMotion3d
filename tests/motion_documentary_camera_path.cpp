#include "chrononmotion/motion/DocumentaryCameraPath.hpp"

#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>

int main() try {
    using namespace chrononmotion;
    using namespace chrononmotion::motion;
    using namespace chrononmotion::motion::camera;

    constexpr std::array moves{
        DocumentaryMove::SnapDown, DocumentaryMove::PullbackReveal,
        DocumentaryMove::PushThroughSnapshot, DocumentaryMove::WhipToPhoto,
        DocumentaryMove::FocusDrop, DocumentaryMove::Reveal90,
        DocumentaryMove::CornerTurn, DocumentaryMove::ForegroundPhotoPass,
        DocumentaryMove::PhotoStack, DocumentaryMove::FilmstripHandoff,
        DocumentaryMove::SplitDepth, DocumentaryMove::ArchiveCraneReveal
    };
    for (const auto move : moves) {
        DocumentaryCameraPath path;
        path.move = move;
        path.title = {{0.f, 180.f, 0.f}, Quaternion{}, 340.f, 100.f};
        path.snapshots = {
            {{-360.f, -120.f, 0.f}, Quaternion{}, 280.f, 190.f},
            {{0.f, -120.f, -160.f}, Quaternion{}, 280.f, 190.f},
            {{360.f, -120.f, -320.f}, Quaternion{}, 280.f, 190.f}
        };
        path.finalTarget = move == DocumentaryMove::ForegroundPhotoPass
            ? path.snapshots.front() : path.snapshots.back();
        path.cameraDistance = 1080.f;
        path.startFrame = 5;
        path.handoffFrame = 35;
        path.endFrame = 100;
        path.transitionFrames = 10;

        CameraRig rig;
        applyDocumentaryCameraPath(rig, path, 30.f);
        for (int frame = path.startFrame; frame <= path.endFrame; ++frame) {
            const CameraPose pose = rig.sample(static_cast<float>(frame) / 30.f);
            if (!std::isfinite(pose.position.x) || !std::isfinite(pose.position.y) ||
                !std::isfinite(pose.position.z) || !std::isfinite(pose.focusDistance) ||
                pose.focusDistance <= 0.f || !pose.hasTarget)
                throw std::runtime_error("documentary camera paths must resolve finite pose and focus samples");
        }
        const auto end = rig.sample(static_cast<float>(path.endFrame) / 30.f);
        if (end.position.distanceTo(rig.position()) > 1e-3f ||
            end.target.distanceTo(path.finalTarget.center) > 1e-3f)
            throw std::runtime_error("documentary camera path must settle on its final anchor: " +
                                     std::to_string(static_cast<int>(move)) + " " +
                                     std::to_string(end.target.x) + " expected " +
                                     std::to_string(path.finalTarget.center.x));
    }
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
