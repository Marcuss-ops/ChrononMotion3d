// T19-B camera authority: sample one ChrononMotion3d CameraRig track and emit
// canonical Chronon3d camera samples for the V3 plan generator.
#include "chrononmotion/motion/CameraRig.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
struct Key {
    int frame;
    float px, py, pz, tx, ty, tz, focus;
};
void require(bool ok, const char* why) {
    if (!ok) throw std::runtime_error(why);
}
}

int main(int argc, char** argv) try {
    require(argc == 4 || argc == 5,
            "usage: t19b_camera_rig_contract keys.csv samples.csv report.json [camera-samples.csv]");
    std::ifstream input(argv[1]);
    require(bool(input), "cannot open T19-B camera key CSV");
    std::vector<Key> keys;
    std::string line;
    while (std::getline(input, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::replace(line.begin(), line.end(), ',', ' ');
        std::istringstream row(line);
        Key key{};
        row >> key.frame >> key.px >> key.py >> key.pz >> key.tx >> key.ty >> key.tz >> key.focus;
        require(bool(row), "malformed T19-B camera key CSV row");
        keys.push_back(key);
    }
    require(keys.size() >= 2 && keys.front().frame == 0 && keys.back().frame == 299,
            "camera keys must span the 300-frame timeline");

    chrononmotion::motion::CameraRig rig(1901, 45.f, 16.f / 9.f, 1.f, 5000.f);
    rig.setPosition({keys.front().px, keys.front().py, -keys.front().pz})
       .setTarget({keys.front().tx, keys.front().ty, -keys.front().tz})
       .setFov(45.f).setAperture(0.018f).setMaxBlur(32.f)
       .setFocusDistance(keys.front().focus);
    for (const auto& key : keys) {
        const float time = float(key.frame) / 30.f;
        rig.positionTrack().add(time, {key.px, key.py, -key.pz});
        rig.targetTrack().add(time, {key.tx, key.ty, -key.tz});
        rig.focusDistanceTrack().add(time, key.focus);
    }

    std::ofstream output(argv[2]);
    require(bool(output), "cannot create sampled CameraRig CSV");
    output << "frame,camera_x,camera_y,camera_z,target_x,target_y,target_z,focus_distance,fov_deg\n";
    float maxStep = 0.f;
    chrononmotion::Vector3 previous{};
    float firstOrbit = 0.f, lastOrbit = 0.f;
    for (int frame = 0; frame < 300; ++frame) {
        const auto pose = rig.sample(float(frame) / 30.f);
        const chrononmotion::Vector3 canonicalPosition{pose.position.x, pose.position.y, -pose.position.z};
        const chrononmotion::Vector3 canonicalTarget{pose.target.x, pose.target.y, -pose.target.z};
        if (frame) maxStep = std::max(maxStep, previous.distanceTo(pose.position));
        previous = pose.position;
        output << frame << ',' << canonicalPosition.x << ',' << canonicalPosition.y << ',' << canonicalPosition.z
               << ',' << canonicalTarget.x << ',' << canonicalTarget.y << ',' << canonicalTarget.z
               << ',' << pose.focusDistance << ',' << pose.fov << '\n';
        if (frame == 45 || frame == 105) {
            const float angle = std::atan2(pose.position.x, pose.position.z) * 180.f / 3.14159265358979323846f;
            if (frame == 45) firstOrbit = angle;
            else lastOrbit = angle;
        }
    }
    require(maxStep < 12.f, "CameraRig has a discontinuity or excessive frame-to-frame translation");
    require(std::abs(lastOrbit - firstOrbit) >= 13.f,
            "CameraRig orbit did not cover the requested 14-degree sweep");
    require(std::abs(rig.sample(105.f / 30.f).focusDistance - 550.f) < 0.1f &&
            std::abs(rig.sample(165.f / 30.f).focusDistance - 1150.f) < 0.1f,
            "CameraRig rack-focus endpoints do not match the astronaut and lunar module planes");
    std::ofstream report(argv[3]);
    require(bool(report), "cannot create CameraRig report");
    report << "{\n  \"status\": \"PASS\",\n  \"authority\": \"ChrononMotion3d CameraRig sampled at every output frame\",\n"
           << "  \"frames\": 300,\n  \"fov_deg\": 45,\n  \"orbit_delta_deg\": " << std::abs(lastOrbit - firstOrbit)
           << ",\n  \"max_position_step\": " << maxStep
           << ",\n  \"focus_distance_frame_105\": 550,\n  \"focus_distance_frame_165\": 1150,\n"
           << "  \"canonical_z_convention\": \"camera looks +Z; adapter reflects Z for threepp\"\n}\n";
    if (argc == 5) {
        std::ofstream samples(argv[4]);
        require(bool(samples), "cannot create sampled CameraRig CSV");
        samples << "frame,camera_x,camera_y,camera_z,target_x,target_y,target_z,focus_distance,fov_deg\n";
        for (int frame = 0; frame < 300; ++frame) {
            const auto pose = rig.sample(float(frame) / 30.f);
            samples << frame << ',' << pose.position.x << ',' << pose.position.y << ',' << -pose.position.z
                    << ',' << pose.target.x << ',' << pose.target.y << ',' << -pose.target.z
                    << ',' << pose.focusDistance << ',' << pose.fov << '\n';
        }
    }
    std::cout << "T19-B CameraRig PASS: 300 deterministic samples, orbit="
              << std::abs(lastOrbit - firstOrbit) << " deg, max step=" << maxStep << '\n';
    return 0;
} catch (const std::exception& e) {
    std::cerr << "T19-B CameraRig FAIL: " << e.what() << '\n';
    return 1;
}
