// ChrononMotion canary scene.
//
// The composition that has to hold *at once*, on one timeline:
//
//     Background image
//     Null_Main
//     ├── Title Text
//     ├── Subtitle Text
//     └── Product Image
//     Camera
//
//     frames 0-30   Title slide + fade
//     frames 15-45  Product scale 70% -> 100%
//     frames 30-60  Null_Main rotationY 0° -> 15°
//     frames 45-75  Camera dolly forward
//     frames 60-90  Title opacity 100 -> 0
//
// If this passes, Text, Image, Parenting, Anchor, Bézier, Scale, Rotation 3D,
// Z-depth, Camera, Parallax, Opacity and Timing are all demonstrated on the same
// deterministic evaluation — which is the whole point of the primitive set.

#include "threepp/threepp.hpp"

#include "motion_check.hpp"

#include <cmath>
#include <string>

using namespace threepp;
using namespace threepp::motion;
using chrononmotion_test::check;
using chrononmotion_test::checkNear;
using chrononmotion_test::section;

namespace {

    constexpr float kFps = 30.f;
    constexpr float kFrameSeconds = 1.f / kFps;

    constexpr Layer::Id kBackground = 1;
    constexpr Layer::Id kController = 10;
    constexpr Layer::Id kTitle = 11;
    constexpr Layer::Id kSubtitle = 12;
    constexpr Layer::Id kProduct = 13;

    struct Canary {
        MotionScene scene{"canary", kFps};
        MotionPlane backgroundPlane;
        MotionPlane titlePlane;
        MotionPlane subtitlePlane;
        MotionPlane productPlane;
        CameraRig camera{20, 60.f, 16.f / 9.f, 0.1f, 2000.f};
    };

    Canary buildCanary() {

        Canary canary;

        Layer& background = canary.scene.addContent(
                kBackground, "Background", ContentRef::image("background.png", Vector2(1920.f, 1080.f)));
        canary.backgroundPlane = MotionPlane::fitted(background.content, 20.f);
        canary.backgroundPlane.depth = -5.f;
        canary.backgroundPlane.applyTo(background);
        background.lifetime = FrameRange::between(0, 90);

        canary.scene.addNull(kController, "Null_Main");

        Layer& title = canary.scene.addContent(
                kTitle, "Title", ContentRef::text("title", Vector2(800.f, 120.f)), kController);
        canary.titlePlane = MotionPlane::fitted(title.content, 8.f);
        canary.titlePlane.applyTo(title);
        title.transform.position.y = 2.f;

        Layer& subtitle = canary.scene.addContent(
                kSubtitle, "Subtitle", ContentRef::text("subtitle", Vector2(600.f, 90.f)), kController);
        canary.subtitlePlane = MotionPlane::fitted(subtitle.content, 6.f);
        canary.subtitlePlane.depth = -0.5f;
        canary.subtitlePlane.applyTo(subtitle);
        subtitle.transform.position.set(-4.f, -2.f, -0.5f);
        subtitle.transform.anchor = canary.subtitlePlane.anchorPoint();

        Layer& product = canary.scene.addContent(
                kProduct, "Product", ContentRef::image("product.png", Vector2(480.f, 480.f)), kController);
        canary.productPlane = MotionPlane::fitted(product.content, 5.f);
        canary.productPlane.depth = 1.f;
        canary.productPlane.applyTo(product);
        product.transform.position.set(0.f, -1.f, 1.f);

        canary.camera.setPosition(Vector3(0.f, 0.f, 14.f));
        canary.camera.setTarget(Vector3(0.f, 0.f, 0.f));

        return canary;
    }

