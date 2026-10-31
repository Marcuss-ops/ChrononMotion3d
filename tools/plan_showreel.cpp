// ChrononMotion — the plan showreel.
//
// The one place in this tree that turns a Motion scene into a renderable file:
// it authors a scene with the motion vocabulary, lowers it through the offline
// adapter, and writes the `chronon.render-plan.v2` document that Chronon3D's CLI
// consumes. Nothing here re-implements the lowering or the plan format — the
// point is the opposite, to exercise the real path end to end:
//
//     MotionScene  ->  lowerToRenderPlan  ->  renderPlanToJson  ->  .plan.json
//
// Every animation below is one of the engine's own primitives, so the clip is a
// record of what the engine does rather than a hand-keyed approximation of it.
// The content (the words, the font, the fill) is caller-supplied, exactly as the
// adapter requires: this side owns space and time, Chronon owns pixels.
//
// Usage: chrononmotion_plan_showreel [output.plan.json] [font-asset] [frames]

#include "chrononmotion/motion/RenderPlanEmission.hpp"
#include "chrononmotion/motion/RenderPlanLowering.hpp"

#include "chrononmotion/motion/MotionScene.hpp"
#include "chrononmotion/motion/Presets.hpp"

#include <chronon3d/render_plan/render_plan.hpp>

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <unordered_map>

using namespace chrononmotion;
using namespace chrononmotion::motion;

namespace {

    constexpr int kWidth = 1920;
    constexpr int kHeight = 1080;
    constexpr float kFps = 30.f;
    constexpr int kFrames = 150;
    constexpr float kRowStep = 110.f;

    /// The content identity a text layer carries. Chronon owns the bytes; this
    /// side only says which asset to draw and how big its box is.
    chronon3d::render_plan::LayerPlan textPlan(const std::string& words, const std::string& font,
                                              float fontSize, const std::string& fill, float glow) {

        chronon3d::render_plan::LayerPlan plan;
        plan.type = chronon3d::render_plan::LayerType::Text;
        plan.text = words;

        chronon3d::render_plan::LayerStylePlan style;
        style.font = font;
        style.font_size = fontSize;
        style.fill = fill;
        if (glow > 0.f) {
            chronon3d::render_plan::GlowStyle halo;
            halo.radius = glow;
            halo.intensity = 0.55f;
            halo.color = fill;
            style.glow = halo;
        }
        plan.style = style;
        return plan;
    }

    struct Showreel {
        MotionScene scene{"chrononmotion_showreel", kFps};
        std::unordered_map<std::string, chronon3d::render_plan::LayerPlan> content{};

        /// Author one text row and hand the content owner its plan.
        Layer& row(Layer::Id id, const std::string& words, const std::string& font, float fontSize,
                   float x, float y, const std::string& fill, float glow, float boxWidth) {

            Layer& layer = scene.addContent(id, words, ContentRef::text(words, Vector2(boxWidth, fontSize * 1.6f)));
            layer.transform.position.set(x, y, 0.f);
            content.emplace(std::to_string(id), textPlan(words, font, fontSize, fill, glow));
            return layer;
        }
    };

