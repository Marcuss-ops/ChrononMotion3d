// ChrononMotion + Chronon3D — Definitive Documentary Showcase Generator
//
// Recreates the 4-stage documentary sequence end-to-end through the real
// MotionScene -> lowerToRenderPlan (with parented CameraRig) -> renderPlanToJson
// -> Chronon3D V3 RenderPlan pipeline:
//
//   1. Paper document plate + Serif text + Animated Cyan Highlighter (#00A8FF,
//      Multiply blend mode, Trim Paths 0% -> 100% with fast-start/soft-out) +
//      Document Null reading motion.
//   2. Foreground subject cutout + 3D Depth of Field (focus pull from document
//      at z=0 to subject at z=-320 when subject enters, darkening/defocusing
//      the background without cutout edge halo) + Floating Dust/Bokeh particles
//      in Screen blend mode.
//   3. Pop-up graphic elements (Logo + "$10,000" tag) with spring overshoot,
//      staggered by 7 frames, Drop Shadow + Glow.
//   4. Top-level Adjustment Layer with archive lens distortion, subtle RGB
//      chromatic aberration, and soft elliptical vignette.

#include "chrononmotion/motion/RenderPlanEmission.hpp"
#include "chrononmotion/motion/RenderPlanLowering.hpp"
#include "chrononmotion/motion/MotionScene.hpp"
#include "chrononmotion/motion/Presets.hpp"

#include <chronon3d/render_plan/render_plan.hpp>

#include <array>
#include <cmath>
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
constexpr int kDefaultFrames = 150;

chronon3d::render_plan::LayerPlan makePaperPlate() {
    using namespace chronon3d::render_plan;
    LayerPlan plan;
    plan.type = LayerType::Shape;
    plan.size = {1640.f, 940.f};
    plan.size_dimensions = 2;
    ShapeContentPlan shape;
    shape.type = ShapeContentTypePlan::RoundedRect;
    shape.radius = 12.f;
    // Warm archival paper tone
    shape.fill_color = std::array<float, 4>{0.95f, 0.93f, 0.88f, 1.f};
    StrokeStyle border;
    border.solid_color = std::array<float, 4>{0.82f, 0.79f, 0.72f, 1.f};
    border.width = 3.f;
    shape.stroke = border;
    plan.shape = shape;

    // Subtle paper grain (Noise 2.5%) + slight softness (GaussianBlur 0.8px)
    LayerPlan::EffectPlan blur;
    blur.kind = LayerPlan::EffectKindPlan::GaussianBlur;
    blur.radius = 0.8f;
    plan.effects.push_back(blur);

    LayerPlan::EffectPlan grain;
    grain.kind = LayerPlan::EffectKindPlan::Noise;
    grain.amount = 0.028f;
    grain.noise_size = 1.5f;
    grain.seed = 101;
    plan.effects.push_back(grain);

    return plan;
}

chronon3d::render_plan::LayerPlan makeDocText(const std::string& text,
                                              const std::string& font,
                                              float fontSize,
                                              const std::string& hexColor = "#18181B") {
    using namespace chronon3d::render_plan;
    LayerPlan plan;
    plan.type = LayerType::Text;
    plan.text = text;
    LayerStylePlan style;
    style.font = font;
    style.font_size = fontSize;
    style.fill = hexColor;
    plan.style = style;

    // Slight print softness (0.6px) to remove razor-sharp vector look
    LayerPlan::EffectPlan blur;
    blur.kind = LayerPlan::EffectKindPlan::GaussianBlur;
    blur.radius = 0.6f;
    plan.effects.push_back(blur);
    return plan;
}

