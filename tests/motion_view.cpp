// ChrononMotion view test.
//
// Pins the three things that put the transform model *in space*: the trajectory a
// layer travels along (MotionPath), the 2.5D plane and the perspective view it is
// seen through (MotionPlane / PerspectiveView), and the animated camera
// (CameraRig) with its classic moves.

#include "chrononmotion/chrononmotion.hpp"

#include "motion_check.hpp"

#include <cmath>
#include <stdexcept>
#include <vector>

using namespace chrononmotion;
using namespace chrononmotion::motion;
using chrononmotion_test::check;
using chrononmotion_test::checkNear;
using chrononmotion_test::section;

namespace {

    Vector3 forwardOf(const CameraPose& pose) {

        Vector3 forward(0.f, 0.f, -1.f);
        forward.applyQuaternion(pose.orientation);
        return forward;
    }

    void configureCamera(PerspectiveCamera& camera, float fov, const Vector3& position, const Vector3& target) {

        camera.fov = fov;
        camera.aspect = 16.f / 9.f;
        camera.nearPlane = 0.1f;
        camera.farPlane = 2000.f;
        camera.position.copy(position);
        camera.lookAt(target);
        camera.updateMatrixWorld(true);
        camera.updateProjectionMatrix();
    }

    // -------------------------------------------------------------- motion path

    void pathTrajectories() {

        section("motion paths");

        const MotionPath line = MotionPath::line(Vector3(0.f, 0.f, 0.f), Vector3(10.f, 0.f, 0.f));
        checkNear(line.sample(0.f), Vector3(0.f, 0.f, 0.f), 1e-5f, "a line starts at its first point");
        checkNear(line.sample(1.f), Vector3(10.f, 0.f, 0.f), 1e-5f, "a line ends at its second point");
        checkNear(line.sampleUniform(0.5f), Vector3(5.f, 0.f, 0.f), 1e-4f, "a line's midpoint is its middle");
        checkNear(line.length(), 10.f, 1e-3f, "a line's length is the distance between its points");
        checkNear(line.tangent(0.5f), Vector3(1.f, 0.f, 0.f), 1e-4f, "a line's tangent is its direction");
        check(line.points(4).size() == 5, "getPoints(n) returns n+1 samples");
        check(!line.closed(), "a line is not closed");

        // Same t yields the same point: sampling has no hidden cursor.
        checkNear(line.sample(0.37f), line.sample(0.37f), 0.f, "path sampling is a pure function of t");

        const MotionPath bezier = MotionPath::cubicBezier(
                Vector3(0.f, 0.f, 0.f), Vector3(1.f, 1.f, 0.f), Vector3(2.f, -1.f, 0.f), Vector3(3.f, 0.f, 0.f));
        checkNear(bezier.sample(0.f), Vector3(0.f, 0.f, 0.f), 1e-5f, "a cubic bezier starts at its first point");
        checkNear(bezier.sample(1.f), Vector3(3.f, 0.f, 0.f), 1e-5f, "a cubic bezier ends at its last point");
        checkNear(bezier.sample(0.5f), Vector3(1.5f, 0.f, 0.f), 1e-4f, "a symmetric cubic bezier is symmetric");
        check(bezier.length() > 3.f, "a bent bezier is longer than the chord it spans");

        const MotionPath quad = MotionPath::quadraticBezier(
                Vector3(0.f, 0.f, 0.f), Vector3(5.f, 10.f, 0.f), Vector3(10.f, 0.f, 0.f));
        checkNear(quad.sample(0.f), Vector3(0.f, 0.f, 0.f), 1e-5f, "a quadratic bezier starts at its first point");
        checkNear(quad.sample(1.f), Vector3(10.f, 0.f, 0.f), 1e-5f, "a quadratic bezier ends at its last point");
        check(quad.sample(0.5f).y > 4.f, "a quadratic bezier bulges through its control point");

        // Catmull-Rom interpolates *through* its waypoints — that is why it is the
        // right choice once there are more than two stops.
        const std::vector<Vector3> waypoints{
                Vector3(0.f, 0.f, 0.f), Vector3(2.f, 3.f, 0.f), Vector3(5.f, -1.f, 0.f), Vector3(8.f, 2.f, 0.f)};
        const MotionPath spline = MotionPath::catmullRom(waypoints);
        checkNear(spline.sample(0.f), waypoints.front(), 1e-4f, "a catmull-rom path starts on its first waypoint");
        checkNear(spline.sample(1.f), waypoints.back(), 1e-4f, "a catmull-rom path ends on its last waypoint");
        for (std::size_t i = 0; i < waypoints.size(); ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(waypoints.size() - 1);
            checkNear(spline.sample(t), waypoints[i], 1e-3f, "a catmull-rom path passes through every waypoint");
        }
        check(spline.length() > 8.f, "a catmull-rom path is longer than its straight span");

        // Orient to path: local +Z follows the tangent.
        Vector3 alongLocalZ(0.f, 0.f, 1.f);
        alongLocalZ.applyQuaternion(line.orientToPath(0.5f));
        checkNear(alongLocalZ, Vector3(1.f, 0.f, 0.f), 1e-4f,
                  "orient-to-path turns local +Z along the tangent");

        // The degenerate cases are well behaved rather than NaN.
        checkNear(MotionPath::directionToQuaternion(Vector3(0.f, 0.f, 0.f))
                          .angleTo(Quaternion(0.f, 0.f, 0.f, 1.f)),
                  0.f, 1e-5f, "orienting to no direction is the identity rotation");
        Vector3 reversed(0.f, 0.f, 1.f);
        reversed.applyQuaternion(MotionPath::directionToQuaternion(Vector3(0.f, 0.f, -1.f)));
        checkNear(reversed, Vector3(0.f, 0.f, -1.f), 1e-3f,
                  "orienting to the exact opposite direction is handled");
    }

