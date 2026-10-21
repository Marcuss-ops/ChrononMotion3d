#include "chrononmotion/motion/CameraFraming.hpp"
#include "chrononmotion/motion/CameraScreenSpace.hpp"

#include <cmath>
#include <stdexcept>

int main() {
    using namespace chrononmotion::motion;
    CameraPose base;
    base.position = {0.f, 0.f, 1000.f};
    base.target = {0.f, 0.f, 0.f};
    base.hasTarget = true;
    base.fov = 50.f;

    CameraFramingRequest request;
    request.viewport = {1920.f, 1080.f};
    request.safeArea = CameraSafeArea::tiktokVertical();
    request.targets.push_back({{-150.f, -100.f, -30.f}, {150.f, 100.f, 30.f}, 1.f});

    CameraFramingSolver solver;
    const auto framed = solver.solve(request, base);
    if (!framed.ok || !framed.pose.hasTarget || framed.requiredDistance <= 0.f)
        throw std::runtime_error("framing must produce an aimed camera and positive distance");

    const ScreenCameraIntrinsics intrinsics{request.viewport, framed.pose.fov};
    const auto& bounds = request.targets.front();
    for (float x : {bounds.min.x, bounds.max.x})
        for (float y : {bounds.min.y, bounds.max.y})
            for (float z : {bounds.min.z, bounds.max.z}) {
                const chrononmotion::Vector3 corner{x, y, z};
                const auto screen = projectToScreen(corner, framed.pose, intrinsics);
                const float safeLeft = request.viewport.x * request.safeArea.left;
                const float safeRight = request.viewport.x * (1.f - request.safeArea.right);
                const float safeTop = request.viewport.y * request.safeArea.top;
                const float safeBottom = request.viewport.y * (1.f - request.safeArea.bottom);
                if (viewDepth(corner, framed.pose) <= 0.f ||
                    !std::isfinite(screen.x) || !std::isfinite(screen.y))
                    throw std::runtime_error("framed bounds must project in front of the camera");
                if (screen.x < safeLeft - 1.f || screen.x > safeRight + 1.f ||
                    screen.y < safeTop - 1.f || screen.y > safeBottom + 1.f)
                    throw std::runtime_error("framed bounds must remain inside the requested safe area");
            }

    CameraFramingSession session;
    const auto first = solver.solve(request, base, &session);
    const auto second = solver.solve(request, base, &session);
    if (first.pose.position.distanceTo(second.pose.position) > 1e-4f)
        throw std::runtime_error("framing session must be deterministic at a held pose");

    request.strategy = CameraFramingStrategy::FitHighest;
    request.targets.push_back({{-400.f, -250.f, -20.f}, {400.f, 250.f, 20.f}, 0.25f});
    const auto highest = solver.solve(request, base);
    if (highest.requiredDistance >= framed.requiredDistance * 2.f)
        throw std::runtime_error("FitHighest must frame only the highest-weight target");
    return 0;
}
