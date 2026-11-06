// Particle autoringshowcase: Motion -> lowerToRenderPlan -> RenderPlan JSON.
//
// The three scenes are deliberately asset-free so they can be rendered by the
// Chronon3D CLI without a content pack. They exercise the authored particle
// layer, colour ramps, blend modes, environmental bokeh and a positioned
// emitter. Output is the plan only; the renderer owns pixels and encoding.

#include "chrononmotion/motion/MotionScene.hpp"
#include "chrononmotion/motion/Particles.hpp"
#include "chrononmotion/motion/RenderPlanEmission.hpp"
#include "chrononmotion/motion/RenderPlanLowering.hpp"

#include <chronon3d/render_plan/render_plan.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace chrononmotion::motion;

namespace {

constexpr int kWidth = 1280;
constexpr int kHeight = 720;
constexpr int kFrames = 120;
constexpr float kFps = 30.f;

void writePlan(const std::filesystem::path& outputDir, const std::string& name,
               ParticleEmitter emitter, chrononmotion::Vector3 position = {}) {
    MotionScene scene(name, kFps);
    auto& layer = scene.addParticles(
        1, name, ContentRef::particles(name, {static_cast<float>(kWidth), static_cast<float>(kHeight)}),
        std::move(emitter));
    layer.flat().alive(0, kFrames - 1).positionedAt(position);

    RenderPlanLoweringOptions options;
    options.width = kWidth;
    options.height = kHeight;
    options.jobId = name;
    options.outputPath = (outputDir / (name + ".png")).string();
    options.schema = chronon3d::render_plan::kRenderPlanSchemaV3;
    options.requireResolvedLayerSamples = true;

    auto plan = lowerToRenderPlan(scene, options);
    if (const auto problem = renderPlanEmissionProblem(plan); !problem.empty())
        throw std::runtime_error(name + ": " + problem);

    const auto path = outputDir / (name + ".plan.json");
    std::ofstream file(path, std::ios::binary);
    if (!file || !(file << renderPlanToJson(plan)))
        throw std::runtime_error("cannot write " + path.string());
    std::cout << path.string() << '\n';
}

ParticleEmitter dust() {
    ParticleEmitter emitter;
    emitter.count = 720;
    emitter.seed = 0xD057U;
    emitter.emitterSize = {static_cast<float>(kWidth), static_cast<float>(kHeight)};
    emitter.velocityMin = {-7.f, -18.f};
    emitter.velocityMax = {7.f, -3.f};
    emitter.lifetimeMin = 3.f;
    emitter.lifetimeMax = 6.5f;
    emitter.sizeMin = 1.2f;
    emitter.sizeMax = 5.5f;
    emitter.turbulence = 14.f;
    emitter.blendMode = ParticleBlendMode::Screen;
    emitter.colorRamp = {
        {0.f, 0.55f, 0.82f, 1.f, 0.f},
        {0.22f, 0.72f, 0.94f, 1.f, 0.55f},
        {0.72f, 0.86f, 1.f, 0.92f, 0.28f},
        {1.f, 1.f, 0.82f, 0.55f, 0.f}};
    return emitter;
}

ParticleEmitter embers() {
    ParticleEmitter emitter;
    emitter.count = 520;
    emitter.seed = 0xE0B3U;
    emitter.emitterSize = {static_cast<float>(kWidth) * 0.72f, 140.f};
    emitter.velocityMin = {-24.f, -105.f};
    emitter.velocityMax = {24.f, -28.f};
    emitter.lifetimeMin = 1.4f;
    emitter.lifetimeMax = 3.2f;
    emitter.sizeMin = 1.f;
    emitter.sizeMax = 4.5f;
    emitter.turbulence = 5.f;
    emitter.rotationMin = -0.8f;
    emitter.rotationMax = 0.8f;
    emitter.angularVelocityMin = -1.2f;
    emitter.angularVelocityMax = 1.2f;
    emitter.blendMode = ParticleBlendMode::Add;
    emitter.colorRamp = {
        {0.f, 1.f, 0.92f, 0.55f, 0.f},
        {0.16f, 1.f, 0.62f, 0.16f, 1.f},
        {0.70f, 0.92f, 0.22f, 0.04f, 0.52f},
        {1.f, 0.25f, 0.03f, 0.01f, 0.f}};
    return emitter;
}

ParticleEmitter bokeh() {
    ParticleEmitter emitter;
    emitter.count = 320;
    emitter.seed = 0xB0C4U;
    emitter.emitterSize = {static_cast<float>(kWidth), static_cast<float>(kHeight)};
    emitter.velocityMin = {-2.5f, -3.5f};
    emitter.velocityMax = {2.5f, 2.5f};
    emitter.lifetimeMin = 4.f;
    emitter.lifetimeMax = 8.f;
    emitter.sizeMin = 14.f;
    emitter.sizeMax = 42.f;
    emitter.turbulence = 1.5f;
    emitter.blendMode = ParticleBlendMode::Screen;
    emitter.environment = true;
    emitter.bokeh = true;
    emitter.depthMin = 0.05f;
    emitter.depthMax = 0.95f;
    emitter.colorRamp = {
        {0.f, 0.55f, 0.72f, 1.f, 0.f},
        {0.28f, 0.68f, 0.86f, 1.f, 0.42f},
        {0.70f, 1.f, 0.78f, 0.92f, 0.22f},
        {1.f, 1.f, 0.96f, 0.86f, 0.f}};
    return emitter;
}

}// namespace

int main(int argc, char** argv) {
    try {
        const std::filesystem::path outputDir =
            argc > 1 ? std::filesystem::path(argv[1])
                     : std::filesystem::path("ChrononMotion3D/out/particles_showcase");
        std::filesystem::create_directories(outputDir);
        writePlan(outputDir, "01_dust_drift", dust());
        writePlan(outputDir, "02_ember_rise", embers(), {0.f, 260.f, 0.f});
        writePlan(outputDir, "03_bokeh_bloom", bokeh());
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "particles showcase: " << error.what() << '\n';
        return 1;
    }
}