    // ------------------------------------------------------------------- 2.5D

    void planeGeometry() {

        section("2.5D plane geometry");

        const ContentRef image = ContentRef::image("logo.png", Vector2(200.f, 100.f));

        bool rejectedWidth = false;
        try {
            (void)MotionPlane::fitted(image, 0.f);
        } catch (const std::invalid_argument&) {
            rejectedWidth = true;
        }
        check(rejectedWidth, "a non-positive fitted width is rejected");

        bool rejectedSize = false;
        try {
            (void)MotionPlane::sized(image, Vector2(0.f, 2.f));
        } catch (const std::invalid_argument&) {
            rejectedSize = true;
        }
        check(rejectedSize, "a non-positive plane size is rejected");

        const MotionPlane fitted = MotionPlane::fitted(image, 10.f);
        checkNear(fitted.size, Vector2(10.f, 5.f), 1e-5f,
                  "a fitted plane keeps the content's aspect ratio");

        const MotionPlane sized = MotionPlane::sized(image, Vector2(4.f, 2.f));
        checkNear(sized.size, Vector2(4.f, 2.f), 1e-5f, "a sized plane uses the given extent");

        // The anchor is normalized in the box: (0,0) is the bottom-left corner,
        // reported in layer-local units.
        MotionPlane corner;
        corner.size.set(10.f, 5.f);
        corner.anchor.set(0.f, 0.f);
        checkNear(corner.anchorPoint(), Vector3(-5.f, -2.5f, 0.f), 1e-5f,
                  "the (0,0) anchor is the bottom-left corner");
        corner.anchor.set(0.5f, 0.5f);
        checkNear(corner.anchorPoint(), Vector3(0.f, 0.f, 0.f), 1e-5f,
                  "the centered anchor is the box origin");

        const auto corners = corner.corners();
        checkNear(corners[0], Vector3(-5.f, -2.5f, 0.f), 1e-5f, "the box corners bound the plane (bottom-left)");
        checkNear(corners[2], Vector3(5.f, 2.5f, 0.f), 1e-5f, "the box corners bound the plane (top-right)");

        const ContentRef logo = ContentRef::image("logo.png", Vector2(200.f, 100.f));
        const ContentRef sameLogo = ContentRef::image("logo.png", Vector2(64.f, 64.f));
        check(logo == sameLogo, "content identity is the id and the kind, not the measurement");
        check(logo != ContentRef::text("logo.png", Vector2(200.f, 100.f)),
              "the same id with a different kind is different content");
        check(ContentRef::none().empty() && !logo.empty(), "an empty ref is the absent content");

        Layer layer(1, "Title");
        check(layer.is3D, "a layer starts as a fully 3D object");
        corner.depth = -3.f;
        corner.applyTo(layer);
        checkNear(layer.transform.anchor, Vector3(0.f, 0.f, 0.f), 1e-5f,
                  "applying a plane writes its anchor onto the layer");
        checkNear(layer.transform.position, Vector3(0.f, 0.f, -3.f), 1e-5f,
                  "applying a plane writes its depth onto the layer");
        check(!layer.is3D, "applying a plane marks the layer as a 2.5D plane");

        // The anchor is the pivot: the plane's anchor point lands exactly on the
        // layer's position, for any anchor.
        Transform transform;
        corner.anchor.set(0.f, 0.f);
        transform.anchor = corner.anchorPoint();
        transform.position.set(7.f, -2.f, 1.f);
        transform.rotation.set(0.f, 0.f, math::degToRad(33.f));
        transform.scale.set(1.5f, 1.5f, 1.f);
        checkNear(transform.applyPoint(corner.anchorPoint()), Vector3(7.f, -2.f, 1.f), 1e-4f,
                  "the plane's anchor is the transform's fixed point");
    }

