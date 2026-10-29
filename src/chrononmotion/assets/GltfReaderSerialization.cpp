#include "chrononmotion/assets/GltfReader.hpp"
#include "../motion/JsonDom.hpp"
#include "chrononmotion/motion/RigGraph.hpp"

#include <algorithm>
#include <iomanip>
#include <limits>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace chrononmotion {
namespace {
using namespace chrononmotion::motion::detail;
[[noreturn]] void invalid(const std::string& message) {
    throw std::invalid_argument("glTF: " + message);
}
}

namespace motion {
namespace {
    Vector3 rigVector(const JsonValue& value) {
        const auto& a = value.array("rig vector");
        if (a.size() != 3) invalid("rig vector requires three components");
        return {floatValue(a[0], "rig vector"), floatValue(a[1], "rig vector"), floatValue(a[2], "rig vector")};
    }
    RigNodeId rigId(const JsonValue& value) {
        const auto id = indexValue(value, "rig ID");
        if (id > std::numeric_limits<RigNodeId>::max()) invalid("rig ID overflow");
        return static_cast<RigNodeId>(id);
    }
    Transform rigTransform(const JsonValue& value) {
        rejectUnknownKeys(value, {"position", "anchor", "scale", "rotation", "orientation", "opacity"}, "rig transform");
        Transform t;
        t.position = rigVector(required(value, "position", "transform"));
        t.anchor = rigVector(required(value, "anchor", "transform"));
        t.scale = rigVector(required(value, "scale", "transform"));
        t.rotation = rigVector(required(value, "rotation", "transform"));
        const auto& q = required(value, "orientation", "transform").array("orientation");
        if (q.size() != 4) invalid("orientation requires four components");
        t.orientation.set(floatValue(q[0], "orientation"), floatValue(q[1], "orientation"),
                          floatValue(q[2], "orientation"), floatValue(q[3], "orientation"));
        t.opacity = floatValue(required(value, "opacity", "transform"), "opacity");
        return t;
    }
    void writeRigVector(std::ostream& out, const Vector3& v) { out << '[' << v.x << ',' << v.y << ',' << v.z << ']'; }
    void writeRigTransform(std::ostream& out, const Transform& t) {
        out << "{\"position\":"; writeRigVector(out, t.position);
        out << ",\"anchor\":"; writeRigVector(out, t.anchor);
        out << ",\"scale\":"; writeRigVector(out, t.scale);
        out << ",\"rotation\":"; writeRigVector(out, t.rotation);
        out << ",\"orientation\":[" << t.orientation.x << ',' << t.orientation.y << ',' << t.orientation.z << ',' << t.orientation.w
            << "],\"opacity\":" << t.opacity << '}';
    }
}
    void writeRigNodes(std::ostream& out, const std::vector<RigNode>& nodes) {
    bool first = true;
    for (const auto& n : nodes) {
        if (!first) out << ',';
        first = false;
        out << "{\"id\":" << n.id << ",\"parent\":" << n.parent << ",\"kind\":" << static_cast<unsigned>(n.kind)
            << ",\"layerId\":" << n.layerId << ",\"length\":" << n.length << ",\"primaryAxis\":";
        writeRigVector(out, n.primaryAxis);
        out << ",\"bind\":"; writeRigTransform(out, n.bind);
        out << ",\"rest\":"; writeRigTransform(out, n.rest);
        out << ",\"dependencies\":[";
        auto deps = n.dependencies; std::sort(deps.begin(), deps.end());
        for (std::size_t i = 0; i < deps.size(); ++i) { if (i) out << ','; out << deps[i]; }
        out << "],\"constraints\":[";
        for (std::size_t i = 0; i < n.constraints.size(); ++i) {
            if (i) out << ',';
            const auto& c = n.constraints[i];
            if (c.path || c.amountTrack) invalid("rig graph serialization does not accept path/track handles");
            out << "{\"kind\":" << static_cast<unsigned>(c.kind) << ",\"targetId\":" << c.targetId
                << ",\"amount\":" << c.amount << ",\"distance\":" << c.distance << ",\"progress\":" << c.progress
                << ",\"restLength\":" << c.restLength << ",\"damping\":" << c.damping << ",\"roll\":" << c.roll
                << ",\"orientToPath\":" << (c.orientToPath ? "true" : "false") << ",\"axis\":";
            writeRigVector(out, c.axis); out << ",\"minimum\":"; writeRigVector(out, c.minimum);
            out << ",\"maximum\":"; writeRigVector(out, c.maximum);
            out << ",\"surfaceNormal\":"; writeRigVector(out, c.surfaceNormal);
            out << ",\"surfaceOffset\":"; writeRigVector(out, c.surfaceOffset); out << '}';
        }
        out << "]}";
    }
}
std::string serializeRigGraph(const RigGraph& graph) {
    std::ostringstream out; out.imbue(std::locale::classic());
    out << std::setprecision(std::numeric_limits<float>::max_digits10);
    out << "{\"schema\":\"chrononmotion.rig.v1\",\"nodes\":[";
    writeRigNodes(out, graph.compile().nodes());
    out << "]}";
    return out.str();
}
RigGraph deserializeRigGraph(std::string_view json) {
    const auto root = JsonParser(json).parse();
    rejectUnknownKeys(root, {"schema", "nodes"}, "rig root");
    if (required(root, "schema", "rig root").string("schema") != "chrononmotion.rig.v1") invalid("unknown rig schema");
    const auto& nodes = required(root, "nodes", "rig root").array("nodes");
    if (nodes.size() > 10000) invalid("rig node budget exceeded");
    RigGraph graph;
    std::size_t edges = 0;
    for (const auto& raw : nodes) {
        rejectUnknownKeys(raw, {"id", "parent", "kind", "layerId", "length", "primaryAxis", "bind", "rest", "dependencies", "constraints"}, "rig node");
        RigNode n;
        n.id = rigId(required(raw, "id", "node")); n.parent = rigId(required(raw, "parent", "node"));
        const auto kind = indexValue(required(raw, "kind", "node"), "kind");
        if (kind > static_cast<unsigned>(RigNodeKind::Pin)) invalid("unknown rig node kind");
        n.kind = static_cast<RigNodeKind>(kind); n.layerId = rigId(required(raw, "layerId", "node"));
        n.length = floatValue(required(raw, "length", "node"), "length");
        n.primaryAxis = rigVector(required(raw, "primaryAxis", "node"));
        n.bind = rigTransform(required(raw, "bind", "node")); n.rest = rigTransform(required(raw, "rest", "node"));
        const auto& deps = required(raw, "dependencies", "node").array("dependencies");
        const auto& constraints = required(raw, "constraints", "node").array("constraints");
        edges += deps.size() + constraints.size();
        if (edges > 160000 || constraints.size() > 256) invalid("rig dependency budget exceeded");
        for (const auto& dep : deps) n.dependencies.push_back(rigId(dep));
        for (const auto& value : constraints) {
            rejectUnknownKeys(value, {"kind", "targetId", "amount", "distance", "progress", "restLength", "damping", "roll", "orientToPath",
                                      "axis", "minimum", "maximum", "surfaceNormal", "surfaceOffset"}, "constraint");
            ConstraintSpec c;
            const auto constraintKind = indexValue(required(value, "kind", "constraint"), "kind");
            if (constraintKind > static_cast<unsigned>(ConstraintKind::LookAtWithRoll)) invalid("unknown constraint kind");
            c.kind = static_cast<ConstraintKind>(constraintKind);
            c.targetId = rigId(required(value, "targetId", "constraint"));
            c.amount = floatValue(required(value, "amount", "constraint"), "amount");
            c.distance = floatValue(required(value, "distance", "constraint"), "distance");
            c.progress = floatValue(required(value, "progress", "constraint"), "progress");
            c.restLength = floatValue(required(value, "restLength", "constraint"), "restLength");
            c.damping = floatValue(required(value, "damping", "constraint"), "damping");
            c.roll = floatValue(required(value, "roll", "constraint"), "roll");
            c.orientToPath = required(value, "orientToPath", "constraint").boolean("orientToPath");
            c.axis = rigVector(required(value, "axis", "constraint")); c.minimum = rigVector(required(value, "minimum", "constraint"));
            c.maximum = rigVector(required(value, "maximum", "constraint")); c.surfaceNormal = rigVector(required(value, "surfaceNormal", "constraint"));
            c.surfaceOffset = rigVector(required(value, "surfaceOffset", "constraint")); n.constraints.push_back(c);
        }
        graph.nodes.push_back(std::move(n));
    }
    (void) graph.compile();
    return graph;
}

RigGraph readRigGraphObject(const JsonValue& root) {
    const auto& nodes = required(root, "nodes", "rig root").array("nodes");
    if (nodes.size() > 10000) invalid("rig node budget exceeded");
    RigGraph graph;
    std::size_t edges = 0;
    for (const auto& raw : nodes) {
        rejectUnknownKeys(raw, {"id", "parent", "kind", "layerId", "length", "primaryAxis", "bind", "rest", "dependencies", "constraints"}, "rig node");
        RigNode n;
        n.id = rigId(required(raw, "id", "node")); n.parent = rigId(required(raw, "parent", "node"));
        const auto kind = indexValue(required(raw, "kind", "node"), "kind");
        if (kind > static_cast<unsigned>(RigNodeKind::Pin)) invalid("unknown rig node kind");
        n.kind = static_cast<RigNodeKind>(kind); n.layerId = rigId(required(raw, "layerId", "node"));
        n.length = floatValue(required(raw, "length", "node"), "length");
        n.primaryAxis = rigVector(required(raw, "primaryAxis", "node"));
        n.bind = rigTransform(required(raw, "bind", "node")); n.rest = rigTransform(required(raw, "rest", "node"));
        const auto& deps = required(raw, "dependencies", "node").array("dependencies");
        const auto& constraints = required(raw, "constraints", "node").array("constraints");
        edges += deps.size() + constraints.size();
        if (edges > 160000 || constraints.size() > 256) invalid("rig dependency budget exceeded");
        for (const auto& dep : deps) n.dependencies.push_back(rigId(dep));
        for (const auto& value : constraints) {
            rejectUnknownKeys(value, {"kind", "targetId", "amount", "distance", "progress", "restLength", "damping", "roll", "orientToPath",
                                      "axis", "minimum", "maximum", "surfaceNormal", "surfaceOffset"}, "constraint");
            ConstraintSpec c;
            const auto constraintKind = indexValue(required(value, "kind", "constraint"), "kind");
            if (constraintKind > static_cast<unsigned>(ConstraintKind::LookAtWithRoll)) invalid("unknown constraint kind");
            c.kind = static_cast<ConstraintKind>(constraintKind);
            c.targetId = rigId(required(value, "targetId", "constraint"));
            c.amount = floatValue(required(value, "amount", "constraint"), "amount");
            c.distance = floatValue(required(value, "distance", "constraint"), "distance");
            c.progress = floatValue(required(value, "progress", "constraint"), "progress");
            c.restLength = floatValue(required(value, "restLength", "constraint"), "restLength");
            c.damping = floatValue(required(value, "damping", "constraint"), "damping");
            c.roll = floatValue(required(value, "roll", "constraint"), "roll");
            c.orientToPath = required(value, "orientToPath", "constraint").boolean("orientToPath");
            c.axis = rigVector(required(value, "axis", "constraint")); c.minimum = rigVector(required(value, "minimum", "constraint"));
            c.maximum = rigVector(required(value, "maximum", "constraint")); c.surfaceNormal = rigVector(required(value, "surfaceNormal", "constraint"));
            c.surfaceOffset = rigVector(required(value, "surfaceOffset", "constraint")); n.constraints.push_back(c);
        }
        graph.nodes.push_back(std::move(n));
    }
    (void) graph.compile();
    return graph;
}

void writeRigProgramTransformArray(std::ostream& out, const std::vector<Transform>& transforms) {
    for (std::size_t i = 0; i < transforms.size(); ++i) { if (i) out << ','; writeRigTransform(out, transforms[i]); }
}
void writeRigTrack(std::ostream& out, const Track<float>& track) {
    out << '[';
    for (std::size_t i = 0; i < track.keys().size(); ++i)
        out << (i ? "," : "") << '[' << track.keys()[i].time << ',' << track.keys()[i].value << ']';
    out << ']';
}
Track<float> readRigTrack(const JsonValue& value) {
    Track<float> track;
    float previous = -1.f;
    for (const auto& key : value.array("track")) {
        const auto& pair = key.array("track key");
        if (pair.size() != 2) invalid("track key requires time and value");
        const float time = floatValue(pair[0], "track time");
        if (time < 0.f || time <= previous) invalid("track times must increase");
        previous = time;
        track.add(time, floatValue(pair[1], "track value"));
    }
    return track;
}
void writeRigDeformer(std::ostream& out, const RigDeformer& d) {
    out << "{\"kind\":" << static_cast<unsigned>(d.kind) << ",\"amount\":" << d.amount << ",\"strength\":" << d.strength
        << ",\"wavelength\":" << d.wavelength << ",\"phase\":" << d.phase << ",\"origin\":";
    writeRigVector(out, d.origin);
    out << ",\"seed\":" << d.seed << ",\"lattice\":{\"columns\":" << d.lattice.columns << ",\"rows\":" << d.lattice.rows
        << ",\"minimum\":"; writeRigVector(out, d.lattice.minimum);
    out << ",\"maximum\":"; writeRigVector(out, d.lattice.maximum);
    out << ",\"offsets\":[";
    for (std::size_t i = 0; i < d.lattice.offsets.size(); ++i) { if (i) out << ','; writeRigVector(out, d.lattice.offsets[i]); }
    out << "]},\"pins\":[";
    for (std::size_t i = 0; i < d.pins.size(); ++i) {
        if (i) out << ',';
        const auto& p = d.pins[i];
        out << "{\"kind\":" << static_cast<unsigned>(p.kind) << ",\"rest\":"; writeRigVector(out, p.rest);
        out << ",\"target\":"; writeRigVector(out, p.target);
        out << ",\"rotation\":[" << p.rotation.x << ',' << p.rotation.y << ',' << p.rotation.z << ',' << p.rotation.w
            << "],\"scale\":"; writeRigVector(out, p.scale);
        out << ",\"radius\":" << p.radius << ",\"weight\":" << p.weight << '}';
    }
    out << "]}";
}
RigDeformer readRigDeformer(const JsonValue& value) {
    rejectUnknownKeys(value, {"kind", "amount", "strength", "wavelength", "phase", "origin", "seed", "lattice", "pins"}, "deformer");
    RigDeformer d;
    const auto kind = indexValue(required(value, "kind", "deformer"), "kind");
    if (kind > static_cast<unsigned>(RigDeformerKind::SquashStretch)) invalid("unknown deformer kind");
    d.kind = static_cast<RigDeformerKind>(kind);
    d.amount = floatValue(required(value, "amount", "deformer"), "amount");
    d.strength = floatValue(required(value, "strength", "deformer"), "strength");
    d.wavelength = floatValue(required(value, "wavelength", "deformer"), "wavelength");
    d.phase = floatValue(required(value, "phase", "deformer"), "phase");
    d.origin = rigVector(required(value, "origin", "deformer"));
    d.seed = indexValue(required(value, "seed", "deformer"), "seed");
    const auto& lattice = required(value, "lattice", "deformer");
    rejectUnknownKeys(lattice, {"columns", "rows", "minimum", "maximum", "offsets"}, "lattice");
    d.lattice.columns = indexValue(required(lattice, "columns", "lattice"), "columns");
    d.lattice.rows = indexValue(required(lattice, "rows", "lattice"), "rows");
    d.lattice.minimum = rigVector(required(lattice, "minimum", "lattice"));
    d.lattice.maximum = rigVector(required(lattice, "maximum", "lattice"));
    for (const auto& offset : required(lattice, "offsets", "lattice").array("offsets")) d.lattice.offsets.push_back(rigVector(offset));
    for (const auto& raw : required(value, "pins", "deformer").array("pins")) {
        rejectUnknownKeys(raw, {"kind", "rest", "target", "rotation", "scale", "radius", "weight"}, "pin");
        RigPin p;
        const auto pinKind = indexValue(required(raw, "kind", "pin"), "kind");
        if (pinKind > static_cast<unsigned>(RigPinKind::Stiffness)) invalid("unknown pin kind");
        p.kind = static_cast<RigPinKind>(pinKind);
        p.rest = rigVector(required(raw, "rest", "pin")); p.target = rigVector(required(raw, "target", "pin"));
        const auto& rotation = required(raw, "rotation", "pin").array("rotation");
        if (rotation.size() != 4) invalid("pin rotation requires four components");
        p.rotation.set(floatValue(rotation[0], "rotation"), floatValue(rotation[1], "rotation"),
                       floatValue(rotation[2], "rotation"), floatValue(rotation[3], "rotation"));
        p.scale = rigVector(required(raw, "scale", "pin"));
        p.radius = floatValue(required(raw, "radius", "pin"), "radius");
        p.weight = floatValue(required(raw, "weight", "pin"), "weight");
        d.pins.push_back(p);
    }
    return d;
}
std::string serializeRigProgram(const RigProgram& program) {
    (void) RigProgramEvaluator(program); // Fail closed on invalid programs before encoding.
    for (const auto& geometry : program.geometry) {
        for (const auto& op : geometry.deformers.operators)
            if (op.path) invalid("RigProgram serialization does not accept MotionPath handles");
        if (!geometry.inverseBinds.empty() || !geometry.morphTracks.empty())
            invalid("RigProgram serialization excludes import-derived data; re-derive it from glTF");
    }
    if (!program.animation.empty() || !program.spaces.empty() || !program.ik.empty() ||
        !program.weights.empty() || !program.secondary.empty())
        invalid("RigProgram serialization v1 covers graph, controllers, constraints, deformers and poses; schedules are re-derived at authoring time");
    const auto rig = program.graph.compile();
    std::ostringstream out; out.imbue(std::locale::classic());
    out << std::setprecision(std::numeric_limits<float>::max_digits10);
    out << "{\"schema\":\"chrononmotion.rig.program.v1\",\"graph\":{\"nodes\":[";
    writeRigNodes(out, rig.nodes());
    out << "]},\"drivers\":{\"controllers\":[";
    for (std::size_t i = 0; i < program.drivers.controllers.size(); ++i) {
        if (i) out << ',';
        const auto& c = program.drivers.controllers[i];
        out << "{\"id\":" << c.id << ",\"kind\":" << static_cast<unsigned>(c.kind) << ",\"neutral\":";
        writeRigVector(out, c.neutral); out << ",\"minimum\":"; writeRigVector(out, c.minimum);
        out << ",\"maximum\":"; writeRigVector(out, c.maximum); out << '}';
    }
    out << "],\"nodes\":[";
    for (std::size_t i = 0; i < program.drivers.nodes.size(); ++i) {
        if (i) out << ',';
        const auto& n = program.drivers.nodes[i];
        out << "{\"id\":" << n.id << ",\"kind\":" << static_cast<unsigned>(n.kind) << ",\"inputs\":[" << n.inputs[0] << ',' << n.inputs[1]
            << "],\"controller\":" << n.controller << ",\"component\":" << n.component << ",\"first\":" << n.first
            << ",\"second\":" << n.second << ",\"value\":" << n.value << ",\"inMin\":" << n.inMin << ",\"inMax\":" << n.inMax
            << ",\"outMin\":" << n.outMin << ",\"outMax\":" << n.outMax << ",\"seed\":" << n.seed << ",\"track\":";
        writeRigTrack(out, n.track); out << '}';
    }
    out << "],\"outputs\":[";
    for (std::size_t i = 0; i < program.drivers.outputs.size(); ++i) {
        if (i) out << ',';
        const auto& o = program.drivers.outputs[i];
        out << "{\"driver\":" << o.driver << ",\"target\":" << o.target << ",\"property\":" << static_cast<unsigned>(o.property)
            << ",\"component\":" << o.component << '}';
    }
    out << "]},\"poses\":[";
    for (std::size_t i = 0; i < program.poses.size(); ++i) {
        if (i) out << ',';
        const auto& layer = program.poses[i];
        out << "{\"mode\":" << static_cast<unsigned>(layer.mode) << ",\"weight\":" << layer.weight << ",\"mask\":[";
        for (std::size_t k = 0; k < layer.mask.size(); ++k) { if (k) out << ','; out << layer.mask[k]; }
        out << "],\"pose\":[";
        for (std::size_t k = 0; k < layer.pose.size(); ++k) {
            if (k) out << ',';
            out << "{\"id\":" << layer.pose[k].id << ",\"transform\":"; writeRigTransform(out, layer.pose[k].local); out << '}';
        }
        out << "]}";
    }
    out << "],\"geometry\":[";
    for (std::size_t i = 0; i < program.geometry.size(); ++i) {
        if (i) out << ',';
        const auto& g = program.geometry[i];
        out << "{\"id\":" << g.id << ",\"rigidNode\":" << g.rigidNode << ",\"source\":[";
        for (std::size_t k = 0; k < g.source.size(); ++k) { if (k) out << ','; writeRigVector(out, g.source[k]); }
        out << ']';
        if (!g.sourceNormals.empty()) {
            out << ",\"sourceNormals\":[";
            for (std::size_t k = 0; k < g.sourceNormals.size(); ++k) { if (k) out << ','; writeRigVector(out, g.sourceNormals[k]); }
            out << ']';
        }
        out << ",\"morphs\":[";
        for (std::size_t k = 0; k < g.morphs.size(); ++k) {
            if (k) out << ',';
            out << "{\"weight\":" << g.morphs[k].weight << ",\"positions\":[";
            for (std::size_t v = 0; v < g.morphs[k].positions.size(); ++v) { if (v) out << ','; writeRigVector(out, g.morphs[k].positions[v]); }
            out << ']';
            if (!g.morphs[k].normals.empty()) {
                out << ",\"normals\":[";
                for (std::size_t v = 0; v < g.morphs[k].normals.size(); ++v) { if (v) out << ','; writeRigVector(out, g.morphs[k].normals[v]); }
                out << ']';
            }
            out << '}';
        }
        out << "],\"skin\":[";
        for (std::size_t k = 0; k < g.skin.size(); ++k) {
            if (k) out << ',';
            out << "{\"j\":[" << g.skin[k].joints[0] << ',' << g.skin[k].joints[1] << ',' << g.skin[k].joints[2] << ',' << g.skin[k].joints[3]
                << "],\"w\":[" << g.skin[k].weights[0] << ',' << g.skin[k].weights[1] << ',' << g.skin[k].weights[2] << ',' << g.skin[k].weights[3] << "]}";
        }
        out << "],\"indices\":[";
        for (std::size_t k = 0; k < g.indices.size(); ++k) { if (k) out << ','; out << g.indices[k]; }
        out << "],\"deformers\":[";
        for (std::size_t k = 0; k < g.deformers.operators.size(); ++k) { if (k) out << ','; writeRigDeformer(out, g.deformers.operators[k]); }
        out << "]}";
    }
    out << "]}";
    return out.str();
}
RigProgram deserializeRigProgram(std::string_view json) {
    const auto root = JsonParser(json).parse();
    rejectUnknownKeys(root, {"schema", "graph", "drivers", "poses", "geometry"}, "rig program root");
    if (required(root, "schema", "rig program root").string("schema") != "chrononmotion.rig.program.v1")
        invalid("unknown rig program schema");
    RigProgram program;
    program.graph = readRigGraphObject(required(root, "graph", "rig program root"));
    (void) program.graph; // Schema/keys already validated by the program reader.
    const auto& drivers = required(root, "drivers", "rig program root");
    rejectUnknownKeys(drivers, {"controllers", "nodes", "outputs"}, "drivers");
    for (const auto& raw : required(drivers, "controllers", "drivers").array("controllers")) {
        rejectUnknownKeys(raw, {"id", "kind", "neutral", "minimum", "maximum"}, "controller");
        RigController c;
        c.id = rigId(required(raw, "id", "controller"));
        const auto kind = indexValue(required(raw, "kind", "controller"), "kind");
        if (kind > static_cast<unsigned>(RigControllerKind::Enum)) invalid("unknown controller kind");
        c.kind = static_cast<RigControllerKind>(kind);
        c.neutral = rigVector(required(raw, "neutral", "controller"));
        c.minimum = rigVector(required(raw, "minimum", "controller"));
        c.maximum = rigVector(required(raw, "maximum", "controller"));
        program.drivers.controllers.push_back(c);
    }
    for (const auto& raw : required(drivers, "nodes", "drivers").array("nodes")) {
        rejectUnknownKeys(raw, {"id", "kind", "inputs", "controller", "component", "first", "second", "value",
                                "inMin", "inMax", "outMin", "outMax", "seed", "track"}, "driver node");
        RigDriverNode n;
        n.id = indexValue(required(raw, "id", "driver node"), "driver id");
        const auto kind = indexValue(required(raw, "kind", "driver node"), "kind");
        if (kind > static_cast<unsigned>(RigDriverKind::Noise)) invalid("unknown driver kind");
        n.kind = static_cast<RigDriverKind>(kind);
        const auto& inputs = required(raw, "inputs", "driver node").array("inputs");
        if (inputs.size() != 2) invalid("driver inputs require two slots");
        n.inputs = {static_cast<std::uint32_t>(indexValue(inputs[0], "input")), static_cast<std::uint32_t>(indexValue(inputs[1], "input"))};
        n.controller = rigId(required(raw, "controller", "driver node"));
        n.component = indexValue(required(raw, "component", "driver node"), "component");
        n.first = rigId(required(raw, "first", "driver node")); n.second = rigId(required(raw, "second", "driver node"));
        n.value = floatValue(required(raw, "value", "driver node"), "value");
        n.inMin = floatValue(required(raw, "inMin", "driver node"), "inMin");
        n.inMax = floatValue(required(raw, "inMax", "driver node"), "inMax");
        n.outMin = floatValue(required(raw, "outMin", "driver node"), "outMin");
        n.outMax = floatValue(required(raw, "outMax", "driver node"), "outMax");
        n.seed = indexValue(required(raw, "seed", "driver node"), "seed");
        n.track = readRigTrack(required(raw, "track", "driver node"));
        program.drivers.nodes.push_back(n);
    }
    for (const auto& raw : required(drivers, "outputs", "drivers").array("outputs")) {
        rejectUnknownKeys(raw, {"driver", "target", "property", "component"}, "driver output");
        RigDriverOutput o;
        o.driver = indexValue(required(raw, "driver", "output"), "driver");
        o.target = rigId(required(raw, "target", "output"));
        const auto property = indexValue(required(raw, "property", "output"), "property");
        if (property > static_cast<unsigned>(RigDrivenProperty::Opacity)) invalid("unknown driven property");
        o.property = static_cast<RigDrivenProperty>(property);
        o.component = indexValue(required(raw, "component", "output"), "component");
        program.drivers.outputs.push_back(o);
    }
    for (const auto& raw : required(root, "poses", "rig program root").array("poses")) {
        rejectUnknownKeys(raw, {"mode", "weight", "mask", "pose"}, "pose layer");
        RigPoseLayer layer;
        const auto mode = indexValue(required(raw, "mode", "pose layer"), "mode");
        if (mode > static_cast<unsigned>(RigPoseBlendMode::MaskedBlend)) invalid("unknown pose blend mode");
        layer.mode = static_cast<RigPoseBlendMode>(mode);
        layer.weight = floatValue(required(raw, "weight", "pose layer"), "weight");
        for (const auto& mask : required(raw, "mask", "pose layer").array("mask")) layer.mask.push_back(rigId(mask));
        for (const auto& value : required(raw, "pose", "pose layer").array("pose")) {
            rejectUnknownKeys(value, {"id", "transform"}, "pose value");
            layer.pose.push_back({rigId(required(value, "id", "pose value")), rigTransform(required(value, "transform", "pose value"))});
        }
        program.poses.push_back(std::move(layer));
    }
    for (const auto& raw : required(root, "geometry", "rig program root").array("geometry")) {
        rejectUnknownKeys(raw, {"id", "rigidNode", "source", "sourceNormals", "morphs", "skin", "indices", "deformers"}, "geometry");
        RigGeometryBinding g;
        g.id = indexValue(required(raw, "id", "geometry"), "geometry id");
        g.rigidNode = rigId(required(raw, "rigidNode", "geometry"));
        for (const auto& point : required(raw, "source", "geometry").array("source")) g.source.push_back(rigVector(point));
        if (const auto* normals = find(raw, "sourceNormals"))
            for (const auto& normal : normals->array("sourceNormals")) g.sourceNormals.push_back(rigVector(normal));
        for (const auto& morph : required(raw, "morphs", "geometry").array("morphs")) {
            rejectUnknownKeys(morph, {"weight", "positions", "normals"}, "morph");
            RigMorphTarget m;
            m.weight = floatValue(required(morph, "weight", "morph"), "weight");
            for (const auto& point : required(morph, "positions", "morph").array("positions")) m.positions.push_back(rigVector(point));
            if (const auto* normals = find(morph, "normals"))
                for (const auto& normal : normals->array("normals")) m.normals.push_back(rigVector(normal));
            g.morphs.push_back(std::move(m));
        }
        for (const auto& vertex : required(raw, "skin", "geometry").array("skin")) {
            rejectUnknownKeys(vertex, {"j", "w"}, "skin vertex");
            RigSkinVertex s;
            const auto& joints = required(vertex, "j", "skin vertex").array("joints");
            const auto& weights = required(vertex, "w", "skin vertex").array("weights");
            if (joints.size() != 4 || weights.size() != 4) invalid("skin vertex requires four influences");
            for (unsigned i = 0; i < 4; ++i) {
                const auto joint = rigId(joints[i]);
                if (joint > std::numeric_limits<RigNodeId>::max()) invalid("joint overflow");
                s.joints[i] = joint; s.weights[i] = floatValue(weights[i], "weight");
            }
            g.skin.push_back(s);
        }
        for (const auto& index : required(raw, "indices", "geometry").array("indices")) {
            const auto value = indexValue(index, "triangle index");
            if (value > std::numeric_limits<std::uint32_t>::max()) invalid("triangle index overflow");
            g.indices.push_back(static_cast<std::uint32_t>(value));
        }
        for (const auto& value : required(raw, "deformers", "geometry").array("deformers")) g.deformers.operators.push_back(readRigDeformer(value));
        program.geometry.push_back(std::move(g));
    }
    (void) RigProgramEvaluator(program);
    return program;
}
} // namespace motion


} // namespace chrononmotion