chronon3d::render_plan::LayerPlan makeCyanHighlighter(float width,
                                                      float height,
                                                      int startFrame,
                                                      int durationFrames) {
    using namespace chronon3d::render_plan;
    LayerPlan plan;
    plan.type = LayerType::Shape;
    plan.size = {width, height};
    plan.size_dimensions = 2;
    // Multiply blend mode so black print text underneath pops through the cyan bar
    plan.blend_mode = chronon3d::BlendMode::Multiply;

    ShapeContentPlan shape;
    shape.type = ShapeContentTypePlan::Path;
    // Horizontal stroke path centered in the shape box
    shape.path = {
        {ShapePathCommandTypePlan::MoveTo, {-width * 0.5f, 0.f}, {0.f, 0.f}, {0.f, 0.f}},
        {ShapePathCommandTypePlan::LineTo, { width * 0.5f, 0.f}, {0.f, 0.f}, {0.f, 0.f}},
    };
    StrokeStyle stroke;
    // #00A8FF bright cyan
    stroke.solid_color = std::array<float, 4>{0.0f, 0.659f, 1.0f, 0.88f};
    stroke.width = height;
    stroke.cap = "butt";
    shape.stroke = stroke;

    // Trim Paths operator animated from 0% -> 100% with out_expo (fast start, soft deceleration)
    PathOperatorPlan trim;
    trim.kind = PathOperatorKindPlan::Trim;
    trim.trim_start = 0.f;
    trim.trim_end = 1.f;
    AnimationTrackPlan anim;
    anim.property = "trim";
    anim.easing = "out_expo";
    AnimationKeyframePlan kf0;
    kf0.frame = chronon3d::Frame{0};
    kf0.value = {0.f, 0.f, 0.f};
    AnimationKeyframePlan kfStart;
    kfStart.frame = chronon3d::Frame{startFrame};
    kfStart.value = {0.f, 0.f, 0.f};
    AnimationKeyframePlan kfEnd;
    kfEnd.frame = chronon3d::Frame{startFrame + durationFrames};
    kfEnd.value = {0.f, 1.f, 0.f};
    anim.keyframes = {kf0, kfStart, kfEnd};
    trim.trim_animation = anim;
    shape.operators.push_back(trim);

    plan.shape = shape;
    return plan;
}

chronon3d::render_plan::LayerPlan makeDustParticles() {
    using namespace chronon3d::render_plan;
    LayerPlan plan;
    plan.type = LayerType::Particles;
    plan.blend_mode = chronon3d::BlendMode::Screen;

    chronon3d::ParticleEmitterParams emitter;
    emitter.count = 55;
    emitter.seed = 2017;
    emitter.emitter_size = {1920.f, 1080.f};
    emitter.velocity_min = {-14.f, -22.f};
    emitter.velocity_max = {18.f, -6.f};
    emitter.lifetime_min = 2.5f;
    emitter.lifetime_max = 5.0f;
    emitter.size_min = 3.0f;
    emitter.size_max = 11.0f;
    emitter.turbulence = 8.0f;
    emitter.blend_mode = chronon3d::BlendMode::Screen;
    emitter.color_ramp = chronon3d::graphics::GradientDefinition::linear(
        {0.f, 0.f}, {1.f, 0.f},
        {{0.f, chronon3d::Color{0.85f, 0.94f, 1.0f, 0.0f}},
         {0.5f, chronon3d::Color{0.85f, 0.94f, 1.0f, 0.50f}},
         {1.f, chronon3d::Color{1.0f, 0.96f, 0.88f, 0.0f}}});
    plan.particle_emitter = emitter;
    return plan;
}

chronon3d::render_plan::LayerPlan makeSubjectCutout(const std::string& asset) {
    using namespace chronon3d::render_plan;
    LayerPlan plan;
    plan.type = LayerType::Image;
    plan.asset = asset;
    plan.fit = FitMode::Contain;
    plan.size = {844.f, 1125.f};
    plan.size_dimensions = 2;

    // Subtle separation shadow behind subject cutout
    LayerPlan::EffectPlan shadow;
    shadow.kind = LayerPlan::EffectKindPlan::DropShadow;
    shadow.radius = 28.f;
    shadow.offset = {0.f, 14.f};
    shadow.effect_color = {0.f, 0.f, 0.f, 0.55f};
    plan.effects.push_back(shadow);
    return plan;
}