    // The schedule, applied to the built scene. Kept separate from the build so a
    // reader can compare it line-for-line against the header comment above.
    void authorSchedule(Canary& canary) {

        Layer* controller = canary.scene.findLayer(kController);
        Layer* title = canary.scene.findLayer(kTitle);
        Layer* subtitle = canary.scene.findLayer(kSubtitle);
        Layer* product = canary.scene.findLayer(kProduct);

        // 0-30: Title slide + fade.
        presets::slideIn(*title, presets::Direction::Left, 0, 30, 6.f, kFps);
        presets::fadeIn(*title, 0, 30, kFps);

        // 15-45: Product scale 70% -> 100%.
        presets::scalePop(*product, 15, 30, 0.7f, 1.0f, kFps, Easing::easeInOut());

        // 30-60: Null_Main rotationY 0° -> 15°.
        controller->tracks.rotation.add(kFrameSeconds * 30.f, Vector3(0.f, 0.f, 0.f), Easing::easeInOut());
        controller->tracks.rotation.add(kFrameSeconds * 60.f, Vector3(0.f, math::degToRad(15.f), 0.f));

        // 45-75: camera dolly forward.
        presets::cameraPush(canary.camera, 45, 30, 4.f, kFps);

        // 60-90: Title opacity 100 -> 0.
        presets::fadeOut(*title, 60, 30, kFps);

        // Subtitle: a curved travel across the whole timeline (Bézier path).
        presets::arcMotion(*subtitle, Vector3(4.f, -2.f, -0.5f), Vector3(0.f, 1.5f, -0.5f), 0, 90, kFps);
    }

    Vector3 worldPositionOf(const MotionScene& scene, Layer::Id id) {

        Vector3 out;
        out.setFromMatrixPosition(scene.worldMatrix(id));
        return out;
    }

    /// The world position of a layer's *pivot*. The anchor is the point the
    /// transform pins to `position`, so `world * anchor` is where that point
    /// lands — which is not the matrix origin unless the anchor is zero.
    Vector3 worldPivotOf(const MotionScene& scene, Layer::Id id) {

        Vector3 pivot = scene.state(id).local.anchor;
        pivot.applyMatrix4(scene.worldMatrix(id));
        return pivot;
    }

    Quaternion worldRotationOf(const MotionScene& scene, Layer::Id id) {

        Quaternion out;
        out.setFromRotationMatrix(scene.worldMatrix(id));
        return out;
    }

    float worldScaleXOf(const MotionScene& scene, Layer::Id id) {

        Vector3 scale;
        scale.setFromMatrixScale(scene.worldMatrix(id));
        return scale.x;
    }

    float cameraDistance(const Canary& canary, int frame) {

        const CameraPose pose = canary.camera.sample(static_cast<float>(frame) * kFrameSeconds);
        return pose.position.distanceTo(pose.target);
    }

    // -------------------------------------------------------------- structure

    void theSceneIsTheIntendedOne() {

        section("canary structure");

        Canary canary = buildCanary();

        check(canary.scene.layerCount() == 5, "the canary has a background, a controller and three children");
        check(canary.scene.validate().empty(), "the canary hierarchy is well formed");

        const Layer* background = canary.scene.findLayer(kBackground);
        const Layer* controller = canary.scene.findLayer(kController);
        const Layer* title = canary.scene.findLayer(kTitle);
        const Layer* subtitle = canary.scene.findLayer(kSubtitle);
        const Layer* product = canary.scene.findLayer(kProduct);

        check(background->content.isImage(), "the background is image content");
        check(product->content.isImage(), "the product is image content");
        check(title->content.isText() && subtitle->content.isText(), "the title and subtitle are text content");
        check(controller->isNull(), "Null_Main is a controller layer with no content");
        check(title->parentId == kController && subtitle->parentId == kController && product->parentId == kController,
              "all three content layers hang off Null_Main");
        check(background->parentId == Layer::kRoot, "the background is a scene root");
    }

    // ---------------------------------------------------------------- schedule