    void planeRefreshDerivesFromTheMeasurement() {

        section("plane re-derivation from a re-measurement");

        // A text run measured at 200x100, pivoted on its left edge.
        const ContentRef first = ContentRef::measured("title", ContentKind::Text, "sha256:a1",
                                                      Vector2(200.f, 100.f), Vector2(0.f, 0.5f));
        MotionPlane plane = MotionPlane::fitted(first, 10.f);
        plane.depth = -4.f;

        Layer layer(1, "Title");
        layer.content = first;
        plane.applyTo(layer);

        check(plane.derivedFrom(first), "a plane knows the measurement it was derived from");
        checkNear(plane.size, Vector2(10.f, 5.f), 1e-5f, "its size comes from the measured aspect ratio");
        checkNear(layer.transform.anchor, Vector3(-5.f, 0.f, 0.f), 1e-5f,
                  "and its pivot from the measured anchor");

        // The same measurement again: nothing to derive, so nothing is written.
        const ContentRef unchanged = ContentRef::measured("title", ContentKind::Text, "sha256:a1",
                                                          Vector2(200.f, 100.f), Vector2(0.f, 0.5f));
        check(!plane.refresh(layer, unchanged), "an unchanged measurement is not re-applied");
        checkNear(layer.transform.anchor, Vector3(-5.f, 0.f, 0.f), 1e-5f, "and the pivot is left alone");

        // Chronon re-measures the same asset: wider, and centred.
        const ContentRef wider = ContentRef::measured("title", ContentKind::Text, "sha256:b2",
                                                      Vector2(400.f, 100.f), Vector2(0.5f, 0.5f));
        check(plane.refresh(layer, wider), "a new measurement re-derives the plane");
        check(plane.derivedFrom(wider) && !plane.derivedFrom(first),
              "the plane derives from the new measurement and not the old one");
        checkNear(plane.size, Vector2(10.f, 2.5f), 1e-5f, "the fitted plane follows the new aspect ratio");
        checkNear(layer.transform.anchor, Vector3(0.f, 0.f, 0.f), 1e-5f, "and the pivot follows the new anchor");
        checkNear(layer.transform.position, Vector3(0.f, 0.f, -4.f), 1e-4f, "the depth is preserved");
        checkNear(plane.depth, -4.f, 1e-5f, "refresh does not move the plane in Z");
        check(layer.content == wider, "the layer carries the re-measured content");

        // A sized plane keeps the extent its author chose and takes only the pivot.
        const ContentRef recentered = ContentRef::measured("title", ContentKind::Text, "sha256:c3",
                                                           Vector2(400.f, 100.f), Vector2(0.5f, 0.5f));
        Layer sizedLayer(2, "Sized");
        sizedLayer.content = first;
        MotionPlane sized = MotionPlane::sized(first, Vector2(6.f, 3.f));
        sized.applyTo(sizedLayer);
        check(sized.refresh(sizedLayer, recentered), "a sized plane re-derives too");
        checkNear(sized.size, Vector2(6.f, 3.f), 1e-5f, "but keeps the explicit extent");
        checkNear(sizedLayer.transform.anchor, Vector3(0.f, 0.f, 0.f), 1e-5f, "while taking the new pivot");

        // A plane built by hand has no source, so nothing can be re-derived.
        Layer handmadeLayer(3, "Handmade");
        handmadeLayer.content = first;
        MotionPlane handmade;
        handmade.applyTo(handmadeLayer);
        check(!handmade.derivedFrom(first) && !handmade.refresh(handmadeLayer, wider),
              "a hand-built plane carries no derivation to refresh");

        // Re-pointing the layer at another asset is not a re-measurement.
        const ContentRef other = ContentRef::measured("other", ContentKind::Image, "sha256:d4", Vector2(10.f, 10.f));
        Layer otherLayer(4, "Other");
        otherLayer.content = other;
        MotionPlane otherPlane = MotionPlane::fitted(other, 4.f);
        check(!otherPlane.refresh(otherLayer, wider), "a different asset is not a re-measurement");
        checkNear(otherPlane.size, Vector2(4.f, 4.f), 1e-5f, "and the plane is left untouched");

        // The derived pivot is the static pose: a keyed anchor channel still wins,
        // exactly as it does over `applyTo`.
        Layer keyed(5, "Keyed");
        keyed.content = first;
        MotionPlane keyedPlane = MotionPlane::fitted(first, 10.f);
        keyedPlane.applyTo(keyed);
        keyed.tracks.anchor.add(0.f, Vector3(9.f, 9.f, 0.f));
        keyed.tracks.anchor.add(1.f, Vector3(9.f, 9.f, 0.f));
        check(keyedPlane.refresh(keyed, wider), "the plane re-derives onto a layer with a keyed anchor");
        checkNear(keyed.sample(0.5f).anchor, Vector3(9.f, 9.f, 0.f), 1e-5f,
                  "the animated anchor channel remains the authority after a refresh");
    }

