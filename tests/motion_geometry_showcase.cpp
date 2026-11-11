// ChrononMotion Procedural 3D Gyroscope & Primitives Showcase Generator
//
// Engineering & Cinematic Redesign:
// 1. Solid Studio Backdrop & Pedestal Contrast:
//    - Real studio cyclorama background plate (soft neutral illuminated wall to floor).
//    - Gunmetal and brushed titanium stand with high contrast.
// 2. Mathematically Exact Rigid Gimbal Rig (5 Multi-Geometry Assemblies):
//    - Stand Assembly (Static): Base, chamfer, column, fork, bearing cups merged into 1 mesh.
//    - Outer Gimbal Assembly: Torus ring + 2 pivot pins merged into 1 rigid mesh (X-precession).
//    - Mid Gimbal Assembly: Torus ring + 2 pivot pins merged into 1 rigid mesh (Y-nutation).
//    - Inner Gimbal Assembly: Torus ring + 2 bearing bosses + 2 hinge pins merged into 1 rigid mesh (Z-drift).
//    - Rotor Assembly: Inertial flywheel + central shaft + sapphire core + 4 cobalt studs merged into 1 rigid mesh (360 RPM spin).
//    - Rigid geometry merging eliminates detached floating parts, eliminates Z-sorting glitches, and reduces layers from 28 to 5!
// 3. True Physical Spin vs Precession Dynamics:
//    - High-speed 360 RPM flywheel spin (24 full revolutions in 4s) with 4 high-contrast perimeter studs.
//    - Majestic slow 15 RPM cardan precession and gentle nutation.
// 4. Rock-Solid Studio Camera:
//    - Fixed optimal 3/4 isometric-perspective framing: zero gimbal lock, zero black frames,
//      continuous rendering from frame 0 to frame 240.

#include "chrononmotion/chrononmotion.hpp"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

using namespace chrononmotion;
using json = nlohmann::json;

namespace {

    std::shared_ptr<BufferGeometry> mergeGeometries(
            const std::vector<std::pair<std::shared_ptr<BufferGeometry>, Matrix4>>& parts) {
        auto merged = BufferGeometry::create();
        std::vector<float> allPositions;
        std::vector<float> allNormals;
        std::vector<float> allUvs;
        std::vector<unsigned int> allIndices;

        unsigned int vertexOffset = 0;

        for (const auto& [geom, mat] : parts) {
            if (!geom) continue;
            const auto* posAttr = geom->getAttribute<float>("position");
            const auto* normAttr = geom->getAttribute<float>("normal");
            const auto* uvAttr = geom->getAttribute<float>("uv");
            const auto* idxAttr = geom->getIndex();
            if (!posAttr) continue;

            Matrix3 normalMatrix;
            normalMatrix.getNormalMatrix(mat);

            const size_t vCount = posAttr->count();
            for (size_t i = 0; i < vCount; ++i) {
                Vector3 p(posAttr->getX(i), posAttr->getY(i), posAttr->getZ(i));
                p.applyMatrix4(mat);
                allPositions.push_back(p.x);
                allPositions.push_back(p.y);
                allPositions.push_back(p.z);

                Vector3 n(0.f, 1.f, 0.f);
                if (normAttr && i < normAttr->count()) {
                    n.set(normAttr->getX(i), normAttr->getY(i), normAttr->getZ(i));
                    n.applyMatrix3(normalMatrix).normalize();
                }
                allNormals.push_back(n.x);
                allNormals.push_back(n.y);
                allNormals.push_back(n.z);

                Vector2 uv(0.5f, 0.5f);
                if (uvAttr && i < uvAttr->count()) {
                    uv.set(uvAttr->getX(i), uvAttr->getY(i));
                }
                allUvs.push_back(uv.x);
                allUvs.push_back(uv.y);
            }

            if (idxAttr) {
                for (size_t j = 0; j < idxAttr->count(); ++j) {
                    allIndices.push_back(vertexOffset + static_cast<unsigned int>(idxAttr->getX(j)));
                }
            } else {
                for (size_t j = 0; j < vCount; ++j) {
                    allIndices.push_back(vertexOffset + static_cast<unsigned int>(j));
                }
            }
            vertexOffset += static_cast<unsigned int>(vCount);
        }

        merged->setAttribute("position", FloatBufferAttribute::create(std::move(allPositions), 3));
        merged->setAttribute("normal", FloatBufferAttribute::create(std::move(allNormals), 3));
        merged->setAttribute("uv", FloatBufferAttribute::create(std::move(allUvs), 2));
        merged->setIndex(std::move(allIndices));
        return merged;
    }