    void titleSlidesAndFades() {

        section("canary: title slide + fade");

        Canary canary = buildCanary();
        authorSchedule(canary);
        MotionScene& scene = canary.scene;

        scene.evaluate(0);
        const Vector3 offFrame = worldPositionOf(scene, kTitle);
        scene.evaluate(30);
        const Vector3 settled = worldPositionOf(scene, kTitle);
        scene.evaluate(15);
        const Vector3 midway = worldPositionOf(scene, kTitle);

        checkNear(offFrame, Vector3(-6.f, 2.f, 0.f), 1e-3f, "the title starts off-frame to the left");
        checkNear(settled, Vector3(0.f, 2.f, 0.f), 1e-3f, "the title reaches its resting position by frame 30");
        check(midway.x > offFrame.x && midway.x < settled.x, "the title slides monotonically into place");

        scene.evaluate(0);
        checkNear(scene.worldOpacity(kTitle), 0.f, 1e-4f, "the title starts transparent");
        scene.evaluate(30);
        checkNear(scene.worldOpacity(kTitle), 1.f, 1e-4f, "the title is fully opaque by the end of the slide");
        scene.evaluate(60);
        checkNear(scene.worldOpacity(kTitle), 1.f, 1e-4f, "the title still holds its opacity at frame 60");
        scene.evaluate(90);
        checkNear(scene.worldOpacity(kTitle), 0.f, 1e-4f, "the title is transparent by frame 90");

        // The fade-out is eased, so pin its endpoints and its monotonicity rather
        // than a midpoint value the curve is free to choose.
        float previous = 1.f;
        for (const int frame : {60, 66, 72, 78, 84, 90}) {
            scene.evaluate(frame);
            const float opacity = scene.worldOpacity(kTitle);
            check(opacity <= previous + 1e-5f, "the title's fade-out never goes back up");
            previous = opacity;
        }
        scene.evaluate(75);
        const float midFade = scene.worldOpacity(kTitle);
        check(midFade > 0.f && midFade < 1.f, "the title is mid-fade at frame 75");
    }

    void productScalesAndControllerRotates() {

        section("canary: scale, rotation 3D and parenting");

        Canary canary = buildCanary();
        authorSchedule(canary);
        MotionScene& scene = canary.scene;

        scene.evaluate(15);
        checkNear(worldScaleXOf(scene, kProduct), 0.7f, 1e-3f, "the product starts at 70% scale");
        scene.evaluate(30);
        checkNear(worldScaleXOf(scene, kProduct), 0.85f, 1e-3f, "the product is halfway at the ease midpoint");
        scene.evaluate(45);
        checkNear(worldScaleXOf(scene, kProduct), 1.f, 1e-3f, "the product reaches 100% by frame 45");

        scene.evaluate(30);
        checkNear(worldRotationOf(scene, kController).angleTo(Quaternion(0.f, 0.f, 0.f, 1.f)), 0.f, 1e-3f,
                  "Null_Main starts unrotated at frame 30");
        scene.evaluate(60);
        checkNear(worldRotationOf(scene, kController).angleTo([] {
                      Quaternion q;
                      q.setFromAxisAngle(Vector3(0.f, 1.f, 0.f), math::degToRad(15.f));
                      return q;
                  }()),
                  0.f, 1e-3f, "Null_Main reaches 15° about Y at frame 60");
        scene.evaluate(45);
        const Quaternion expectedHalfway = [] {
            Quaternion q;
            q.setFromAxisAngle(Vector3(0.f, 1.f, 0.f), math::degToRad(7.5f));
            return q;
        }();
        checkNear(worldRotationOf(scene, kController).angleTo(expectedHalfway), 0.f, 1e-3f,
                  "Null_Main is halfway through its turn at frame 45");

        // Parenting *and* the text/image invariance, in one assertion: every child
        // inherits exactly the controller's world rotation, whether it draws text
        // or an image.
        for (Layer::Id child : {kTitle, kSubtitle, kProduct}) {
            checkNear(worldRotationOf(scene, child).angleTo(worldRotationOf(scene, kController)), 0.f, 1e-3f,
                      "every child inherits the controller's rotation, text and image alike");
        }
    }

