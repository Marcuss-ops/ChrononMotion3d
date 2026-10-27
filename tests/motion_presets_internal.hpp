#pragma once
#include "chrononmotion/chrononmotion.hpp"
#include "motion_check.hpp"
#include <algorithm>
#include <cmath>
#include <vector>
using namespace chrononmotion;
using namespace chrononmotion::motion;
using chrononmotion_test::check;
using chrononmotion_test::checkNear;
using chrononmotion_test::section;
namespace motion_presets_test {
inline constexpr float kFps = 30.f;

    inline float at(int frame) {

        return static_cast<float>(frame) / kFps;
    }

    inline Quaternion aboutY(float degrees) {

        Quaternion q;
        q.setFromAxisAngle(Vector3(0.f, 1.f, 0.f), math::degToRad(degrees));
        return q;
    }

    inline Quaternion aboutX(float degrees) {

        Quaternion q;
        q.setFromAxisAngle(Vector3(1.f, 0.f, 0.f), math::degToRad(degrees));
        return q;
    }

void slideAndFadePresets();
void scalePresets();
void rotationPresets();
void motionPresets();
void groupPresets();
void pivotAndLifetimePresets();
void cameraPresets();
void namedPresetAliases();
void presetTimingUsesFrames();
void environmentAndBackgroundPresets();
void settleAndWavePresets();
void documentaryPresets();
}