    void textAndImagePlanesAreTheSameType() {

        section("text/image plane invariance");

        MotionScene scene("invariance", 24.f);
        Layer& textLayer = scene.addContent(1, "Title", ContentRef::text("title", Vector2(800.f, 120.f)));
        Layer& imageLayer = scene.addContent(2, "Logo", ContentRef::image("logo.png", Vector2(800.f, 120.f)));

        const MotionPlane textPlane = MotionPlane::fitted(textLayer.content, 16.f);
        const MotionPlane imagePlane = MotionPlane::fitted(imageLayer.content, 16.f);

        checkNear(textPlane.size, imagePlane.size, 0.f, "a text plane and an image plane of the same box match");
        checkNear(textPlane.anchorPoint(), imagePlane.anchorPoint(), 0.f,
                  "their anchors are computed by the same code");

        MotionPlane textOnLayer = textPlane;
        MotionPlane imageOnLayer = imagePlane;
        textOnLayer.depth = -3.f;
        imageOnLayer.depth = -3.f;
        textOnLayer.applyTo(textLayer);
        imageOnLayer.applyTo(imageLayer);

        // Identical motion, authored the same way on both layers.
        const Vector3 from(0.f, 0.f, -3.f);
        const Vector3 to(8.f, 2.f, -3.f);
        for (Layer* layer : {&textLayer, &imageLayer}) {
            layer->tracks.position.add(0.f, from, Easing::easeInOut());
            layer->tracks.position.add(1.f, to, Easing::linear());
            layer->tracks.rotation.add(0.f, Vector3(0.f, 0.f, 0.f));
            layer->tracks.rotation.add(1.f, Vector3(0.f, math::degToRad(20.f), 0.f));
            layer->tracks.scale.add(0.f, Vector3(0.7f, 0.7f, 0.7f));
            layer->tracks.scale.add(1.f, Vector3(1.f, 1.f, 1.f));
            layer->tracks.opacity.add(0.f, 0.f);
            layer->tracks.opacity.add(1.f, 1.f);
        }

        PerspectiveCamera camera;
        configureCamera(camera, 60.f, Vector3(0.f, 0.f, 12.f), Vector3(0.f, 0.f, 0.f));
        PerspectiveView view(camera, Vector2(1920.f, 1080.f));

        for (int frame = 0; frame <= 24; frame += 6) {
            scene.evaluate(frame);

            const Matrix4& textWorld = scene.worldMatrix(1);
            const Matrix4& imageWorld = scene.worldMatrix(2);
            bool identical = true;
            for (unsigned int i = 0; i < 16; ++i) {
                if (textWorld[i] != imageWorld[i]) identical = false;
            }
            check(identical, "a text layer and an image layer with the same channels share one world matrix");

            const ScreenPlane textScreen = view.projectPlane(textPlane, textWorld);
            const ScreenPlane imageScreen = view.projectPlane(imagePlane, imageWorld);
            check(textScreen.visible == imageScreen.visible, "both planes agree on visibility");
            checkNear(textScreen.center, imageScreen.center, 1e-4f, "both planes project to the same center");
            checkNear(textScreen.size, imageScreen.size, 1e-4f, "both planes project to the same size");

            checkNear(scene.worldOpacity(1), scene.worldOpacity(2), 0.f,
                      "text and image share the opacity channel");
        }
    }

