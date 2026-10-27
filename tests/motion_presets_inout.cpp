#include "motion_presets_internal.hpp"
namespace motion_presets_test {


    void slideAndFadePresets() {

        section("slide, fade and depth presets");

        {
            Layer layer(1, "L");
            layer.transform.position.set(1.f, 0.f, 0.f);
            presets::slideIn(layer, presets::Direction::Left, 0, 30, 5.f, kFps);
            checkNear(layer.sample(at(0)).position, Vector3(-4.f, 0.f, 0.f), 1e-3f,
                      "slideIn starts the layer one distance away, in the named direction");
            checkNear(layer.sample(at(30)).position, Vector3(1.f, 0.f, 0.f), 1e-3f,
                      "slideIn settles on the resting position");
        }
        {
            Layer layer(1, "L");
            layer.transform.position.set(1.f, 2.f, 3.f);
            presets::slideOut(layer, presets::Direction::Down, 0, 30, 4.f, kFps);
            checkNear(layer.sample(at(0)).position, Vector3(1.f, 2.f, 3.f), 1e-3f, "slideOut starts at rest");
            checkNear(layer.sample(at(30)).position, Vector3(1.f, -2.f, 3.f), 1e-3f,
                      "slideOut leaves along the named direction");
        }
        {
            Layer layer(1, "L");
            presets::pushInZ(layer, 0, 30, -10.f, kFps);
            checkNear(layer.sample(at(0)).position.z, -10.f, 1e-3f, "pushInZ starts at the given depth");
            checkNear(layer.sample(at(30)).position.z, 0.f, 1e-3f, "pushInZ arrives at the resting depth");
        }
        {
            Layer layer(1, "L");
            presets::pullOutZ(layer, 30, 30, -20.f, kFps);
            checkNear(layer.sample(at(30)).position.z, 0.f, 1e-3f, "pullOutZ starts at the resting depth");
            checkNear(layer.sample(at(60)).position.z, -20.f, 1e-3f, "pullOutZ leaves toward the given depth");
        }
        {
            Layer layer(1, "L");
            layer.transform.opacity = 0.9f;
            presets::fadeIn(layer, 0, 30, kFps);
            checkNear(layer.sample(at(0)).opacity, 0.f, 1e-4f, "fadeIn starts transparent");
            checkNear(layer.sample(at(30)).opacity, 0.9f, 1e-4f, "fadeIn lands on the layer's own opacity");

            presets::fadeOut(layer, 60, 30, kFps);
            checkNear(layer.sample(at(60)).opacity, 0.9f, 1e-4f, "fadeOut starts from the layer's own opacity");
            checkNear(layer.sample(at(90)).opacity, 0.f, 1e-4f, "fadeOut ends transparent");
        }
        {
            Layer layer(1, "L");
            layer.transform.position.set(0.f, 0.f, -2.f);
            presets::fadeSlide(layer, presets::Direction::Right, 0, 30, 6.f, kFps);
            checkNear(layer.sample(at(0)).opacity, 0.f, 1e-4f, "fadeSlide fades in");
            checkNear(layer.sample(at(0)).position, Vector3(6.f, 0.f, -2.f), 1e-3f,
                      "fadeSlide also slides in");
            checkNear(layer.sample(at(30)).position, Vector3(0.f, 0.f, -2.f), 1e-3f,
                      "fadeSlide settles on the resting position");
            checkNear(layer.sample(at(30)).position.z, -2.f, 1e-4f, "fadeSlide keeps the Z depth");
        }
        {
            Layer layer(1, "L");
            presets::fadeScale(layer, 0, 30, 0.8f, kFps);
            checkNear(layer.sample(at(0)).scale.x, 0.8f, 1e-3f, "fadeScale starts at the given scale factor");
            checkNear(layer.sample(at(0)).opacity, 0.f, 1e-4f, "fadeScale starts transparent");
            checkNear(layer.sample(at(30)).scale.x, 1.f, 1e-3f, "fadeScale lands at full scale");
        }
        {
            Layer layer(1, "L");
            presets::parallaxEntrance(layer, 0, 30, -12.f, kFps);
            checkNear(layer.sample(at(0)).position.z, -12.f, 1e-3f, "parallaxEntrance starts deep");
            checkNear(layer.sample(at(30)).position.z, 0.f, 1e-3f, "parallaxEntrance arrives at depth 0");
        }
    }