chronon3d::render_plan::LayerPlan makeLogoBadge() {
    using namespace chronon3d::render_plan;
    LayerPlan plan;
    plan.type = LayerType::Shape;
    plan.size = {210.f, 210.f};
    plan.size_dimensions = 2;

    ShapeContentPlan shape;
    shape.type = ShapeContentTypePlan::RoundedRect;
    shape.radius = 44.f;
    shape.fill_color = std::array<float, 4>{0.04f, 0.68f, 0.98f, 1.f};
    StrokeStyle stroke;
    stroke.solid_color = std::array<float, 4>{1.f, 1.f, 1.f, 1.f};
    stroke.width = 6.f;
    shape.stroke = stroke;
    plan.shape = shape;

    // Drop Shadow + wide soft Glow on the logo badge
    LayerPlan::EffectPlan shadow;
    shadow.kind = LayerPlan::EffectKindPlan::DropShadow;
    shadow.radius = 22.f;
    shadow.offset = {0.f, 10.f};
    shadow.effect_color = {0.f, 0.f, 0.f, 0.65f};
    plan.effects.push_back(shadow);

    LayerPlan::EffectPlan glow;
    glow.kind = LayerPlan::EffectKindPlan::Glow;
    glow.radius = 36.f;
    glow.glow_intensity = 0.65f;
    glow.effect_color = {0.0f, 0.72f, 1.0f, 1.0f};
    plan.effects.push_back(glow);

    return plan;
}

chronon3d::render_plan::LayerPlan makeTagCard() {
    using namespace chronon3d::render_plan;
    LayerPlan plan;
    plan.type = LayerType::Shape;
    plan.size = {420.f, 118.f};
    plan.size_dimensions = 2;

    ShapeContentPlan shape;
    shape.type = ShapeContentTypePlan::RoundedRect;
    shape.radius = 22.f;
    shape.fill_color = std::array<float, 4>{0.08f, 0.78f, 0.38f, 1.f};
    StrokeStyle stroke;
    stroke.solid_color = std::array<float, 4>{0.96f, 1.f, 0.96f, 1.f};
    stroke.width = 4.f;
    shape.stroke = stroke;
    plan.shape = shape;

    LayerPlan::EffectPlan shadow;
    shadow.kind = LayerPlan::EffectKindPlan::DropShadow;
    shadow.radius = 20.f;
    shadow.offset = {0.f, 8.f};
    shadow.effect_color = {0.f, 0.f, 0.f, 0.60f};
    plan.effects.push_back(shadow);

    LayerPlan::EffectPlan glow;
    glow.kind = LayerPlan::EffectKindPlan::Glow;
    glow.radius = 28.f;
    glow.glow_intensity = 0.45f;
    glow.effect_color = {0.15f, 0.95f, 0.48f, 1.0f};
    plan.effects.push_back(glow);

    return plan;
}

chronon3d::render_plan::LayerPlan makePopupText(const std::string& text,
                                                const std::string& font,
                                                float fontSize) {
    using namespace chronon3d::render_plan;
    LayerPlan plan;
    plan.type = LayerType::Text;
    plan.text = text;
    LayerStylePlan style;
    style.font = font;
    style.font_size = fontSize;
    style.fill = "#FFFFFF";
    GlowStyle glow;
    glow.radius = 18.f;
    glow.intensity = 0.40f;
    glow.color = "#FFFFFF";
    style.glow = glow;
    plan.style = style;
    return plan;
}

chronon3d::render_plan::LayerPlan makeCinematicGradeAdjustment() {
    using namespace chronon3d::render_plan;
    LayerPlan plan;
    plan.type = LayerType::Adjustment;

    // 1. Archive barrel lens distortion (Optics Compensation equivalent)
    LayerPlan::EffectPlan lens;
    lens.kind = LayerPlan::EffectKindPlan::LensDistortion;
    lens.lens_curvature = -0.06f;
    lens.lens_scale = 1.02f;
    lens.lens_center = {0.5f, 0.5f};
    plan.effects.push_back(lens);

    // 2. Subtle RGB chromatic aberration
    LayerPlan::EffectPlan ca;
    ca.kind = LayerPlan::EffectKindPlan::ChromaticAberration;
    ca.chromatic_red_offset = 1.6f;
    ca.chromatic_blue_offset = -1.6f;
    plan.effects.push_back(ca);

    // 3. Soft elliptical vignette
    LayerPlan::EffectPlan vignette;
    vignette.kind = LayerPlan::EffectKindPlan::Vignette;
    vignette.amount = 0.42f;
    vignette.radius = 0.78f;
    vignette.softness = 0.45f;
    vignette.effect_color = {0.02f, 0.02f, 0.04f, 1.0f};
    plan.effects.push_back(vignette);

    return plan;
}

} // namespace

