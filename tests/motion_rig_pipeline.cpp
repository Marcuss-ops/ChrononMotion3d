#include "motion_check.hpp"
#include "chrononmotion/motion/Motion.hpp"
#include "chrononmotion/assets/GltfReader.hpp"
#include <bit>
#include <stdexcept>

using namespace chrononmotion;
using namespace chrononmotion::motion;
using namespace chrononmotion_test;
namespace {
    template<class F> void rejects(F&& f, const char* message) {
        bool rejected = false; try { f(); } catch (const std::invalid_argument&) { rejected = true; }
        check(rejected, message);
    }
    void replace(std::string& text, const std::string& from, const std::string& to) {
        const auto pos = text.find(from); if (pos == std::string::npos) throw std::runtime_error("fixture replacement missing");
        text.replace(pos, from.size(), to);
    }
    struct Fixture {
        std::vector<std::byte> bytes;
        std::string json;
        Fixture() {
            const auto scalar = [&](float v) {
                const auto bits = std::bit_cast<std::uint32_t>(v);
                for (unsigned i = 0; i < 4; ++i) bytes.push_back(static_cast<std::byte>((bits >> (8 * i)) & 255));
            };
            for (float v : {0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f, 0.f}) scalar(v); // positions 0..35
            for (unsigned i = 0; i < 12; ++i) bytes.push_back(std::byte{0}); // joints 36..47
            for (unsigned vertex = 0; vertex < 3; ++vertex) for (float v : {1.f, 0.f, 0.f, 0.f}) scalar(v); // weights 48..95
            for (unsigned i = 0; i < 16; ++i) scalar(i % 5 == 0 ? 1.f : 0.f); // inverse 96..159
            scalar(0.f); scalar(1.f); // time 160..167
            for (float v : {0.f, 0.f, 0.f, 2.f, 0.f, 0.f}) scalar(v); // translation 168..191
            for (unsigned i = 0; i < 9; ++i) scalar(i % 3 == 1 ? 1.f : 0.f); // morph 192..227
            json = R"({"asset":{"version":"2.0"},"buffers":[{"byteLength":228,"uri":"fixture.bin"}],
"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":12},
{"buffer":0,"byteOffset":48,"byteLength":48},{"buffer":0,"byteOffset":96,"byteLength":64},
{"buffer":0,"byteOffset":160,"byteLength":8},{"buffer":0,"byteOffset":168,"byteLength":24},
{"buffer":0,"byteOffset":192,"byteLength":36}],
"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"},
{"bufferView":1,"componentType":5121,"count":3,"type":"VEC4"},
{"bufferView":2,"componentType":5126,"count":3,"type":"VEC4"},
{"bufferView":3,"componentType":5126,"count":1,"type":"MAT4"},
{"bufferView":4,"componentType":5126,"count":2,"type":"SCALAR"},
{"bufferView":5,"componentType":5126,"count":2,"type":"VEC3"},
{"bufferView":6,"componentType":5126,"count":3,"type":"VEC3"}],
"nodes":[{"name":"joint"},{"name":"mesh","mesh":0,"skin":0}],
"skins":[{"joints":[0],"skeleton":0,"inverseBindMatrices":3}],
"meshes":[{"weights":[0.5],"primitives":[{"attributes":{"POSITION":0,"JOINTS_0":1,"WEIGHTS_0":2},"targets":[{"POSITION":6}]}]}],
"animations":[{"name":"move","samplers":[{"input":4,"output":5,"interpolation":"LINEAR"}],
"channels":[{"sampler":0,"target":{"node":0,"path":"translation"}}]}]})";
        }
        GltfMotionAsset load(const std::string& text) const {
            return loadGltfMotion(text, [&](std::string_view uri) { if (uri != "fixture.bin") throw std::invalid_argument("unknown fixture URI"); return bytes; });
        }
    };
}
namespace {
    RigProgram makeProgram() {
        RigProgram p;
        for (unsigned i = 0; i < 3; ++i) {
            RigNode n; n.id = i + 1; n.parent = i;
            if (i) n.rest.position = {1.f, 0.f, 0.f};
            n.bind = n.rest; n.layerId = n.id; p.graph.nodes.push_back(n);
        }
        RigNode target; target.id = 4; target.rest.position = {1.f, 1.f, 0.f}; target.bind = target.rest;
        RigNode weight; weight.id = 6; weight.kind = RigNodeKind::Controller;
        p.graph.nodes.insert(p.graph.nodes.end(), {target, weight});
        RigController control; control.id = 6; control.minimum = {}; control.maximum = {1.f, 0.f, 0.f};
        p.drivers.controllers.push_back(control);
        RigDriverNode drive; drive.id = 1; drive.kind = RigDriverKind::Track;
        drive.track.add(0.f, 0.f).add(1.f, 1.f).add(9.f, 1.f).add(10.f, 0.f);
        p.drivers.nodes.push_back(drive);
        p.drivers.outputs.push_back({1, 6, RigDrivenProperty::Position, 0});
        RigGeometryBinding rigid; rigid.id = 20; rigid.rigidNode = 3;
        rigid.source = {{0.f, 0.f, 0.f}, {.25f, .5f, 0.f}, {1.f, 0.f, 0.f}};
        RigMorphTarget morph; morph.positions = rigid.source; morph.positions[1].y = 1.f;
        rigid.morphs.push_back(morph);
        RigDeformer wave; wave.wavelength = 3.f; rigid.deformers.operators.push_back(wave);
        p.geometry.push_back(rigid);
        return p;
    }
}