    void cameraDolliesAndParallaxFollowsDepth() {

        section("canary: camera dolly and automatic parallax");

        Canary canary = buildCanary();
        authorSchedule(canary);
        MotionScene& scene = canary.scene;

        checkNear(cameraDistance(canary, 45), 14.f, 1e-3f, "the camera starts its push at distance 14");
        checkNear(cameraDistance(canary, 75), 10.f, 1e-3f, "the camera dolly closes to distance 10");
        const float midDistance = cameraDistance(canary, 60);
        check(midDistance < 14.f && midDistance > 10.f, "the dolly is monotone between its keys");

        // The camera faces its subject throughout.
        const CameraPose pose = canary.camera.sample(60.f * kFrameSeconds);
        Vector3 forward(0.f, 0.f, -1.f);
        forward.applyQuaternion(pose.orientation);
        checkNear(forward, Vector3(0.f, 0.f, -1.f), 1e-3f, "the camera keeps looking down the scene");

        // Automatic parallax: the nearer plane grows more as the camera pushes in,
        // purely because its distance shrinks more. No parallax was authored.
        auto camera = canary.camera.makeCamera();
        PerspectiveView view(*camera, Vector2(1920.f, 1080.f));

        scene.evaluate(45);
        canary.camera.apply(45.f * kFrameSeconds, *camera);
        const ScreenPlane backgroundBefore = view.projectPlane(canary.backgroundPlane, scene.worldMatrix(kBackground));
        const ScreenPlane productBefore = view.projectPlane(canary.productPlane, scene.worldMatrix(kProduct));

        scene.evaluate(75);
        canary.camera.apply(75.f * kFrameSeconds, *camera);
        const ScreenPlane backgroundAfter = view.projectPlane(canary.backgroundPlane, scene.worldMatrix(kBackground));
        const ScreenPlane productAfter = view.projectPlane(canary.productPlane, scene.worldMatrix(kProduct));

        check(backgroundBefore.visible && productBefore.visible, "both planes are on screen before the push");
        check(backgroundAfter.visible && productAfter.visible, "both planes are on screen after the push");
        check(productBefore.distance < backgroundBefore.distance, "the product plane is nearer than the background");

        const float backgroundGrowth = backgroundAfter.size.x / backgroundBefore.size.x;
        const float productGrowth = productAfter.size.x / productBefore.size.x;
        check(productGrowth > 1.f && backgroundGrowth > 1.f, "a dolly forward enlarges every plane");
        check(productGrowth > backgroundGrowth,
              "the nearer plane grows more: parallax follows Z-depth automatically");

        // And the screen-space parallax of a camera move is larger for the nearer plane.
        const Vector3 cameraDelta(1.f, 0.f, 0.f);
        check(std::fabs(view.parallaxShift(cameraDelta, productBefore).x) >
                      std::fabs(view.parallaxShift(cameraDelta, backgroundBefore).x),
              "a camera move shifts the nearer plane further than the background");
    }

    void subtitleTravelsACurvedPath() {

        section("canary: curved motion path");

        Canary canary = buildCanary();
        authorSchedule(canary);
        MotionScene& scene = canary.scene;

        // The path is authored in the layer's own space; the world composition on
        // top of it (the controller's turn) is pinned by the parenting section.
        const Layer* subtitle = scene.findLayer(kSubtitle);
        const Vector3 start = subtitle->sample(0.f).position;
        const Vector3 end = subtitle->sample(90.f * kFrameSeconds).position;
        const Vector3 mid = subtitle->sample(45.f * kFrameSeconds).position;

        checkNear(start, Vector3(-4.f, -2.f, -0.5f), 1e-3f, "the subtitle starts at its authored position");
        checkNear(end, Vector3(4.f, -2.f, -0.5f), 1e-3f, "the subtitle arrives at the target");

        const Vector3 chordMid = (start + end) * 0.5f;
        check(mid.y > chordMid.y + 1.f, "the subtitle travels a curve, not the straight chord");
        checkNear(mid.z, -0.5f, 1e-3f, "the curved travel keeps its Z depth");

        // Arced through the apex: the midpoint is nearer the apex than the chord.
        const Vector3 apex(0.f, 1.5f, -0.5f);
        check(mid.distanceTo(apex) < chordMid.distanceTo(apex),
              "the curved travel bulges toward the apex");

        // ... and the curve is smooth: consecutive samples are close together.
        const Vector3 before = subtitle->sample(44.f * kFrameSeconds).position;
        const Vector3 after = subtitle->sample(46.f * kFrameSeconds).position;
        check(before.distanceTo(after) < 1.f, "the curved travel is continuous across frames");
    }

