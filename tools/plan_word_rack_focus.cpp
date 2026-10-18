// ChrononMotion — the word rack-focus proof.
//
// Recreates the reference Screenrecording animation (PROBLEMS item 3) with the
// camera vocabulary of this tree, through the same real path as the showreel:
//
//     MotionScene  ->  lowerToRenderPlan  ->  renderPlanToJson  ->  .plan.json
//
// Scene: a static background image plate, three foreground words placed at
// three different depths in 3D space (each with a yellow highlight bar under
// its baseline), and an animated camera that pans word to word while it pulls
// focus: the active word is at its own focal distance (sharp), the other words
// and the plate fall out of depth. The lens contract (aperture, fStop, focus
// distance, max blur) rides the same CameraRig channels the lowering maps to
// the RenderPlan camera tracks.
//
// Usage: chrononmotion_plan_word_rack_focus [output.plan.json] [font-asset]
//              [backdrop-asset] [frames] [render-output.mp4]

#include "chrononmotion/motion/RenderPlanEmission.hpp"
#include "chrononmotion/motion/RenderPlanLowering.hpp"

#include "chrononmotion/motion/MotionScene.hpp"
#include "chrononmotion/motion/Presets.hpp"

#include <chronon3d/render_plan/render_plan.hpp>

#include <array>
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

    /// Screen-space layout of the three words. Units of the motion scene: the
    /// camera rests on the +Z axis looking at origin; the RenderPlan legacy DoF
    /// contract requires the focal plane at world z <= 0, so every word lives
    /// in the negative-z half and its focus distance clears the camera path.
    struct WordLayout {
        const char* text;
        Vector3 position;
        float focusDistance;
    };

    const std::array<WordLayout, 3> kWords{{
        {"food", Vector3{-520.f, 60.f, -360.f}, 660.f},
        {"houses", Vector3{0.f, 10.f, -700.f}, 1000.f},
        {"see", Vector3{520.f, -60.f, -620.f}, 920.f},
    }};

    /// The look of one word: big bold white type. Chronon owns the bytes; this
    /// side only says which asset to draw.
    chronon3d::render_plan::LayerPlan textPlan(const std::string& words, const std::string& font) {
        chronon3d::render_plan::LayerPlan plan;
        plan.type = chronon3d::render_plan::LayerType::Text;
        plan.text = words;
        chronon3d::render_plan::LayerStylePlan style;
        style.font = font;
        style.font_size = 190.f;
        style.fill = "#FFFFFF";
        plan.style = style;
        return plan;
    }

    /// The reference yellow highlight bar under the word's baseline. Image
    /// content mapped to an explicitly supplied Shape LayerPlan (the certified
    /// path for structural primitives).
    chronon3d::render_plan::LayerPlan yellowBarPlan(float width, float height) {
        chronon3d::render_plan::LayerPlan plan;
        plan.type = chronon3d::render_plan::LayerType::Shape;
        chronon3d::render_plan::ShapeContentPlan shape;
        shape.type = chronon3d::render_plan::ShapeContentTypePlan::Rect;
        shape.fill_color = std::array<float, 4>{1.f, 0.83f, 0.f, 1.f};
        plan.shape = shape;
        plan.size = {width, height};
        return plan;
    }

    chronon3d::render_plan::LayerPlan backdropPlan(const std::string& asset) {
        chronon3d::render_plan::LayerPlan plan;
        plan.type = chronon3d::render_plan::LayerType::Image;
        plan.asset = asset;
        return plan;
    }

    /// Author one word + its yellow bar, both fixed to the word's depth slot.
    void addWord(MotionScene& scene, Layer::Id textId, const WordLayout& word, const std::string& font,
                 std::unordered_map<std::string, chronon3d::render_plan::LayerPlan>& content) {
        Layer& text = scene.addContent(textId, word.text,
                                       ContentRef::text(word.text, Vector2(760.f, 270.f)));
        text.transform.position = word.position;
        content.emplace(std::to_string(textId), textPlan(word.text, font));

        const std::string barId = std::to_string(textId + 1) + ":bar";
        Layer& bar = scene.addContent(textId + 1, barId,
                                      ContentRef::image(barId, Vector2(520.f, 26.f)));
        bar.transform.position = Vector3{word.position.x, word.position.y - 150.f, word.position.z};
        content.emplace(std::to_string(textId + 1), yellowBarPlan(520.f, 26.f));
    }

}// namespace