    json geometryToInlineMesh(
            const std::shared_ptr<BufferGeometry>& geom,
            int frames,
            std::function<Matrix4(int)> transform = nullptr) {

        const auto* posAttr = geom->getAttribute<float>("position");
        const auto* normAttr = geom->getAttribute<float>("normal");
        const auto* uvAttr = geom->getAttribute<float>("uv");
        const auto* idxAttr = geom->getIndex();

        if (!posAttr) {
            throw std::runtime_error("geometry missing position attribute");
        }

        const size_t vertexCount = posAttr->count();
        json vertices = json::array();
        std::vector<Vector3> originalPositions;
        std::vector<Vector3> originalNormals;
        originalPositions.reserve(vertexCount);
        originalNormals.reserve(vertexCount);

        for (size_t i = 0; i < vertexCount; ++i) {
            Vector3 p(posAttr->getX(i), posAttr->getY(i), posAttr->getZ(i));
            originalPositions.push_back(p);

            Vector3 n(0.f, 1.f, 0.f);
            if (normAttr && i < normAttr->count()) {
                n.set(normAttr->getX(i), normAttr->getY(i), normAttr->getZ(i));
            }
            originalNormals.push_back(n);

            Vector2 uv(0.5f, 0.5f);
            if (uvAttr && i < uvAttr->count()) {
                uv.set(uvAttr->getX(i), uvAttr->getY(i));
            }

            vertices.push_back({
                {"position", {std::round(p.x * 1000.f) / 1000.f, std::round(p.y * 1000.f) / 1000.f, std::round(p.z * 1000.f) / 1000.f}},
                {"normal", {std::round(n.x * 1000.f) / 1000.f, std::round(n.y * 1000.f) / 1000.f, std::round(n.z * 1000.f) / 1000.f}},
                {"uv", {std::round(uv.x * 1000.f) / 1000.f, std::round(uv.y * 1000.f) / 1000.f}}
            });
        }

        json indices = json::array();
        if (idxAttr) {
            for (size_t j = 0; j < idxAttr->count(); ++j) {
                indices.push_back(static_cast<unsigned int>(idxAttr->getX(j)));
            }
        }

        json meshObj = {
            {"vertices", std::move(vertices)},
            {"indices", std::move(indices)}
        };

        if (transform) {
            json samples = json::array();
            for (int f = 0; f < frames; ++f) {
                Matrix4 m = transform(f);
                Matrix3 normalMat;
                normalMat.getNormalMatrix(m);

                json framePositions = json::array();
                json frameNormals = json::array();

                for (size_t vi = 0; vi < vertexCount; ++vi) {
                    Vector3 tp = originalPositions[vi];
                    tp.applyMatrix4(m);
                    Vector3 tn = originalNormals[vi];
                    tn.applyMatrix3(normalMat).normalize();

                    framePositions.push_back({
                        std::round(tp.x * 1000.f) / 1000.f,
                        std::round(tp.y * 1000.f) / 1000.f,
                        std::round(tp.z * 1000.f) / 1000.f
                    });
                    frameNormals.push_back({
                        std::round(tn.x * 1000.f) / 1000.f,
                        std::round(tn.y * 1000.f) / 1000.f,
                        std::round(tn.z * 1000.f) / 1000.f
                    });
                }

                samples.push_back({
                    {"frame", f},
                    {"positions", std::move(framePositions)},
                    {"normals", std::move(frameNormals)}
                });
            }
            meshObj["animation"] = std::move(samples);
        }

        return meshObj;
    }

} // namespace