    Showreel author(const std::string& font, int frames) {

        Showreel reel;
        const float end = static_cast<float>(frames);

        // The headline enters and stays: the reference the moving rows are read
        // against, so a viewer can tell motion from a static frame.
        Layer& title = reel.row(1, "CHRONONMOTION", font, 92.f, 0.f, -430.f, "#F4F7FF", 26.f, 1500.f);
        presets::fadeIn(title, 0, 20, kFps);
        presets::scalePop(title, 0, 24, 0.88f, 1.f, kFps);

        // spinIn — a rotation that unwinds onto the resting pose. Quaternion
        // keys, so a half turn is a half turn and not the long way round.
        Layer& spin = reel.row(2, "spinIn  \u2014  apre in rotazione su Y", font, 58.f, 0.f, -300.f,
                               "#9BE7FF", 0.f, 1600.f);
        presets::spinIn(spin, presets::Axis::Y, 8, 46, 0.5f, kFps);
        presets::fadeIn(spin, 4, 24, kFps);

        // bounceIn — arrives from an offset and rebounds onto the pose.
        Layer& bounce = reel.row(3, "bounceIn  \u2014  atterra con rimbalzo", font, 58.f, 0.f, -190.f,
                                 "#FFD79B", 0.f, 1600.f);
        presets::bounceIn(bounce, presets::Axis::Y, 8, 52, 190.f, kFps);
        presets::fadeIn(bounce, 4, 20, kFps);

        // springSettle — the smooth counterpart: one soft overshoot.
        Layer& spring = reel.row(4, "springSettle  \u2014  overshoot elastico", font, 58.f, 0.f, -80.f,
                                 "#C6FFB3", 0.f, 1600.f);
        presets::springSettle(spring, presets::Axis::Y, 8, 60, 150.f, kFps);
        presets::fadeIn(spring, 4, 20, kFps);

        // pulseOpacity — a keyed pulse train, not an effect parameter.
        Layer& pulse = reel.row(5, "pulseOpacity  \u2014  impulso nelle chiavi", font, 58.f, 0.f, 30.f,
                                "#FFB3E4", 18.f, 1600.f);
        presets::pulseOpacity(pulse, 10, 120, 5, kFps, 0.12f);
        presets::fadeIn(pulse, 4, 20, kFps);

        // window + smooth — the channel moves only inside its own work area, and
        // the pass across it is averaged so the corner cannot snap.
        Layer& windowed = reel.row(6, "window + smooth  \u2014  area di lavoro e media", font, 58.f, 0.f,
                                   140.f, "#D7C6FF", 0.f, 1600.f);
        presets::fadeSlide(windowed, presets::Direction::Left, 10, 40, 260.f, kFps);
        windowed.tracks.position.window(frameToTime(10, kFps), frameToTime(125, kFps));
        windowed.tracks.position.smooth(3.f / kFps, 5);

        // followThrough — the follower is where the driver was half a second ago,
        // borrowed through a channel link instead of baked into keys.
        Layer& driver = reel.row(7, "followThrough  \u2014  driver", font, 48.f, -380.f, 250.f,
                                 "#FFF6A8", 0.f, 900.f);
        driver.tracks.position.add(frameToTime(20, kFps), Vector3(-380.f, 250.f, 0.f), Easing::linear());
        driver.tracks.position.add(frameToTime(90, kFps), Vector3(380.f, 250.f, 0.f), Easing::easeInOut());
        driver.tracks.position.add(frameToTime(120, kFps), Vector3(-380.f, 250.f, 0.f), Easing::easeInOut());

        Layer& follower = reel.row(8, "follower (lag 0.5s)", font, 48.f, -380.f, 320.f, "#A8FFE1", 0.f, 900.f);
        presets::followThrough(follower, driver, 0.5f);

        // rippleWave — one wave sampled across three lanes, so half a cycle apart
        // moves in opposite directions at the same instant.
        Layer& waveA = reel.row(9, "ril", font, 56.f, -330.f, 400.f, "#8FD3FF", 0.f, 420.f);
        Layer& waveB = reel.row(10, "ple", font, 56.f, -110.f, 400.f, "#8FD3FF", 0.f, 420.f);
        Layer& waveC = reel.row(11, "Wave", font, 56.f, 110.f, 400.f, "#8FD3FF", 0.f, 420.f);
        const std::vector<Layer*> wave{&waveA, &waveB, &waveC};
        presets::rippleWave(wave, presets::Direction::Up, 0, static_cast<int>(end), 2.f, 34.f, kFps);

        return reel;
    }

}// namespace

int main(int argc, char** argv) {

    const std::string outputPath = argc > 1 ? argv[1] : "showreel.plan.json";
    const std::string font = argc > 2 ? argv[2] : "assets/fonts/Inter-Bold.ttf";
    const int frames = argc > 3 ? std::atoi(argv[3]) : kFrames;
    if (frames <= 1) {
        std::cerr << "plan_showreel: frames must be greater than one\n";
        return 2;
    }

    try {
        Showreel reel = author(font, frames);

        RenderPlanLoweringOptions options;
        options.width = kWidth;
        options.height = kHeight;
        options.schema = chronon3d::render_plan::kRenderPlanSchemaV3;
        options.jobId = "chrononmotion_showreel";
        options.outputPath = outputPath;
        options.contentLayers = reel.content;
        options.requireResolvedLayerSamples = true;

        chronon3d::render_plan::RenderPlan plan = lowerToRenderPlan(reel.scene, options);
        // The lowering is format-agnostic and defaults to a still; the file we
        // are about to name is a clip, so say so.
        plan.output.format = chronon3d::render_plan::OutputFormat::Mp4;

        const std::string problem = renderPlanEmissionProblem(plan);
        if (!problem.empty()) {
            std::cerr << "plan_showreel: the plan is not expressible: " << problem << "\n";
            return 1;
        }

        const std::string json = renderPlanToJson(plan);

        std::ofstream file(outputPath, std::ios::binary | std::ios::trunc);
        if (!file) {
            std::cerr << "plan_showreel: cannot write " << outputPath << "\n";
            return 1;
        }
        file << json << '\n';
        file.close();

        std::cout << "plan_showreel: " << outputPath << " — " << plan.layers.size() << " layers, "
                  << plan.canvas.duration.integral() << " frames at " << plan.canvas.fps.numerator << "/"
                  << plan.canvas.fps.denominator << " fps, " << plan.canvas.width << "x" << plan.canvas.height
                  << ", " << json.size() << " bytes\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "plan_showreel: " << error.what() << "\n";
        return 1;
    }
}