    void scalePresets() {

        section("scale presets");

        {
            Layer layer(1, "L");
            presets::scalePop(layer, 0, 30, 0.5f, 1.5f, kFps);
            checkNear(layer.sample(at(0)).scale.x, 0.5f, 1e-3f, "scalePop starts at the given scale");
            checkNear(layer.sample(at(30)).scale.x, 1.5f, 1e-3f, "scalePop lands exactly on its target scale");

            // "Pop" is the overshoot: the value passes the target mid-segment.
            float peak = 0.f;
            for (int frame = 0; frame <= 30; ++frame) {
                peak = std::max(peak, layer.sample(at(frame)).scale.x);
            }
            check(peak > 1.5f, "scalePop overshoots past its target before settling");
        }
        {
            Layer layer(1, "L");
            presets::scaleOvershoot(layer, 0, 30, 0.6f, 1.f, kFps);
            checkNear(layer.sample(at(0)).scale.x, 0.6f, 1e-3f, "scaleOvershoot starts at the given scale");
            checkNear(layer.sample(at(30)).scale.x, 1.f, 1e-3f, "scaleOvershoot lands at full scale");

            float peak = 0.f;
            for (int frame = 0; frame <= 30; ++frame) {
                peak = std::max(peak, layer.sample(at(frame)).scale.x);
            }
            check(peak > 1.f, "scaleOvershoot overshoots past full scale");
        }
        {
            // The overshoot is uniform on all three axes.
            Layer layer(1, "L");
            presets::scalePop(layer, 0, 30, 0.5f, 1.5f, kFps);
            const Vector3 mid = layer.sample(at(15)).scale;
            checkNear(mid, Vector3(mid.x, mid.x, mid.x), 1e-5f, "scale presets scale uniformly");
        }
    }


    void rotationPresets() {

        section("rotation presets");

        {
            Layer layer(1, "L");
            presets::flip(layer, presets::Axis::Y, 0, 30, kFps);
            checkNear(layer.sample(at(0)).orientation.angleTo(Quaternion(0.f, 0.f, 0.f, 1.f)), 0.f, 1e-3f,
                      "flip starts from the identity orientation");
            checkNear(layer.sample(at(30)).orientation.angleTo(aboutY(180.f)), 0.f, 1e-3f,
                      "flip about Y ends at 180°, the short way");
            checkNear(layer.sample(at(15)).orientation.angleTo(Quaternion(0.f, 0.f, 0.f, 1.f)),
                      math::PI * 0.5f, 1e-3f, "flip travels half the angle at its midpoint");
        }
        {
            Layer layer(1, "L");
            presets::flip(layer, presets::Axis::X, 0, 30, kFps, 90.f);
            checkNear(layer.sample(at(30)).orientation.angleTo(aboutX(90.f)), 0.f, 1e-3f,
                      "flip about X honors the given angle");
        }
        {
            Layer layer(1, "L");
            presets::cardTurn(layer, 0, 30, kFps);
            checkNear(layer.sample(at(0)).orientation.angleTo(aboutY(-90.f)), 0.f, 1e-3f,
                      "cardTurn starts folded a quarter turn away");
            checkNear(layer.sample(at(30)).orientation.angleTo(Quaternion(0.f, 0.f, 0.f, 1.f)), 0.f, 1e-3f,
                      "cardTurn lands facing the viewer");
        }
        {
            Layer layer(1, "L");
            layer.transform.rotation.set(0.1f, 0.2f, 0.3f);
            presets::tilt3D(layer, 0, 30, 20.f, kFps);
            checkNear(layer.sample(at(0)).rotation.x, 0.1f, 1e-4f, "tilt3D starts from the resting rotation");
            checkNear(layer.sample(at(30)).rotation.x, 0.1f + math::degToRad(20.f), 1e-4f,
                      "tilt3D adds its angle about X");
            checkNear(layer.sample(at(30)).rotation.y, 0.2f, 1e-4f, "tilt3D leaves the other axes alone");
        }
        {
            Layer layer(1, "L");
            presets::spinZ(layer, 0, 30, 1.f, kFps);
            checkNear(layer.sample(at(0)).rotation.z, 0.f, 1e-4f, "spinZ starts unrotated");
            checkNear(layer.sample(at(30)).rotation.z, math::TWO_PI, 1e-3f, "spinZ completes one full turn");
        }
        {
            Layer layer(1, "L");
            presets::spinXYZ(layer, 0, 30, 1.f, kFps);
            checkNear(layer.sample(at(0)).orientation.angleTo(Quaternion(0.f, 0.f, 0.f, 1.f)), 0.f, 1e-3f,
                      "spinXYZ starts from the identity orientation");
            checkNear(layer.sample(at(30)).orientation.angleTo(Quaternion(0.f, 0.f, 0.f, 1.f)), 0.f, 1e-3f,
                      "a full spinXYZ turn comes back to the identity orientation");
            // One sixth of the way is one sixth of the turn: 60° of the 360°.
            const Quaternion sixth = layer.sample(at(5)).orientation;
            checkNear(sixth.angleTo(Quaternion(0.f, 0.f, 0.f, 1.f)), math::PI / 3.f, 1e-2f,
                      "spinXYZ rotates continuously rather than snapping between keys");
        }
    }


