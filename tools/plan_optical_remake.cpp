// New optical remake: four authored RenderPlan v3 scenes built in C++.
// Usage: chrononmotion_plan_optical_remake [OUTPUT_DIR]
// The PNG assets live in OUTPUT_DIR/assets and are referenced relative to it.
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace {

constexpr int kWidth = 1920;
constexpr int kHeight = 1080;
constexpr float kFps = 30.f;

struct Sample {
    float x = 0.f, y = 0.f, z = 0.f;
    float sx = 1.f, sy = 1.f, sz = 1.f;
    float rx = 0.f, ry = 0.f, rz = 0.f;
    float opacity = 1.f;
};

float clamp01(float t) { return std::clamp(t, 0.f, 1.f); }
float easeInOut(float t) {
    t = clamp01(t);
    return t * t * (3.f - 2.f * t);
}
json vec3(float x, float y, float z) { return json::array({x, y, z}); }

json sampleJson(int frame, const Sample& s) {
    return {
        {"frame", frame},
        {"position", vec3(s.x, s.y, s.z)},
        {"scale", vec3(s.sx, s.sy, s.sz)},
        {"rotation_deg", vec3(s.rx, s.ry, s.rz)},
        {"opacity", s.opacity}
    };
}

json imageLayer(int id, const std::string& asset, float w, float h, int frames,
                const std::function<Sample(float)>& at) {
    const Sample first = at(0.f);
    json layer = {
        {"id", std::to_string(id)},
        {"type", "image"},
        {"asset", asset},
        {"color", json::array({1.f, 1.f, 1.f, 1.f})},
        {"size", json::array({w, h})},
        {"position", vec3(first.x, first.y, first.z)},
        {"scale", vec3(first.sx, first.sy, first.sz)},
        {"rotation", vec3(first.rx, first.ry, first.rz)},
        {"opacity", first.opacity},
        {"enable_3d", true},
        {"screen_space", false},
        {"loop", false},
        {"fit", "contain"},
        {"start_frame", 0},
        {"duration_frames", frames},
        {"transform_samples", json::array()}
    };
    for (int i = 0; i < frames; ++i)
        layer["transform_samples"].push_back(sampleJson(i, at(static_cast<float>(i) / kFps)));
    return layer;
}

json textLayer(const std::string& id, const std::string& text, float x, float y,
               float width, float fontSize, const std::string& fill, int frames) {
    return {
        {"id", id},
        {"type", "text"},
        {"text", text},
        {"position", json::array({x, y})},
        {"size", json::array({width, fontSize * 1.5f})},
        {"style", {
            {"font", "assets/Inter-Bold.ttf"},
            {"font_size", fontSize},
            {"fill", fill}
        }},
        {"opacity", 1.f},
        {"screen_space", true},
        {"start_frame", 0},
        {"duration_frames", frames}
    };
}

json cameraAt(float focus, float fstop, float maxBlur) {
    const bool dof = fstop > 0.f;
    return {
        {"type", "perspective"},
        {"fov_deg", 50.f},
        {"near", 1.f},
        {"far", 10000.f},
        {"zoom", 1.f},
        {"sensor_width_mm", 36.f},
        {"sensor_height_mm", 24.f},
        {"lens_shift", json::array({0.f, 0.f})},
        {"iris_blades", 0},
        {"position", vec3(0.f, 0.f, -1000.f)},
        {"rotation_deg", vec3(0.f, 0.f, 0.f)},
        {"f_stop", fstop},
        {"dof", {
            {"enabled", dof},
            {"focus_distance", focus},
            {"max_blur", maxBlur},
            {"highlight_gain", 0.f}
        }}
    };
}

json makePlan(const std::string& name, int frames, const json& layers,
              const std::function<json(float)>& cameraAtTime) {
    json plan = {
        {"schema", "chronon.render-plan.v3"},
        {"version", 3},
        {"job_id", "optical_remake_" + name},
        {"canvas", {
            {"width", kWidth}, {"height", kHeight},
            {"fps_num", 30}, {"fps_den", 1},
            {"duration_frames", frames}
        }},
        {"output", {{"path", name + ".mp4"}, {"format", "mp4"}}},
        {"layers", layers},
        {"camera_samples", json::array()}
    };
    for (int i = 0; i < frames; ++i) {
        const float t = static_cast<float>(i) / kFps;
        plan["camera_samples"].push_back({{"frame", i}, {"camera", cameraAtTime(t)}});
    }
    plan["camera"] = cameraAtTime(0.f);
    return plan;
}

void writePlan(const fs::path& dir, const std::string& name, const json& plan) {
    std::ofstream out(dir / (name + ".plan.json"), std::ios::binary);
    if (!out) throw std::runtime_error("cannot open " + name + ".plan.json");
    out << plan.dump(2) << '\n';
    if (!out) throw std::runtime_error("cannot write " + name + ".plan.json");
}

json samuraiScene(int frames) {
    json layers = json::array();
    layers.push_back(imageLayer(1, "assets/samurai_bg.png", 2400.f, 1350.f, frames,
        [&](float t) {
            const float e = easeInOut(t / 5.f);
            Sample s;
            s.x = 10.f * e;
            s.y = -8.f * e;
            s.sx = s.sy = s.sz = 1.f + 0.06f * e;
            return s;
        }));
    layers.push_back(imageLayer(2, "assets/light_sweep.png", 700.f, 1350.f, frames,
        [&](float t) {
            Sample s;
            s.x = -1250.f + 2700.f * easeInOut(t / 3.8f);
            s.y = 0.f;
            s.z = -180.f;
            s.rz = 7.f;
            s.opacity = 0.62f * (1.f - std::abs(2.f * clamp01(t / 3.8f) - 1.f));
            return s;
        }));
    layers.push_back(textLayer("title", "SAMURAI", 820.f, 820.f, 1500.f, 96.f, "#F4F6F2", frames));
    layers.push_back(textLayer("kicker", "A STUDY IN STEEL // 01", 820.f, 730.f, 900.f, 26.f, "#C9B27A", frames));
    return makePlan("samurai_focus", frames, layers,
        [](float) { return cameraAt(1000.f, 0.f, 0.f); });
}

json astronautScene(int frames) {
    json layers = json::array();
    layers.push_back(imageLayer(1, "assets/earth_bg.png", 2400.f, 1350.f, frames,
        [&](float t) {
            const float e = easeInOut(t / 5.f);
            Sample s;
            s.x = -22.f * e;
            s.y = 6.f * e;
            s.sx = s.sy = s.sz = 1.03f + 0.07f * e;
            return s;
        }));
    layers.push_back(imageLayer(2, "assets/glow_amber.png", 760.f, 520.f, frames,
        [&](float t) {
            const float e = easeInOut(t / 5.f);
            Sample s;
            s.x = 70.f;
            s.y = -50.f;
            s.z = 100.f;
            s.sx = s.sy = s.sz = 0.9f + 0.16f * e;
            s.opacity = 0.42f;
            return s;
        }));
    layers.push_back(imageLayer(3, "assets/astronaut.png", 620.f, 620.f, frames,
        [&](float t) {
            const float e = easeInOut(t / 5.f);
            Sample s;
            s.x = 80.f;
            s.y = -45.f + 18.f * e;
            s.z = 0.f;
            s.rz = -4.f + 6.f * e;
            return s;
        }));
    layers.push_back(textLayer("title", "LOW ORBIT", 820.f, 820.f, 1500.f, 96.f, "#F4F6F2", frames));
    layers.push_back(textLayer("kicker", "408 KM // ATLANTIC", 820.f, 730.f, 900.f, 26.f, "#8FD8FF", frames));
    return makePlan("astronaut_orbit", frames, layers,
        [](float) { return cameraAt(1000.f, 0.f, 0.f); });
}

json harborScene(int frames) {
    json layers = json::array();
    layers.push_back(imageLayer(1, "assets/port_bg.png", 2400.f, 1350.f, frames,
        [&](float t) {
            const float e = easeInOut(t / 5.f);
            Sample s;
            s.x = -55.f + 110.f * e;
            s.y = 4.f - 8.f * e;
            s.sx = s.sy = s.sz = 1.02f + 0.06f * e;
            return s;
        }));
    layers.push_back(imageLayer(2, "assets/light_sweep.png", 700.f, 1350.f, frames,
        [&](float t) {
            Sample s;
            s.x = -1250.f + 2700.f * easeInOut(t / 4.2f);
            s.z = -180.f;
            s.rz = 5.f;
            s.opacity = 0.55f * (1.f - std::abs(2.f * clamp01(t / 4.2f) - 1.f));
            return s;
        }));
    layers.push_back(textLayer("title", "PORTO // DAWN", 830.f, 820.f, 1600.f, 92.f, "#F4F6F2", frames));
    layers.push_back(textLayer("kicker", "INDUSTRIAL COAST // 02", 830.f, 730.f, 900.f, 26.f, "#E0A76A", frames));
    return makePlan("harbor_dolly", frames, layers,
        [](float) { return cameraAt(1000.f, 0.f, 0.f); });
}

json carouselScene(int frames) {
    json layers = json::array();
    layers.push_back(json{
        {"id", "bg"},
        {"type", "image"},
        {"asset", "assets/port_bg.png"},
        {"color", json::array({1.f, 1.f, 1.f, 1.f})},
        {"size", json::array({1920.f, 1080.f})},
        {"position", json::array({0.f, 0.f})},
        {"opacity", 0.55f},
        {"enable_3d", false},
        {"screen_space", true},
        {"loop", false},
        {"fit", "cover"},
        {"start_frame", 0},
        {"duration_frames", frames}
    });
    layers.push_back(imageLayer(5, "assets/glow_amber.png", 1200.f, 840.f, frames,
        [&](float) {
            Sample s;
            s.x = 0.f;
            s.y = -40.f;
            s.z = 250.f;
            s.opacity = 0.30f;
            return s;
        }));
    layers.push_back(imageLayer(1, "assets/card_port.png", 3200.f, 2133.f, frames,
        [&](float t) {
            const float e = easeInOut(t / 6.f);
            Sample s;
            s.x = -1800.f * e;
            s.y = -40.f;
            s.z = 0.f;
            s.rz = -2.f;
            s.opacity = 1.f - 0.20f * e;
            return s;
        }));
    layers.push_back(imageLayer(2, "assets/card_samurai.png", 3200.f, 2133.f, frames,
        [&](float t) {
            const float e = easeInOut(t / 6.f);
            Sample s;
            s.x = 1800.f - 1800.f * e;
            s.y = 70.f;
            s.z = 300.f;
            s.rz = 2.f - 2.f * e;
            s.opacity = 0.85f + 0.15f * e;
            return s;
        }));
    layers.push_back(imageLayer(3, "assets/card_astronaut.png", 3200.f, 2133.f, frames,
        [&](float t) {
            const float e = easeInOut(t / 6.f);
            Sample s;
            s.x = -1800.f - 1800.f * e;
            s.y = -100.f;
            s.z = 150.f;
            s.opacity = 0.55f - 0.30f * e;
            return s;
        }));
    layers.push_back(imageLayer(4, "assets/card_einstein.png", 2000.f, 2000.f, frames,
        [&](float t) {
            const float e = easeInOut(t / 6.f);
            Sample s;
            s.x = 3600.f - 1800.f * e;
            s.y = -60.f;
            s.z = 500.f;
            s.opacity = 0.50f + 0.35f * e;
            return s;
        }));
    layers.push_back(textLayer("archive_label", "ARCHIVE", 420.f, 104.f, 700.f, 58.f, "#F4F6F2", frames));
    return makePlan("archive_carousel", frames, layers,
        [](float) { return cameraAt(1000.f, 4.f, 4.f); });
}

} // namespace

int main(int argc, char** argv) try {
    if (argc > 2) {
        std::cerr << "usage: " << argv[0] << " [OUTPUT_DIR]\n";
        return 2;
    }
    const fs::path outDir = argc == 2 ? fs::path(argv[1]) : fs::current_path();
    fs::create_directories(outDir);
    writePlan(outDir, "samurai_focus", samuraiScene(150));
    writePlan(outDir, "astronaut_orbit", astronautScene(150));
    writePlan(outDir, "harbor_dolly", harborScene(150));
    writePlan(outDir, "archive_carousel", carouselScene(180));
    std::cout << "wrote four remake plans to " << outDir << '\n';
    return 0;
} catch (const std::exception& e) {
    std::cerr << "plan_optical_remake: " << e.what() << '\n';
    return 1;
}