int main(int argc, char** argv) {
    const std::string outputPath = argc > 1 ? argv[1] : "documentary_showcase.plan.json";
    const std::string serifFont = argc > 2 ? argv[2] : "assets/fonts/FreeSerif.ttf";
    const std::string boldFont = argc > 3 ? argv[3] : "assets/fonts/Inter-Bold.ttf";
    const std::string cutoutAsset = argc > 4 ? argv[4] : "output/T16_Hero_Title/subject_cutout_refined.png";
    const int frames = argc > 5 ? std::atoi(argv[5]) : kDefaultFrames;
    const std::string renderOutput = argc > 6 ? argv[6] : "documentary_showcase.mp4";

    try {
        MotionScene scene("documentary_showcase", kFps);
        std::unordered_map<std::string, chronon3d::render_plan::LayerPlan> content;

        // ====================================================================
        // STEP 1: Paper Document + Serif Print Text + Animated Cyan Highlighter
        //         parented under a Document Null Controller
        // ====================================================================
        Layer& docNull = scene.addNull(10, "DocumentReadingNull");
        docNull.is3D = true;
        docNull.transform.position.set(0.f, 0.f, 0.f);
        // Slow continuous reading scale (100% -> 105%) and slight upward drift
        docNull.tracks.scale
            .add(0.f, Vector3{1.0f, 1.0f, 1.0f})
            .add(frames / kFps, Vector3{1.05f, 1.05f, 1.0f});
        docNull.tracks.position
            .add(0.f, Vector3{0.f, 0.f, 0.f})
            .add(frames / kFps, Vector3{-18.f, -24.f, 0.f});

        // Paper background plate (also dims slightly when subject cutout enters at frame 45)
        Layer& paper = scene.addContent(11, "paper_plate",
                                        ContentRef::image("paper_plate", Vector2{1640.f, 940.f}), 10);
        paper.is3D = true;
        paper.transform.position.set(0.f, 0.f, 0.f);
        paper.tracks.opacity
            .add(0.f, 1.0f)
            .add(42.f / kFps, 1.0f)
            .add(56.f / kFps, 0.52f)
            .add(frames / kFps, 0.52f);
        content.emplace("11", makePaperPlate());

        // Document Header metadata
        Layer& headerText = scene.addContent(12, "ARCHIVE DOSSIER // CASE #2017-MB",
                                             ContentRef::text("ARCHIVE DOSSIER // CASE #2017-MB", Vector2{1200.f, 60.f}), 10);
        headerText.is3D = true;
        headerText.transform.position.set(-180.f, 310.f, 1.f);
        content.emplace("12", makeDocText("ARCHIVE DOSSIER // CASE #2017-MB", boldFont, 28.f, "#52525B"));

        // Three lines of Serif document text
        struct DocLine {
            Layer::Id textId;
            Layer::Id barId;
            const char* text;
            float x;
            float y;
            float barWidth;
            int barStartFrame;
        };
        const std::array<DocLine, 3> kLines{{
            {13, 14, "in May 2017,",              -260.f, -130.f, 540.f,  6},
            {15, 16, "MrBeast would hit 1 million", -60.f,   -10.f, 980.f, 20},
            {17, 18, "subscribers.",              -310.f,  110.f, 460.f, 34},
        }};

        for (const auto& line : kLines) {
            // Print text layer first, then Cyan Highlighter layer above it with Multiply blend mode
            Layer& txt = scene.addContent(line.textId, line.text,
                                          ContentRef::text(line.text, Vector2{1100.f, 110.f}), 10);
            txt.is3D = true;
            txt.transform.position.set(line.x, -line.y, 2.f);
            content.emplace(std::to_string(line.textId), makeDocText(line.text, serifFont, 68.f));

            const std::string barName = std::string("cyan_hi_") + std::to_string(line.barId);
            Layer& bar = scene.addContent(line.barId, barName,
                                          ContentRef::image(barName, Vector2{line.barWidth, 78.f}), 10);
            bar.is3D = true;
            bar.transform.position.set(line.x, -(line.y + 6.f), 3.f);
            content.emplace(std::to_string(line.barId),
                            makeCyanHighlighter(line.barWidth, 78.f, line.barStartFrame, 13));
        }

        // ====================================================================
        // STEP 2: Atmospheric Dust Overlay (Screen, 50% opacity) + Subject Cutout
        //         in foreground (z = +320 in Motion space -> z = -320 in RenderPlan)
        // ====================================================================
        Layer& dust = scene.addContent(30, "dust_particles",
                                       ContentRef::image("dust_particles", Vector2{1920.f, 1080.f}));
        dust.is3D = true;
        dust.transform.position.set(0.f, 0.f, 140.f);
        dust.transform.opacity = 0.55f;
        content.emplace("30", makeDustParticles());

        Layer& subject = scene.addContent(40, cutoutAsset,
                                          ContentRef::image(cutoutAsset, Vector2{844.f, 1125.f}));
        subject.is3D = true;
        subject.transform.position.set(330.f, 60.f, 120.f);
        presets::fadeIn(subject, 44, 12, kFps);
        presets::slideIn(subject, presets::Direction::Up, 44, 16, 110.f, kFps, Easing::easeOut());
        content.emplace("40", makeSubjectCutout(cutoutAsset));

        // ====================================================================
        // STEP 3: Pop-up Graphics (Logo Badge + "$10,000" Tag) with Overshoot
        //         staggered by 7 frames, placed on the sharp foreground plane
        // ====================================================================
        Layer& logoBadge = scene.addContent(50, "logo_badge",
                                            ContentRef::image("logo_badge", Vector2{210.f, 210.f}));
        logoBadge.is3D = true;
        logoBadge.transform.position.set(-560.f, 90.f, 120.f);
        presets::fadeIn(logoBadge, 62, 6, kFps);
        presets::scaleOvershoot(logoBadge, 62, 12, 0.01f, 1.0f, kFps);
        content.emplace("50", makeLogoBadge());

        Layer& logoLabel = scene.addContent(51, "MB",
                                            ContentRef::text("MB", Vector2{180.f, 120.f}));
        logoLabel.is3D = true;
        logoLabel.transform.position.set(-560.f, 90.f, 122.f);
        presets::fadeIn(logoLabel, 62, 6, kFps);
        presets::scaleOvershoot(logoLabel, 62, 12, 0.01f, 1.0f, kFps);
        content.emplace("51", makePopupText("MB", boldFont, 76.f));

        // Staggered by 7 frames (frame 69)
        Layer& tagCard = scene.addContent(60, "tag_10k_card",
                                          ContentRef::image("tag_10k_card", Vector2{420.f, 118.f}));
        tagCard.is3D = true;
        tagCard.transform.position.set(-560.f, -135.f, 120.f);
        presets::fadeIn(tagCard, 69, 6, kFps);
        presets::scaleOvershoot(tagCard, 69, 12, 0.01f, 1.0f, kFps);
        content.emplace("60", makeTagCard());

        Layer& tagText = scene.addContent(61, "$10,000",
                                          ContentRef::text("$10,000", Vector2{380.f, 95.f}));
        tagText.is3D = true;
        tagText.transform.position.set(-560.f, -135.f, 122.f);
        presets::fadeIn(tagText, 69, 6, kFps);
        presets::scaleOvershoot(tagText, 69, 12, 0.01f, 1.0f, kFps);
        content.emplace("61", makePopupText("$10,000", boldFont, 64.f));

        // ====================================================================
        // STEP 4: Top-Level Adjustment Layer (Optics Distortion + Chromatic
        //         Aberration + Soft Elliptical Vignette)
        // ====================================================================
        Layer& grade = scene.addContent(90, "cinematic_grade",
                                        ContentRef::image("cinematic_grade", Vector2{1920.f, 1080.f}));
        grade.transform.position.set(0.f, 0.f, 0.f);
        content.emplace("90", makeCinematicGradeAdjustment());

        // ====================================================================
        // CAMERA RIG: Parented to a 3D Null Controller (exercises Step 1 fix!)
        //             with Rack Focus & DoF (exercises Step 2 & Step 3 fixes!)
        // ====================================================================
        Layer& camController = scene.addNull(99, "CameraDollyController");
        camController.is3D = true;
        camController.transform.position.set(0.f, 0.f, 0.f);
        // Subtle dolly push & pan from the parent controller
        camController.tracks.position
            .add(0.f, Vector3{0.f, 0.f, 0.f}, Easing::easeInOut())
            .add(44.f / kFps, Vector3{-15.f, -8.f, -45.f}, Easing::easeInOut())
            .add(frames / kFps, Vector3{22.f, 6.f, -90.f});
        camController.tracks.rotation
            .add(0.f, Vector3{0.f, 0.f, 0.f}, Easing::easeInOut())
            .add(60.f / kFps, Vector3{0.f, 0.f, -0.012f}, Easing::easeInOut())
            .add(frames / kFps, Vector3{0.f, 0.f, 0.008f});

        CameraRig rig(100, 50.f, static_cast<float>(kWidth) / static_cast<float>(kHeight), 1.f, 10000.f);
        rig.layer().parentId = 99; // Parented to CameraDollyController!
        rig.setSensorWidth(36.f).setFocalLength(20.f).setMaxBlur(28.f);

        // Local camera position rests at z = +1000 (looking down -Z in Motion space)
        rig.positionTrack()
            .add(0.f, Vector3{0.f, 0.f, 1000.f}, Easing::easeInOut())
            .add(frames / kFps, Vector3{0.f, 0.f, 980.f});

        // Focus distance & f-stop:
        // Frames 0..42: focused on the document plane (~1000 units away), f/5.6 (sharp document)
        // Frames 44..56: rack focus to the foreground cutout & pop-up badges (~625 units away)
        //                and open aperture to f/1.8 so the background document blurs heavily (28px)
        //                while the foreground cutout & badges stay razor-sharp without halo!
        rig.focusDistanceTrack()
            .add(0.f, 1000.f)
            .add(42.f / kFps, 985.f, Easing::easeInOut())
            .add(56.f / kFps, 870.f)
            .add(frames / kFps, 850.f);
        rig.fStopTrack()
            .add(0.f, 5.6f)
            .add(42.f / kFps, 5.6f, Easing::easeInOut())
            .add(56.f / kFps, 1.4f)
            .add(frames / kFps, 1.4f);

        RenderPlanLoweringOptions options;
        options.width = kWidth;
        options.height = kHeight;
        options.jobId = "chronon_documentary_showcase";
        options.schema = chronon3d::render_plan::kRenderPlanSchemaV3;
        options.outputPath = renderOutput;
        options.contentLayers = content;
        options.cameraRig = &rig;
        options.requireResolvedLayerSamples = true;

        chronon3d::render_plan::RenderPlan plan = lowerToRenderPlan(scene, options);
        plan.output.format = chronon3d::render_plan::OutputFormat::Mp4;
        plan.output.color_space = "srgb";

        const std::string problem = renderPlanEmissionProblem(plan);
        if (!problem.empty()) {
            std::cerr << "plan_documentary_showcase: emission validation failed: " << problem << "\n";
            return 1;
        }

        const std::string json = renderPlanToJson(plan);
        std::ofstream file(outputPath, std::ios::binary | std::ios::trunc);
        if (!file) {
            std::cerr << "plan_documentary_showcase: cannot write " << outputPath << "\n";
            return 1;
        }
        file << json << '\n';
        file.close();

        std::cout << "plan_documentary_showcase: wrote " << outputPath
                  << " (" << plan.layers.size() << " layers, "
                  << plan.canvas.duration.integral() << " frames @ "
                  << plan.canvas.fps.numerator << "/" << plan.canvas.fps.denominator << " fps, "
                  << plan.canvas.width << "x" << plan.canvas.height << ", "
                  << json.size() << " bytes)\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "plan_documentary_showcase error: " << e.what() << "\n";
        return 1;
    }
}