    void perspectiveViewProjectsAndParallaxes() {

        section("perspective view");

        PerspectiveCamera camera;
        configureCamera(camera, 90.f, Vector3(0.f, 0.f, 10.f), Vector3(0.f, 0.f, 0.f));
        PerspectiveView view(camera, Vector2(1920.f, 1080.f));

        // fov 90, zoom 1 -> the focal length in pixels is half the viewport height.
        checkNear(view.focalPixels(), 540.f, 1e-3f, "the focal length in pixels follows the fov");
        checkNear(view.pixelsPerUnit(10.f), 54.f, 1e-3f, "world units scale with 1/distance");

        MotionPlane plane;
        plane.size.set(2.f, 1.f);
        plane.anchor.set(0.5f, 0.5f);

        const Matrix4 atOrigin;
        const ScreenPlane near = view.projectPlane(plane, atOrigin);
        check(near.visible, "a plane in front of the camera is visible");
        checkNear(near.center, Vector2(960.f, 540.f), 1e-2f, "a centered plane projects to the viewport center");
        checkNear(near.size, Vector2(108.f, 54.f), 1e-2f, "a plane at distance 10 projects at 54 px per unit");
        checkNear(near.distance, 10.f, 1e-4f, "the plane reports its distance to the camera");

        // Z depth: the same plane twice as far away is half the size on screen.
        Matrix4 farther;
        farther.makeTranslation(0.f, 0.f, -10.f);
        const ScreenPlane far = view.projectPlane(plane, farther);
        checkNear(far.size, Vector2(54.f, 27.f), 1e-2f, "doubling the distance halves the projected size");
        checkNear(far.pixelsPerUnit, 27.f, 1e-3f, "a farther plane has a smaller pixels-per-unit");

        // Automatic parallax: the same camera move shifts the near plane more.
        const Vector3 cameraDelta(1.f, 0.f, 0.f);
        const Vector2 nearShift = view.parallaxShift(cameraDelta, near);
        const Vector2 farShift = view.parallaxShift(cameraDelta, far);
        check(std::fabs(nearShift.x) > std::fabs(farShift.x),
              "a nearer plane parallaxes more than a farther one");
        checkNear(nearShift, Vector2(-54.f, 0.f), 1e-2f,
                  "the parallax shift is the inverse camera move scaled by pixels-per-unit");
        checkNear(view.parallaxShift(Vector3(0.f, 1.f, 0.f), near).y, 54.f, 1e-2f,
                  "a camera move up slides the plane down in a y-down pixel space");
        checkNear(view.parallaxShift(cameraDelta, ScreenPlane{}), Vector2(0.f, 0.f), 0.f,
                  "an off-screen plane has no parallax");

        // Behind the camera is not visible, and not silently clamped.
        Vector2 pixels;
        Vector3 ndc;
        check(!view.project(Vector3(0.f, 0.f, 20.f), pixels, ndc), "a point behind the camera does not project");

        Matrix4 behind;
        behind.makeTranslation(0.f, 0.f, 20.f);
        check(!view.projectPlane(plane, behind).visible, "a plane behind the camera is not visible");
    }

    // ------------------------------------------------------------------ camera