    void motionPresets() {

        section("drift, hover and motion-path presets");

        {
            Layer layer(1, "L");
            layer.transform.position.set(1.f, 1.f, 1.f);
            presets::drift(layer, 0, 30, Vector3(3.f, -2.f, 1.f), kFps);
            checkNear(layer.sample(at(0)).position, Vector3(1.f, 1.f, 1.f), 1e-4f, "drift starts at rest");
            checkNear(layer.sample(at(15)).position, Vector3(2.5f, 0.f, 1.5f), 1e-3f,
                      "drift is constant-speed, so it is halfway at the midpoint");
            checkNear(layer.sample(at(30)).position, Vector3(4.f, -1.f, 2.f), 1e-3f,
                      "drift ends at the resting position plus the offset");
        }
        {
            Layer layer(1, "L");
            layer.transform.position.set(0.f, 5.f, 0.f);
            presets::floatHover(layer, 0, 40, 2.f, kFps);
            checkNear(layer.sample(at(0)).position.y, 5.f, 1e-3f, "hover starts at rest");
            checkNear(layer.sample(at(10)).position.y, 7.f, 1e-3f, "hover rises by the amplitude");
            checkNear(layer.sample(at(20)).position.y, 5.f, 1e-3f, "hover returns to rest");
            checkNear(layer.sample(at(30)).position.y, 3.f, 1e-3f, "hover dips by the amplitude");
            checkNear(layer.sample(at(40)).position.y, 5.f, 1e-3f, "hover ends at rest");
        }
        {
            Layer layer(1, "L");
            layer.transform.position.set(0.f, 0.f, 0.f);
            presets::arcMotion(layer, Vector3(10.f, 0.f, 0.f), Vector3(5.f, 8.f, 0.f), 0, 30, kFps);
            checkNear(layer.sample(at(0)).position, Vector3(0.f, 0.f, 0.f), 1e-3f, "arcMotion starts at rest");
            checkNear(layer.sample(at(30)).position, Vector3(10.f, 0.f, 0.f), 1e-3f, "arcMotion ends at its target");
            check(layer.sample(at(15)).position.y > 3.f, "arcMotion travels a curve through its apex");
        }
        {
            Layer layer(1, "L");
            const MotionPath path = MotionPath::line(Vector3(0.f, 0.f, 0.f), Vector3(9.f, 0.f, 0.f));
            presets::pathMotion(layer, path, 0, 30, kFps, true, 9);
            checkNear(layer.sample(at(0)).position, Vector3(0.f, 0.f, 0.f), 1e-4f, "pathMotion starts on the path");
            checkNear(layer.sample(at(30)).position, Vector3(9.f, 0.f, 0.f), 1e-4f, "pathMotion ends on the path");
            checkNear(layer.sample(at(15)).position, Vector3(4.5f, 0.f, 0.f), 1e-3f,
                      "pathMotion samples uniformly along the path");

            Vector3 alongZ(0.f, 0.f, 1.f);
            alongZ.applyQuaternion(layer.sample(at(15)).orientation);
            checkNear(alongZ, Vector3(1.f, 0.f, 0.f), 1e-3f, "pathMotion can orient the layer along the path");
        }
        {
            Layer layer(1, "L");
            const MotionPath path = MotionPath::catmullRom(
                    {Vector3(0.f, 0.f, 0.f), Vector3(3.f, 2.f, 0.f), Vector3(6.f, 0.f, 0.f)});
            presets::pathMotion(layer, path, 0, 30, kFps, false, 12);
            check(layer.sample(at(15)).position.y > 0.5f, "pathMotion follows a curved catmull-rom path");
        }
    }
}