int main(int argc, char** argv) {

    const std::string outputPath = argc > 1 ? argv[1] : "word_rack_focus.plan.json";
    const std::string font = argc > 2 ? argv[2] : "assets/fonts/Poppins-Bold.ttf";
    const std::string backdrop = argc > 3 ? argv[3] : "assets/backdrop.png";
    const int frames = argc > 4 ? std::atoi(argv[4]) : 150;
    // The plan's own output contract is the rendered clip; the plan document is
    // the JSON written next to it.
    const std::string renderOutput = argc > 5 ? argv[5] : "word_rack_focus.mp4";
    if (frames <= 1) {
        std::cerr << "plan_word_rack_focus: frames must be greater than one\n";
        return 2;
    }

    try {
        MotionScene scene("word_rack_focus", kFps);
        std::unordered_map<std::string, chronon3d::render_plan::LayerPlan> content;

        // 1) Background plate: fills the canvas, parked on the canvas plane so
        //    the plate keeps its authored blur/vignette look (screen_space).
        //    The z must stay BEYOND the camera's pull-back excursion (+420): if
        //    the camera crosses the plate plane mid-shot, the renderer drops
        //    the frame content and the tail of the clip renders black. The
        //    +700 anchor keeps the plate in frustum for the whole camera path.
        Layer& bg = scene.addContent(1, "backdrop",
                                     ContentRef::image(backdrop, Vector2{static_cast<float>(kWidth), static_cast<float>(kHeight)}));
        bg.transform.position.set(0.f, 0.f, 700.f);
        content.emplace("1", backdropPlan(backdrop));

        // 2) Three words at three depths, each with its yellow baseline bar.
        addWord(scene, 2, kWords[0], font, content);
        addWord(scene, 4, kWords[1], font, content);
        addWord(scene, 6, kWords[2], font, content);

        // 3) The camera: pans right word to word, then pulls back and widens
        //    while the lens re-aims the focal plane on the active word. The
        //    rack order is word 1 -> word 2 -> word 3 -> whole band.
        CameraRig rig(100, 58.f, static_cast<float>(kWidth) / static_cast<float>(kHeight));
        rig.setAperture(0.12f).setFStop(2.2f).setMaxBlur(22.f);
        rig.setTarget(Vector3{0.f, 0.f, 0.f});

        const auto timeAt = [](int framesIn) { return static_cast<float>(framesIn) / kFps; };
        rig.positionTrack()
            .add(timeAt(0), Vector3{0.f, 0.f, 300.f})
            .add(timeAt(frames / 2), Vector3{200.f, 0.f, 300.f})
            .add(timeAt(frames - 30), Vector3{340.f, -10.f, 420.f});
        rig.fovTrack()
            .add(timeAt(0), 58.f)
            .add(timeAt(frames / 2), 62.f)
            .add(timeAt(frames - 30), 46.f)
            .add(timeAt(frames), 44.f);
        rig.focusDistanceTrack()
            .add(timeAt(0), kWords[0].focusDistance, Easing::easeInOut())
            .add(timeAt(frames / 4), kWords[0].focusDistance)
            .add(timeAt(frames / 2), kWords[1].focusDistance, Easing::easeInOut())
            .add(timeAt(frames - 30), kWords[2].focusDistance, Easing::easeInOut())
            .add(timeAt(frames), 1400.f);
        rig.fStopTrack()
            .add(timeAt(0), 2.2f)
            .add(timeAt(frames - 30), 2.2f)
            .add(timeAt(frames), 5.6f);

        RenderPlanLoweringOptions options;
        options.width = kWidth;
        options.height = kHeight;
        options.jobId = "chrononmotion_word_rack_focus";
        // Lens optics (focal length, f-stop) and the camera track bundle are
        // a V3 contract; the reference proof renders as V3.
        options.schema = chronon3d::render_plan::kRenderPlanSchemaV3;
        options.outputPath = renderOutput;
        options.contentLayers = content;
        options.cameraRig = &rig;

        chronon3d::render_plan::RenderPlan plan = lowerToRenderPlan(scene, options);
        plan.output.format = chronon3d::render_plan::OutputFormat::Mp4;
        plan.output.color_space = "srgb";

        const std::string problem = renderPlanEmissionProblem(plan);
        if (!problem.empty()) {
            std::cerr << "plan_word_rack_focus: the plan is not expressible: " << problem << "\n";
            return 1;
        }

        const std::string json = renderPlanToJson(plan);

        std::ofstream file(outputPath, std::ios::binary | std::ios::trunc);
        if (!file) {
            std::cerr << "plan_word_rack_focus: cannot write " << outputPath << "\n";
            return 1;
        }
        file << json << '\n';
        file.close();

        std::cout << "plan_word_rack_focus: " << outputPath << " — " << plan.layers.size()
                  << " layers, " << plan.canvas.duration.integral() << " frames at "
                  << plan.canvas.fps.numerator << "/" << plan.canvas.fps.denominator << " fps, "
                  << plan.canvas.width << "x" << plan.canvas.height << ", " << json.size()
                  << " bytes\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "plan_word_rack_focus: " << error.what() << "\n";
        return 1;
    }
}