int main(int argc, char** argv) {
    std::string outputPath = "ChrononMotion3D/docs/evidence/roblox_showcase_20261111/plans/chronon_gyroscope_1080p.plan.json";
    if (argc > 1) {
        outputPath = argv[1];
    }

    const int w = 1920;
    const int h = 1080;
    const int fps = 60;
    const int frames = 240; // 4.0 seconds @ 60fps

    std::printf("== Building High-Precision Gyroscope Plan (%dx%d @ %dfps, %d frames) ==\n",
                w, h, fps, frames);

    // ── Kinematic Hierarchy Evaluators ─────────────────────────────────────────
    //
    // Base Frame: Static studio mount
    // Outer Gimbal: Rotates around X axis (cardan precession, ~15 RPM = 1 rev / 4s)
    // Mid Gimbal: Child of Outer Gimbal, tilts around Y axis (nutation, ±18 deg)
    // Inner Gimbal: Child of Mid Gimbal, rotates around Z axis (precession drift, ±28 deg)
    // Rotor Spindle: Child of Inner Gimbal, spins at 360 RPM around local Y (24 full revs)

    auto evalOuterMatrix = [&](int f) -> Matrix4 {
        const float t = static_cast<float>(f) / static_cast<float>(frames);
        const float precAngle = t * (2.f * math::PI); // Exactly 1 complete 360 deg precession
        Matrix4 m;
        m.makeRotationX(precAngle);
        return m;
    };

    auto evalMidMatrix = [&](int f) -> Matrix4 {
        const float t = static_cast<float>(f) / static_cast<float>(frames);
        const float nutationAngle = std::sin(t * (4.f * math::PI)) * 0.32f; // ±18.3 deg nutation
        Matrix4 parent = evalOuterMatrix(f);
        Matrix4 local;
        local.makeRotationY(nutationAngle);
        parent.multiply(local);
        return parent;
    };

    auto evalInnerMatrix = [&](int f) -> Matrix4 {
        const float t = static_cast<float>(f) / static_cast<float>(frames);
        const float innerAngle = std::sin(t * (2.f * math::PI) + 0.5f) * 0.48f; // ±27.5 deg drift
        Matrix4 parent = evalMidMatrix(f);
        Matrix4 local;
        local.makeRotationZ(innerAngle);
        parent.multiply(local);
        return parent;
    };

    auto evalRotorMatrix = [&](int f) -> Matrix4 {
        const float t = static_cast<float>(f) / static_cast<float>(frames);
        const float spinAngle = t * (24.f * 2.f * math::PI); // 24 full revs (360 RPM)
        Matrix4 parent = evalInnerMatrix(f);
        Matrix4 local;
        local.makeRotationY(spinAngle);
        parent.multiply(local);
        return parent;
    };

    json layers = json::array();

    // ── 1. Studio Backdrop (ImageLayer: High-End Photographic Cyclorama) ────────
    layers.push_back({
        {"id", "studio_cyclorama_backdrop"},
        {"type", "image"},
        {"asset", "assets/studio_cyclorama.png"},
        {"position", {w / 2.0, h / 2.0}},
        {"size", {static_cast<double>(w), static_cast<double>(h)}},
        {"opacity", 1.0},
        {"start_frame", 0},
        {"duration_frames", frames}
    });

    // ── 2. Unified Static Stand Assembly (Gunmetal Titanium, 9 parts in 1 mesh) ──
    std::vector<std::pair<std::shared_ptr<BufferGeometry>, Matrix4>> standParts;

    // Contact shadow catcher floor disc (dark charcoal floor contact)
    Matrix4 mFloor; mFloor.makeTranslation(0.f, -238.f, 0.f);
    standParts.push_back({CylinderGeometry::create(170.f, 170.f, 3.f, 32), mFloor});

    // Base heavy disc
    Matrix4 mBase; mBase.makeTranslation(0.f, -230.f, 0.f);
    standParts.push_back({CylinderGeometry::create(95.f, 95.f, 12.f, 32), mBase});

    // Base beveled chamfer ring
    Matrix4 mChamfer; mChamfer.makeTranslation(0.f, -220.f, 0.f);
    standParts.push_back({CylinderGeometry::create(75.f, 95.f, 8.f, 32), mChamfer});

    // Pedestal column
    Matrix4 mPedestal; mPedestal.makeTranslation(0.f, -156.f, 0.f);
    standParts.push_back({CylinderGeometry::create(22.f, 22.f, 120.f, 24), mPedestal});

    // Fork lower cross-beam
    Matrix4 mForkBase; mForkBase.makeTranslation(0.f, -92.f, 0.f);
    standParts.push_back({BoxGeometry::create(56.f, 18.f, 36.f), mForkBase});

    // Fork left support arm
    Matrix4 mArmLeft; mArmLeft.makeTranslation(-106.f, -45.f, 0.f);
    standParts.push_back({BoxGeometry::create(14.f, 102.f, 26.f), mArmLeft});

    // Fork right support arm
    Matrix4 mArmRight; mArmRight.makeTranslation(106.f, -45.f, 0.f);
    standParts.push_back({BoxGeometry::create(14.f, 102.f, 26.f), mArmRight});

    // Precision bearing cups (bottom & top pivots)
    Matrix4 mCupBottom; mCupBottom.makeTranslation(0.f, -94.f, 0.f);
    standParts.push_back({CylinderGeometry::create(10.f, 10.f, 14.f, 20), mCupBottom});

    Matrix4 mCupTop; mCupTop.makeTranslation(0.f, 94.f, 0.f);
    standParts.push_back({CylinderGeometry::create(10.f, 10.f, 14.f, 20), mCupTop});

    auto standGeom = mergeGeometries(standParts);
    json standMesh = geometryToInlineMesh(standGeom, frames, nullptr); // Static: zero per-frame deformation!
    standMesh["material"] = {
        {"diffuse", 0.78}, {"specular", 0.86}, {"shininess", 65.0},
        {"base_color", {0.32, 0.35, 0.40, 1.0}} // Brushed gunmetal titanium
    };
    layers.push_back({
        {"id", "geo_stand_assembly"},
        {"type", "mesh"},
        {"size", {1.0, 1.0}},
        {"position", {0.0, 0.0, 0.0}},
        {"enable_3d", true},
        {"opacity", 1.0},
        {"casts_shadows", true},
        {"accepts_shadows", true},
        {"start_frame", 0},
        {"duration_frames", frames},
        {"color", {0.32, 0.35, 0.40, 1.0}},
        {"inline_mesh", std::move(standMesh)}
    });

    // ── 3. Outer Gimbal Assembly (Polished Brass / Gold, Ring + Pins in 1 mesh) ─
    std::vector<std::pair<std::shared_ptr<BufferGeometry>, Matrix4>> outerParts;

    // Outer Torus Ring (radius 108, tube 5.5)
    Matrix4 mId;
    outerParts.push_back({TorusGeometry::create(108.f, 5.5f, 16, 40), mId});

    // Top mounting pin
    Matrix4 mTopPin; mTopPin.makeTranslation(0.f, 108.f, 0.f);
    outerParts.push_back({CylinderGeometry::create(4.2f, 4.2f, 24.f, 16), mTopPin});

    // Bottom mounting pin
    Matrix4 mBotPin; mBotPin.makeTranslation(0.f, -108.f, 0.f);
    outerParts.push_back({CylinderGeometry::create(4.2f, 4.2f, 24.f, 16), mBotPin});

    auto outerGeom = mergeGeometries(outerParts);
    json outerMesh = geometryToInlineMesh(outerGeom, frames, evalOuterMatrix);
    outerMesh["material"] = {
        {"diffuse", 0.90}, {"specular", 0.95}, {"shininess", 95.0},
        {"base_color", {0.98, 0.80, 0.22, 1.0}} // Mirror Polished Gold / Brass
    };
    layers.push_back({
        {"id", "geo_outer_gimbal_assembly"},
        {"type", "mesh"},
        {"size", {1.0, 1.0}},
        {"position", {0.0, 0.0, 0.0}},
        {"enable_3d", true},
        {"opacity", 1.0},
        {"casts_shadows", true},
        {"accepts_shadows", true},
        {"start_frame", 0},
        {"duration_frames", frames},
        {"color", {0.98, 0.80, 0.22, 1.0}},
        {"inline_mesh", std::move(outerMesh)}
    });

    // ── 4. Mid Gimbal Assembly (Precision Chrome, Ring + Pins in 1 mesh) ────────
    std::vector<std::pair<std::shared_ptr<BufferGeometry>, Matrix4>> midParts;

    // Mid Torus Ring (radius 89, tube 5.0)
    midParts.push_back({TorusGeometry::create(89.f, 5.0f, 16, 38), mId});

    // Left pivot pin (rotated along X axis)
    Matrix4 mPinLeft;
    Matrix4 rz; rz.makeRotationZ(math::PI / 2.f);
    Matrix4 txLeft; txLeft.makeTranslation(-89.f, 0.f, 0.f);
    mPinLeft.multiply(txLeft);
    mPinLeft.multiply(rz);
    midParts.push_back({CylinderGeometry::create(3.8f, 3.8f, 22.f, 16), mPinLeft});

    // Right pivot pin
    Matrix4 mPinRight;
    Matrix4 txRight; txRight.makeTranslation(89.f, 0.f, 0.f);
    mPinRight.multiply(txRight);
    mPinRight.multiply(rz);
    midParts.push_back({CylinderGeometry::create(3.8f, 3.8f, 22.f, 16), mPinRight});

    auto midGeom = mergeGeometries(midParts);
    json midMesh = geometryToInlineMesh(midGeom, frames, evalMidMatrix);
    midMesh["material"] = {
        {"diffuse", 0.82}, {"specular", 0.94}, {"shininess", 92.0},
        {"base_color", {0.84, 0.88, 0.92, 1.0}} // Mirror Chrome / Precision Steel
    };
    layers.push_back({
        {"id", "geo_mid_gimbal_assembly"},
        {"type", "mesh"},
        {"size", {1.0, 1.0}},
        {"position", {0.0, 0.0, 0.0}},
        {"enable_3d", true},
        {"opacity", 1.0},
        {"casts_shadows", true},
        {"accepts_shadows", true},
        {"start_frame", 0},
        {"duration_frames", frames},
        {"color", {0.84, 0.88, 0.92, 1.0}},
        {"inline_mesh", std::move(midMesh)}
    });

    // ── 5. Inner Gimbal Assembly (Satin Rose Bronze, Ring + Bosses in 1 mesh) ───
    std::vector<std::pair<std::shared_ptr<BufferGeometry>, Matrix4>> innerParts;

    // Inner Torus Ring (radius 70, tube 4.8)
    innerParts.push_back({TorusGeometry::create(70.f, 4.8f, 16, 36), mId});

    // Top bearing boss
    Matrix4 mBossTop; mBossTop.makeTranslation(0.f, 70.f, 0.f);
    innerParts.push_back({CylinderGeometry::create(8.2f, 8.2f, 14.f, 18), mBossTop});

    // Bottom bearing boss
    Matrix4 mBossBottom; mBossBottom.makeTranslation(0.f, -70.f, 0.f);
    innerParts.push_back({CylinderGeometry::create(8.2f, 8.2f, 14.f, 18), mBossBottom});

    // Front hinge pin
    Matrix4 mHingeFront;
    Matrix4 rx; rx.makeRotationX(math::PI / 2.f);
    Matrix4 tzFront; tzFront.makeTranslation(0.f, 0.f, 70.f);
    mHingeFront.multiply(tzFront);
    mHingeFront.multiply(rx);
    innerParts.push_back({CylinderGeometry::create(3.2f, 3.2f, 20.f, 16), mHingeFront});

    // Back hinge pin
    Matrix4 mHingeBack;
    Matrix4 tzBack; tzBack.makeTranslation(0.f, 0.f, -70.f);
    mHingeBack.multiply(tzBack);
    mHingeBack.multiply(rx);
    innerParts.push_back({CylinderGeometry::create(3.2f, 3.2f, 20.f, 16), mHingeBack});

    auto innerGeom = mergeGeometries(innerParts);
    json innerMesh = geometryToInlineMesh(innerGeom, frames, evalInnerMatrix);
    innerMesh["material"] = {
        {"diffuse", 0.88}, {"specular", 0.90}, {"shininess", 80.0},
        {"base_color", {0.90, 0.52, 0.32, 1.0}} // Satin Rose Bronze
    };
    layers.push_back({
        {"id", "geo_inner_gimbal_assembly"},
        {"type", "mesh"},
        {"size", {1.0, 1.0}},
        {"position", {0.0, 0.0, 0.0}},
        {"enable_3d", true},
        {"opacity", 1.0},
        {"casts_shadows", true},
        {"accepts_shadows", true},
        {"start_frame", 0},
        {"duration_frames", frames},
        {"color", {0.90, 0.52, 0.32, 1.0}},
        {"inline_mesh", std::move(innerMesh)}
    });

    // ── 6. Rotor Assembly (Flywheel, Shaft, Jewel Core & Studs in 1 mesh) ──────
    std::vector<std::pair<std::shared_ptr<BufferGeometry>, Matrix4>> rotorParts;

    // Heavy inertial flywheel cylinder (radius 55, height 18)
    rotorParts.push_back({CylinderGeometry::create(55.f, 55.f, 18.f, 32), mId});

    // Central high-speed precision spindle shaft (radius 4.0, height 142)
    rotorParts.push_back({CylinderGeometry::create(4.0f, 4.0f, 142.f, 20), mId});

    // Luminescent central sapphire jewel core (Capsule radius 13, length 24)
    rotorParts.push_back({CapsuleGeometry::create(13.f, 24.f, 14, 20), mId});

    // 4 High-contrast radial inertia studs at 90 deg around circumference (radius 48)
    const float markerDist = 48.f;
    for (int k = 0; k < 4; ++k) {
        const float angle = k * (math::PI / 2.f);
        Matrix4 mStud;
        mStud.makeTranslation(std::cos(angle) * markerDist, 0.f, std::sin(angle) * markerDist);
        rotorParts.push_back({BoxGeometry::create(5.f, 8.f, 12.f), mStud});
    }

    auto rotorGeom = mergeGeometries(rotorParts);
    json rotorMesh = geometryToInlineMesh(rotorGeom, frames, evalRotorMatrix);
    rotorMesh["material"] = {
        {"diffuse", 0.92}, {"specular", 0.96}, {"shininess", 95.0},
        {"base_color", {0.86, 0.74, 0.38, 1.0}} // Beryllium Copper & Sapphire Core
    };
    layers.push_back({
        {"id", "geo_rotor_assembly"},
        {"type", "mesh"},
        {"size", {1.0, 1.0}},
        {"position", {0.0, 0.0, 0.0}},
        {"enable_3d", true},
        {"opacity", 1.0},
        {"casts_shadows", true},
        {"accepts_shadows", true},
        {"start_frame", 0},
        {"duration_frames", frames},
        {"color", {0.86, 0.74, 0.38, 1.0}},
        {"inline_mesh", std::move(rotorMesh)}
    });

    // ── 7. Fixed Studio Camera (Zero Blackout, Permanent Optical Stability) ──
    const float camDist = 530.f;
    const float camX = 145.f;
    const float camY = 70.f;
    const float camZ = -camDist;
    const float targetY = -12.f;
    const float dy = camY - targetY;
    const float pitch = std::atan2(dy, std::sqrt(camX * camX + camZ * camZ)) * (180.f / math::PI);
    const float yaw = std::atan2(camX, -camZ) * (180.f / math::PI);

    // ── 8. Studio 3-Point Lighting Rig ────────────────────────────────────────
    json lightingRig = {
        {"key_direction", {0.55, -0.75, 0.50}},
        {"key_intensity", 1.95},
        {"key_color", {1.0, 0.97, 0.92, 1.0}},
        {"ambient_intensity", 0.42},
        {"ambient_color", {0.70, 0.80, 0.95, 1.0}},
        {"rim_enabled", true},
        {"rim_intensity", 1.6},
        {"rim_power", 3.2},
        {"rim_color", {0.85, 0.94, 1.0, 1.0}}
    };

    json plan = {
        {"schema", "chronon.render-plan.v3"},
        {"version", 3},
        {"job_id", "precision_gyroscope_showcase_1080p"},
        {"camera", {
            {"type", "perspective"},
            {"position", {camX, camY, camZ}},
            {"rotation_deg", {std::round(pitch * 100.f) / 100.f, std::round(yaw * 100.f) / 100.f, 0.0}},
            {"fov_deg", 32.0},
            {"near", 1.0},
            {"far", 10000.0},
            {"zoom", 1.0}
        }},
        {"canvas", {
            {"width", w},
            {"height", h},
            {"fps_num", fps},
            {"fps_den", 1},
            {"duration_frames", frames}
        }},
        {"output", {
            {"path", "chronon_gyroscope_1080p.mp4"},
            {"format", "mp4"}
        }},
        {"lighting_rig", std::move(lightingRig)},
        {"layers", std::move(layers)}
    };

    std::ofstream out(outputPath);
    if (!out.is_open()) {
        std::fprintf(stderr, "Failed to open %s for writing\n", outputPath.c_str());
        return 1;
    }
    out << plan.dump(1);
    out.close();

    std::printf("== SUCCESS: Precision Gyroscope RenderPlan written to %s (%zu layers)\n",
                outputPath.c_str(), plan["layers"].size());
    return 0;
}