    void cameraRigMoves() {

        section("camera rig");

        bool rejectedCamera = false;
        try {
            CameraRig invalid(1, 0.f, 16.f / 9.f, 0.1f, 2000.f);
        } catch (const std::invalid_argument&) {
            rejectedCamera = true;
        }
        check(rejectedCamera, "an invalid camera fov is rejected");

        CameraRig rig(1, 60.f, 16.f / 9.f, 0.1f, 2000.f);
        rig.setPosition(Vector3(0.f, 0.f, 10.f));

        bool rejectedFocalLength = false;
        try {
            rig.setFocalLength(0.f);
        } catch (const std::invalid_argument&) {
            rejectedFocalLength = true;
        }
        check(rejectedFocalLength, "a non-positive focal length is rejected");

        const CameraPose resting = rig.sample(0.f);
        checkNear(resting.position, Vector3(0.f, 0.f, 10.f), 1e-5f, "the rig samples its static position");
        checkNear(resting.fov, 60.f, 1e-5f, "the rig samples its static fov");
        check(!resting.hasTarget, "a rig with no target has no look-at");
        checkNear(resting.orientation.angleTo(Quaternion(0.f, 0.f, 0.f, 1.f)), 0.f, 1e-5f,
                  "an unauthored rig has an identity orientation");

        // Look-at: the pose's forward is the direction to the target.
        CameraPose aimed = resting;
        aimed.hasTarget = true;
        aimed.target.set(0.f, 0.f, 0.f);
        CameraRig::lookAtPose(aimed, aimed.target);
        checkNear(forwardOf(aimed), Vector3(0.f, 0.f, -1.f), 1e-4f,
                  "a look-at pose faces the target down its local -Z");

        // Dolly moves along the view axis, so the distance to the target shrinks.
        CameraPose dollied = aimed;
        CameraRig::dolly(dollied, 3.f);
        checkNear(dollied.position, Vector3(0.f, 0.f, 7.f), 1e-4f, "dolly moves along the view axis");
        checkNear(dollied.position.distanceTo(dollied.target), 7.f, 1e-4f,
                  "dolling forward closes the distance to the target");
        checkNear(forwardOf(dollied), Vector3(0.f, 0.f, -1.f), 1e-4f, "dolly keeps the aim");

        // Pan translates in the camera's own plane and leaves the aim alone.
        CameraPose panned = aimed;
        CameraRig::pan(panned, Vector2(2.f, 3.f));
        checkNear(panned.position, Vector3(2.f, 3.f, 10.f), 1e-4f, "pan moves in the camera's plane");
        checkNear(panned.orientation.angleTo(aimed.orientation), 0.f, 1e-4f, "pan does not rotate the camera");

        // Tilt pitches about local X; roll turns about the view axis.
        CameraPose tilted = aimed;
        CameraRig::tilt(tilted, math::degToRad(20.f));
        Vector3 tiltedRight(1.f, 0.f, 0.f);
        tiltedRight.applyQuaternion(tilted.orientation);
        checkNear(tiltedRight, Vector3(1.f, 0.f, 0.f), 1e-4f, "tilt leaves the local X axis alone");
        // A positive rotation about local X lifts the -Z view axis.
        check(forwardOf(tilted).y > 0.3f, "a positive tilt pitches the view up");
        check(forwardOf(tilted).z < 0.f, "a tilted camera still looks forward");

        CameraPose rolled = aimed;
        CameraRig::roll(rolled, math::degToRad(45.f));
        Vector3 rolledForward(0.f, 0.f, -1.f);
        rolledForward.applyQuaternion(rolled.orientation);
        checkNear(rolledForward, Vector3(0.f, 0.f, -1.f), 1e-4f, "roll leaves the view axis alone");
        Vector3 rolledRight(1.f, 0.f, 0.f);
        rolledRight.applyQuaternion(rolled.orientation);
        checkNear(rolledRight,
                  Vector3(std::cos(math::degToRad(45.f)), std::sin(math::degToRad(45.f)), 0.f),
                  1e-4f, "roll turns the local X axis about the view axis");

        // Orbit keeps the subject framed and the distance unchanged.
        CameraPose orbited = aimed;
        CameraRig::orbit(orbited, math::PI * 0.5f, 0.f);
        checkNear(orbited.position, Vector3(10.f, 0.f, 0.f), 1e-3f, "orbit swings the camera around the target");
        checkNear(orbited.position.distanceTo(orbited.target), 10.f, 1e-3f,
                  "orbit keeps the distance to the target");
        checkNear(forwardOf(orbited), Vector3(-1.f, 0.f, 0.f), 1e-3f, "orbit keeps the target centered");

        CameraPose lifted = aimed;
        CameraRig::orbit(lifted, 0.f, 0.4f);
        check(lifted.position.y > 1.f, "a positive pitch raises the camera above the target");
        checkNear(lifted.position.distanceTo(lifted.target), 10.f, 1e-3f,
                  "lifting keeps the distance to the target");
        Vector3 expectedAim = lifted.target;
        expectedAim.sub(lifted.position);
        expectedAim.normalize();
        checkNear(forwardOf(lifted), expectedAim, 1e-3f, "a lifted camera still aims at the target");
        check(rig.layer().isNull(), "a camera rig is backed by a null layer");
        checkNear(presets::directionOffset(presets::Direction::Left), Vector3(-1.f, 0.f, 0.f), 1e-6f,
                  "the preset direction helper names the left offset");
        checkNear(presets::axisVector(presets::Axis::Y), Vector3(0.f, 1.f, 0.f), 1e-6f,
                  "the preset axis helper names the Y axis");
    }