    void canaryIsDeterministicAndAnchored() {

        section("canary: determinism, anchors and lifetime");

        Canary canary = buildCanary();
        authorSchedule(canary);
        MotionScene& scene = canary.scene;

        // Anchors: a plane is applied through the one anchor path, so its anchor
        // is the pivot the layer reports.
        checkNear(scene.findLayer(kBackground)->transform.anchor, canary.backgroundPlane.anchorPoint(), 0.f,
                  "the background's anchor is its plane's pivot");
        checkNear(scene.findLayer(kTitle)->transform.anchor, canary.titlePlane.anchorPoint(), 0.f,
                  "the title's anchor is its plane's pivot");

        // Lifetime: the background is alive across the whole canary.
        scene.evaluate(0);
        check(scene.visible(kBackground), "the background is visible at frame 0");
        scene.evaluate(90);
        check(scene.visible(kBackground), "the background is visible at frame 90");
        check(scene.contentEndFrame() >= 90, "the scene knows it runs at least 90 frames");

        // Two full evaluations of the same frame are bit-identical, for every layer.
        scene.evaluate(37);
        std::string first;
        for (Layer::Id id : {kBackground, kController, kTitle, kSubtitle, kProduct}) {
            for (unsigned int i = 0; i < 16; ++i) {
                first += std::to_string(scene.worldMatrix(id)[i]);
                first += ',';
            }
        }

        scene.evaluate(90);
        scene.evaluate(37);
        std::string second;
        for (Layer::Id id : {kBackground, kController, kTitle, kSubtitle, kProduct}) {
            for (unsigned int i = 0; i < 16; ++i) {
                second += std::to_string(scene.worldMatrix(id)[i]);
                second += ',';
            }
        }

        check(first == second, "evaluating a frame after another frame reproduces it exactly");
        check(!first.empty(), "the determinism check actually captured state");
    }