int main() {
    section("TestRigSerializationStrictRoundTrip");
    RigGraph graph;
    RigNode root; root.id = 1; root.rest.position = {2.f, 3.f, 4.f}; root.bind = root.rest;
    RigNode child; child.id = 2; child.parent = 1;
    ConstraintSpec limit; limit.kind = ConstraintKind::LimitRotation; child.constraints.push_back(limit);
    graph.nodes = {child, root};
    const auto encoded = serializeRigGraph(graph);
    const auto decoded = deserializeRigGraph(encoded);
    check(serializeRigGraph(decoded) == encoded, "canonical serialization round-trips byte-for-byte");
    check(decoded.compile().evaluate().world == graph.compile().evaluate().world, "rig serialization preserves evaluation bit-for-bit");
    auto invalid = encoded; replace(invalid, "chrononmotion.rig.v1", "chrononmotion.rig.v2");
    rejects([&] { (void) deserializeRigGraph(invalid); }, "unknown rig version rejected");
    invalid = encoded; replace(invalid, "\"nodes\":[", "\"unexpected\":true,\"nodes\":[");
    rejects([&] { (void) deserializeRigGraph(invalid); }, "unknown rig field rejected");
    invalid = encoded; replace(invalid, "\"id\":1", "\"id\":1,\"id\":1");
    rejects([&] { (void) deserializeRigGraph(invalid); }, "duplicate JSON keys rejected");
    invalid = encoded; replace(invalid, "\"parent\":0", "\"parent\":2");
    rejects([&] { (void) deserializeRigGraph(invalid); }, "serialized cycle rejected");
    invalid = encoded; replace(invalid, "\"length\":1", "\"length\":1e999");
    rejects([&] { (void) deserializeRigGraph(invalid); }, "nonfinite serialized number rejected");
    rejects([&] { (void) deserializeRigGraph(encoded + "garbage"); }, "trailing JSON rejected");

    section("TestGltfMotionSkinMorphClipImport");
    Fixture f;
    const auto asset = f.load(f.json);
    check(asset.graph.nodes.size() == 2 && asset.skins.size() == 1 && asset.geometry.size() == 1 && asset.clips.size() == 1,
          "motion imports canonical graph, skin, geometry and clip");
    check(asset.skins[0].joints == std::vector<RigNodeId>{1}, "glTF node indices map to stable nonzero rig IDs");
    checkNear(asset.clips[0].duration, 1.f, 0.f, "clip duration preserved");
    RigProgram program; program.graph = asset.graph; program.animation = asset.clips[0].animation; program.geometry = asset.geometry;
    const RigProgramEvaluator evaluator(program);
    checkNear(evaluator.evaluate(15, 30.f).geometry[0].positions[0], {1.f, .5f, 0.f}, 1e-5f, "imported linear clip, inverse bind skin and morph compose");
    check(evaluator.bake(0, 30, 30.f).signature == evaluator.bake(0, 30, 30.f).signature, "imported motion repeat bake exact");
    auto step = f.json; replace(step, "LINEAR", "STEP");
    const auto stepped = f.load(step); program.animation = stepped.clips[0].animation;
    checkNear(RigProgramEvaluator(program).evaluate(15, 30.f).geometry[0].positions[0].x, 0.f, 0.f, "STEP clip holds source value");
    auto bad = f.json; replace(bad, "LINEAR", "CUBICSPLINE");
    rejects([&] { (void) f.load(bad); }, "unsupported clip interpolation rejected");
    bad = f.json; replace(bad, "\"joints\":[0]", "\"joints\":[99]");
    rejects([&] { (void) f.load(bad); }, "missing skin joint rejected");
    bad = f.json; replace(bad, "\"byteLength\":228", "\"byteLength\":227");
    rejects([&] { (void) f.load(bad); }, "truncated buffer contract rejected");
    bad = f.json; replace(bad, "\"type\":\"MAT4\"", "\"type\":\"VEC4\"");
    rejects([&] { (void) f.load(bad); }, "invalid inverse bind shape rejected");
    bad = f.json; replace(bad, "\"weights\":[0.5]", "\"weights\":[2]");
    rejects([&] { (void) f.load(bad); }, "invalid morph weight rejected");
    auto malformed = f; malformed.bytes[36] = std::byte{9};
    rejects([&] { (void) malformed.load(malformed.json); }, "vertex joint index outside skin rejected");
    malformed = f;
    for (unsigned i = 48; i < 52; ++i) malformed.bytes[i] = std::byte{0};
    rejects([&] { (void) malformed.load(malformed.json); }, "unnormalized skin weight rejected");
    rejects([&] { (void) loadGltfMotion(f.json); }, "external buffers never implicitly read");
    section("TestExplicitInverseBind / AnimatedMorphImport");
    auto alternateBind = f;
    const auto bits = std::bit_cast<std::uint32_t>(-1.f);
    for (unsigned i = 0; i < 4; ++i) alternateBind.bytes[96 + 12 * 4 + i] = static_cast<std::byte>((bits >> (8 * i)) & 255);
    const auto alternate = alternateBind.load(alternateBind.json);
    program.graph = alternate.graph; program.geometry = alternate.geometry; program.animation.clear();
    checkNear(RigProgramEvaluator(program).evaluate(0, 30.f).geometry[0].positions[0], {-1.f, .5f, 0.f}, 1e-5f,
              "explicit inverse bind honored even when distinct from authored rest");
    auto morphJson = f.json;
    replace(morphJson, "\"output\":5", "\"output\":4");
    replace(morphJson, "\"node\":0,\"path\":\"translation\"", "\"node\":1,\"path\":\"weights\"");
    const auto morphAsset = f.load(morphJson);
    check(morphAsset.clips[0].morphs.size() == 1, "morph weight channels imported");
    program.graph = morphAsset.graph; program.geometry = morphAsset.geometry; program.animation = morphAsset.clips[0].animation;
    for (const auto& channel : morphAsset.clips[0].morphs) program.geometry[channel.geometryId - 1].morphTracks = channel.weights;
    const RigProgramEvaluator morphEvaluator(program);
    checkNear(morphEvaluator.evaluate(0, 30.f).geometry[0].positions[0].y, 0.f, 0.f, "morph clip weight zero retains base");
    checkNear(morphEvaluator.evaluate(15, 30.f).geometry[0].positions[0].y, .5f, 1e-5f, "morph clip midpoint");
    checkNear(morphEvaluator.evaluate(30, 30.f).geometry[0].positions[0].y, 1.f, 0.f, "morph clip endpoint");

    section("TestRigProgramSerializationStrictRoundTrip");
    auto serializedProgram = makeProgram();
    (void) serializedProgram;
    const auto encodedProgram = serializeRigProgram(serializedProgram);
    const auto decodedProgram = deserializeRigProgram(encodedProgram);
    check(serializeRigProgram(decodedProgram) == encodedProgram, "program serialization round-trips byte-for-byte");
    const RigProgramEvaluator original(serializedProgram), restored(decodedProgram);
    for (const int frame : {0, 97, 300})
        check(rigFrameSignature(original.evaluate(frame, 30.f)) == rigFrameSignature(restored.evaluate(frame, 30.f)),
              "serialized program evaluates identically");
    auto unknown = encodedProgram; replace(unknown, "chrononmotion.rig.program.v1", "chrononmotion.rig.program.v2");
    rejects([&] { (void) deserializeRigProgram(unknown); }, "unknown program schema rejected");
    unknown = encodedProgram; replace(unknown, "\"poses\":", "\"unexpected\":true,\"poses\":");
    rejects([&] { (void) deserializeRigProgram(unknown); }, "unknown program field rejected");
    auto scheduled = makeProgram();
    RigScheduledIK ik; ik.id = 10; ik.chain.joints = {1, 2, 3}; ik.chain.target = {1.f, 1.f, 0.f};
    scheduled.ik.push_back(ik);
    rejects([&] { (void) serializeRigProgram(scheduled); }, "IK schedule outside program v1 serialization rejected");
    auto withPath = makeProgram();
    withPath.geometry[0].deformers.operators[0].path = std::make_shared<MotionPath>(MotionPath::line({}, {1.f, 0.f, 0.f}));
    rejects([&] { (void) serializeRigProgram(withPath); }, "MotionPath handles rejected in serialization");
    rejects([&] { (void) deserializeRigProgram(encodedProgram + "garbage"); }, "trailing program JSON rejected");
    return report();
}
