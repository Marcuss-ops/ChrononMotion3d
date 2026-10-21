#include "chrononmotion/motion/CameraFraming.hpp"
#include "chrononmotion/motion/CameraScreenSpace.hpp"
#include "chrononmotion/motion/CameraPanelChoreography.hpp"

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

    CameraFramingRequest all = request;
    all.strategy = CameraFramingStrategy::FitAll;
    const auto fitAll = solver.solve(all, base);
    if (fitAll.requiredDistance <= highest.requiredDistance)
        throw std::runtime_error("FitAll must include lower-weight targets");

    CameraFramingRequest thirds = request;
    thirds.strategy = CameraFramingStrategy::RuleOfThirds;
    thirds.compositionPoint = {1.f / 3.f, 1.f / 3.f};
    const auto ruleOfThirds = solver.solve(thirds, base);
    const chrononmotion::Vector3 center{0.f, 0.f, 0.f};
    const auto thirdsScreen = projectToScreen(center, ruleOfThirds.pose, intrinsics);
    if (std::abs(thirdsScreen.x - request.viewport.x / 3.f) > 0.1f ||
        std::abs(thirdsScreen.y - request.viewport.y / 3.f) > 0.1f)
        throw std::runtime_error("RuleOfThirds must place the target at the requested composition point");

    CameraFramingRequest dollyOnly = request;
    dollyOnly.strategy = CameraFramingStrategy::DollyOnly;
    const auto dollied = solver.solve(dollyOnly, base);
    if (!dollied.pose.hasTarget || dollied.pose.target.distanceTo(base.target) > 1e-4f ||
        dollied.pose.position.distanceTo(base.position) < 1.f)
        throw std::runtime_error("DollyOnly must move the camera while preserving its aim point");

    CameraFramingRequest aimOnly = request;
    aimOnly.strategy = CameraFramingStrategy::AimOnly;
    const auto aimed = solver.solve(aimOnly, base);
    if (aimed.pose.position.distanceTo(base.position) > 1e-4f ||
        aimed.pose.target.distanceTo(center) > 1e-4f)
        throw std::runtime_error("AimOnly must change the aim point while preserving position");

    CameraFramingRequest bounded = request;
    bounded.maxDistance = 100.f;
    const auto clamped = solver.solve(bounded, base);
    if (clamped.pose.position.distanceTo(clamped.pose.target) > 100.01f)
        throw std::runtime_error("framing distance must respect the requested maximum");

    CameraFramingRequest invalid = request;
    invalid.viewport.x = 0.f;
    if (solver.solve(invalid, base).ok)
        throw std::runtime_error("invalid viewport dimensions must be rejected");

    CameraFramingRequest predictive = request;
    predictive.strategy = CameraFramingStrategy::AimOnly;
    predictive.targetVelocity = {100.f, 0.f, 0.f};
    predictive.lookAheadSeconds = 0.5f;
    const auto predicted = solver.solve(predictive, base);
    if (std::abs(predicted.pose.target.x - 50.f) > 1e-4f)
        throw std::runtime_error("look-ahead must frame the predicted target position");

    using namespace chrononmotion::motion::camera;
    const chrononmotion::Vector2 viewport{1920.f, 1080.f};
    const float fit = panelFramingDistance(900.f, 50.f, viewport, 0.78f);
    if (!(fit > 600.f && fit < 1080.f))
        throw std::runtime_error("panel framing must fit the requested width and field of view");
    CameraRig panelRig;
    CameraPanelChoreography move;
    move.subjectWidth = 900.f;
    applyPanelChoreography(panelRig, move, 30.f, viewport);
    const auto start = panelRig.sample(10.f / 30.f);
    const auto middle = panelRig.sample(40.f / 30.f);
    const auto panStart = panelRig.sample(28.f / 30.f);
    const auto end = panelRig.sample(90.f / 30.f);
    if (!(start.position.z > middle.position.z) ||
        std::abs(end.position.x - (panStart.position.x + move.panOffset.x)) > 0.1f)
        throw std::runtime_error("panel choreography must resolve its push, pan and settle through CameraRig");
    return 0;
}
