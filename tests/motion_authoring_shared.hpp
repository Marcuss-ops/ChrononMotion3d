#pragma once

#include "chrononmotion/chrononmotion.hpp"
#include "chrononmotion/motion/OfflineBaking.hpp"
#include "motion_check.hpp"

#include <array>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace chrononmotion_test::authoring {
using namespace chrononmotion;
using namespace chrononmotion::motion;
using chrononmotion_test::check;
using chrononmotion_test::checkNear;
using chrononmotion_test::section;

template<class Function>
inline bool throwsInvalid(Function&& function) {
    try { function(); }
    catch (const std::invalid_argument&) { return true; }
    return false;
}

void testVerletAndRigidBodyBaking();
void testTwoSegmentIk();
void testThreeDimensionalIkChain();
void testPcmAudioAnalysis();
void testPresetTrackGolden();
void testDynamicFlowLayout();
void testVectorPathBaking();

#if defined(CHRONONMOTION_HAS_CHRONON3D_ADAPTER)
void testTypedChronon3dLowering();
#endif

} // namespace chrononmotion_test::authoring