    void cameraRigDrivesACameraNode() {

        section("camera rig animation");

        CameraRig rig(1, 60.f, 16.f / 9.f, 0.1f, 2000.f);
        rig.setPosition(Vector3(0.f, 0.f, 12.f));
        rig.setTarget(Vector3(0.f, 0.f, 0.f));

        presets::cameraPush(rig, 0, 30, 4.f, 30.f);
        checkNear(rig.sample(0.f).position.z, 12.f, 1e-3f, "a camera push starts at the authored position");
        checkNear(rig.sample(1.f).position.z, 8.f, 1e-3f, "a camera push dollies toward the target");
        const float midZ = rig.sample(0.5f).position.z;
        check(midZ < 12.f && midZ > 8.f, "a camera push is monotone between its keys");
        checkNear(rig.endTime(), 1.f, 1e-5f, "the rig reports the end of its keys");

        presets::cameraFov(rig, 0, 30, 60.f, 30.f, 30.f);
        checkNear(rig.sample(0.f).fov, 60.f, 1e-4f, "an fov track drives the sampled fov");
        checkNear(rig.sample(0.5f).fov, 45.f, 1e-3f, "the fov interpolates");
        checkNear(rig.sample(1.f).fov, 30.f, 1e-4f, "the fov track lands on its last key");

        auto camera = rig.makeCamera();
        rig.apply(0.f, *camera);
        checkNear(camera->position, Vector3(0.f, 0.f, 12.f), 1e-3f, "apply writes the sampled position");
        checkNear(camera->fov, 60.f, 1e-3f, "apply writes the sampled fov");
        checkNear(forwardOf(rig.sample(0.f)), Vector3(0.f, 0.f, -1.f), 1e-3f, "the camera faces its target");
        check(camera->matrixWorldInverse[14] != 0.f,
              "apply leaves matrixWorldInverse materialized, ready for projection");
        checkNear(camera->aspect, 16.f / 9.f, 1e-5f, "the rig's camera carries its aspect");
        checkNear(camera->nearPlane, 0.1f, 1e-6f, "the rig's camera carries its near plane");

        // Focal length: writing it re-derives the fov from the film gauge.
        CameraRig focal(2, 60.f, 16.f / 9.f);
        focal.setPosition(Vector3(0.f, 0.f, 10.f));
        focal.setFocalLength(50.f);
        auto focalCamera = focal.makeCamera();
        focal.apply(0.f, *focalCamera);
        // filmHeight = 35 / (16/9) = 19.6875; fov = 2*atan(0.5*19.6875/50) in degrees.
        const float expectedFov = math::radToDeg(2.f * std::atan(0.5f * (35.f / (16.f / 9.f)) / 50.f));
        checkNear(focalCamera->fov, expectedFov, 1e-2f,
                  "an authored focal length re-derives the fov from the film gauge");
        checkNear(focalCamera->fov, 22.27f, 0.05f, "a 50mm lens on a 35mm gauge is about 22 degrees");

        // A rig can itself be parented: its parent world matrix is prepended.
        CameraRig child(3, 60.f, 16.f / 9.f);
        child.setPosition(Vector3(0.f, 0.f, 10.f));
        Matrix4 parentWorld;
        parentWorld.makeTranslation(0.f, 5.f, 0.f);
        auto childCamera = child.makeCamera();
        child.apply(0.f, *childCamera, &parentWorld);
        checkNear(childCamera->position, Vector3(0.f, 5.f, 10.f), 1e-3f,
                  "a parented camera inherits its rig's transform");
    }

}// namespace

int main() {

    pathTrajectories();
    planeGeometry();
    planeRefreshDerivesFromTheMeasurement();
    textAndImagePlanesAreTheSameType();
    perspectiveViewProjectsAndParallaxes();
    cameraRigMoves();
    cameraRigDrivesACameraNode();

    return chrononmotion_test::report();
}