    // A second, small composition for the two capabilities the main canary does not
    // exercise on its own: a pivot that travels over time, and an entrance/exit
    // keyed from the layer's own declared lifetime. Nothing here is a special case
    // — it is the same Layer/Transform/Track evaluation as everywhere above.
    void thePivotTravelsAndTheLifetimeDrivesTheEntrance() {

        section("canary: animated pivot and lifetime-driven entrance/exit");

        constexpr Layer::Id kRig = 1;
        constexpr Layer::Id kCard = 2;

        MotionScene scene("pivot", kFps);

        Layer& rig = scene.addNull(kRig, "Card_Rig");
        rig.transform.position.set(0.f, 1.f, 0.f);// a controller offset, so world != local

        Layer& card = scene.addContent(kCard, "Card", ContentRef::image("card.png", Vector2(4.f, 4.f)), kRig);
        card.alive(10, 70);// 60 frames of lifetime, holding a 10-frame entrance and exit

        // The plane's pivot starts on the card's bottom-left corner ...
        MotionPlane plane = MotionPlane::sized(card.content, Vector2(4.f, 4.f));
        plane.anchor = Vector2(0.f, 0.f);
        plane.applyTo(card);

        // ... and travels to its top-right corner over frames 10-40, while the card
        // turns a quarter turn about whatever pivot it has at that instant.
        presets::anchorTo(card, Vector3(2.f, 2.f, 0.f), 10, 30, kFps);
        card.tracks.rotation.add(10.f * kFrameSeconds, Vector3(0.f, 0.f, 0.f), Easing::linear());
        card.tracks.rotation.add(40.f * kFrameSeconds, Vector3(0.f, 0.f, math::degToRad(90.f)),
                                Easing::linear());

        // The entrance and the exit come from the lifetime itself, never from
        // hand-picked frames: the slide starts on frame 10 and finishes on frame 70.
        presets::lifetimeSlide(card, presets::Direction::Left, presets::Direction::Right, 10, 5.f, kFps);

        const Layer* sampled = scene.findLayer(kCard);
        check(sampled != nullptr, "the card exists");

        const presets::LifetimeWindows windows = presets::lifetimeWindows(*sampled, 10);
        check(windows.hasIn && windows.hasOut, "the declared lifetime holds both the entrance and the exit");
        check(windows.in.inFrame == 10 && windows.out.outFrame == 70,
              "the entrance starts on the in frame and the exit finishes on the out frame");

        scene.evaluate(10);
        checkNear(worldPivotOf(scene, kCard), Vector3(-5.f, 1.f, 0.f), 1e-3f,
                  "the card enters from the lifetime's in frame, offset by its controller");
        checkNear(scene.worldOpacity(kCard), 0.f, 1e-4f, "the card enters transparent");

        scene.evaluate(20);
        checkNear(worldPivotOf(scene, kCard), Vector3(0.f, 1.f, 0.f), 1e-3f,
                  "the card settles on its resting position one window later");
        checkNear(scene.worldOpacity(kCard), 1.f, 1e-4f, "the card is fully opaque inside its lifetime");

        scene.evaluate(40);
        checkNear(worldPivotOf(scene, kCard), Vector3(0.f, 1.f, 0.f), 1e-3f,
                  "the card holds its position while only the pivot travels");

        const Transform pose = sampled->sample(40.f * kFrameSeconds);
        checkNear(pose.anchor, Vector3(2.f, 2.f, 0.f), 1e-4f, "the pivot has reached the far corner");
        checkNear(sampled->transform.anchor, Vector3(-2.f, -2.f, 0.f), 0.f,
                  "the static anchor is untouched: the pivot animation lives in the track, not the pose");
        // A point one unit right of the moved pivot has turned a quarter turn about
        // it, which is only true if the pivot really travelled with the animation.
        checkNear(pose.applyPoint(Vector3(3.f, 2.f, 0.f)), Vector3(0.f, 1.f, 0.f), 1e-4f,
                  "the card turns about the pivot it has at that frame, not the authored one");

        scene.evaluate(5);
        check(!scene.visible(kCard), "the card is hidden before its lifetime starts");
        scene.evaluate(75);
        check(!scene.visible(kCard), "the card is hidden after its lifetime ends");

        scene.evaluate(70);
        checkNear(worldPivotOf(scene, kCard), Vector3(5.f, 1.f, 0.f), 1e-3f,
                  "the exit finishes on the out frame");
        checkNear(scene.worldOpacity(kCard), 0.f, 1e-4f, "the exit fades out on the out frame");

        // The second composition is a pure function of the frame too.
        scene.evaluate(33);
        const Vector3 first = worldPivotOf(scene, kCard);
        scene.evaluate(70);
        scene.evaluate(33);
        checkNear(worldPivotOf(scene, kCard), first, 0.f,
                  "re-evaluating a frame reproduces it exactly");
    }

}// namespace

int main() {

    theSceneIsTheIntendedOne();
    titleSlidesAndFades();
    productScalesAndControllerRotates();
    cameraDolliesAndParallaxFollowsDepth();
    subtitleTravelsACurvedPath();
    canaryIsDeterministicAndAnchored();
    thePivotTravelsAndTheLifetimeDrivesTheEntrance();

    return chrononmotion_test::report();
}
