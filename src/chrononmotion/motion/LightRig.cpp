#include "chrononmotion/motion/LightRig.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <set>
#include <sstream>
#include <stdexcept>

namespace chrononmotion::motion {
namespace {
void requireFinite(float value, const char* field) {
    if (!std::isfinite(value)) throw std::invalid_argument(std::string("LightRig: non-finite ") + field);
}

void writeVec(std::ostringstream& out, const Vector3& v) {
    out << '[' << v.x << ',' << v.y << ',' << v.z << ']';
}

void writeTrack(std::ostringstream& out, const Track<Vector3>& track) {
    out << '[';
    bool first = true;
    for (const auto& key : track.keys()) {
        if (!first) out << ',';
        first = false;
        out << "{\"time\":" << key.time << ",\"value\":";
        writeVec(out, key.value);
        const auto& easing = key.easing;
        out << ",\"discrete\":" << (key.discrete ? "true" : "false")
            << ",\"easing\":[" << static_cast<int>(easing.kind()) << ',' << easing.x1() << ','
            << easing.y1() << ',' << easing.x2() << ',' << easing.y2() << ',' << easing.stepCount() << "]}";
    }
    out << ']';
}

void writeString(std::ostringstream& out, const std::string& value) {
    out << '"';
    for (unsigned char c : value) {
        if (c == '"' || c == '\\') out << '\\' << static_cast<char>(c);
        else if (c < 0x20) {
            constexpr char hex[] = "0123456789abcdef";
            out << "\\u00" << hex[c >> 4] << hex[c & 15];
        } else out << static_cast<char>(c);
    }
    out << '"';
}

const char* kindName(LightKind kind) {
    switch (kind) {
        case LightKind::Directional: return "directional";
        case LightKind::Point: return "point";
        case LightKind::Spot: return "spot";
        case LightKind::Ambient: break;
    }
    throw std::invalid_argument("LightRig: ambient sources are unsupported in V1");
}
} // namespace

void LightRig::validate() const {
    if (id.empty() || name.empty()) throw std::invalid_argument("LightRig: id and name are required");
    if (sources.empty() || sources.size() > kMaxSources)
        throw std::invalid_argument("LightRig: source count must be 1..16");
    std::set<std::uint32_t> ids;
    for (const auto& source : sources) {
        if (source.id == 0 || !ids.insert(source.id).second)
            throw std::invalid_argument("LightRig: source ids must be unique and nonzero");
        (void)kindName(source.kind);
        requireFinite(source.intensity, "intensity");
        requireFinite(source.range, "range");
        requireFinite(source.decay, "decay");
        requireFinite(source.angle, "angle");
        requireFinite(source.penumbra, "penumbra");
        if (source.intensity < 0.f || source.range < 0.f || source.decay < 0.f ||
            source.penumbra < 0.f || source.penumbra > 1.f ||
            (source.kind == LightKind::Spot && (source.angle <= 0.f || source.angle > 3.14159265358979323846f)))
            throw std::invalid_argument("LightRig: source parameters are outside supported ranges");
        if (source.direction.lengthSq() == 0.f)
            throw std::invalid_argument("LightRig: source direction must be nonzero");
        for (float component : {source.color.r, source.color.g, source.color.b}) {
            requireFinite(component, "color");
            if (component < 0.f || component > 1.f)
                throw std::invalid_argument("LightRig: color components must be within [0,1]");
        }
    }
}

std::vector<LightSample> LightRig::sample(float seconds) const {
    validate();
    requireFinite(seconds, "time");
    Transform rigPose = transform;
    if (!positionTrack.empty()) rigPose.position = positionTrack.sample(seconds);
    if (!rotationTrack.empty()) rigPose.rotation = rotationTrack.sample(seconds);
    const Matrix4 rigMatrix = rigPose.toMatrix();
    std::vector<LightSample> result;
    result.reserve(sources.size());
    for (const auto& source : sources) {
        const Matrix4 localMatrix = source.local.toMatrix();
        LightSample sample;
        sample.kind = source.kind;
        sample.id = source.id;
        sample.local = source.local;
        sample.local.position.applyMatrix4(rigMatrix);
        sample.direction = source.direction;
        sample.direction.transformDirection(localMatrix).transformDirection(rigMatrix);
        sample.color = source.color;
        sample.intensity = source.intensity;
        sample.range = source.range;
        sample.decay = source.decay;
        sample.angle = source.angle;
        sample.penumbra = source.penumbra;
        sample.castsShadow = source.castsShadow;
        result.push_back(sample);
    }
    return result;
}

std::string LightRig::serialize() const {
    validate();
    std::ostringstream out;
    out << std::setprecision(9) << "{\"schema\":\"LIGHT_RIG_V1\",\"id\":";
    writeString(out, id);
    out << ",\"name\":"; writeString(out, name);
    out << ",\"position\":"; writeVec(out, transform.position);
    out << ",\"anchor\":"; writeVec(out, transform.anchor);
    out << ",\"scale\":"; writeVec(out, transform.scale);
    out << ",\"rotation\":"; writeVec(out, transform.rotation);
    out << ",\"orientation\":[" << transform.orientation.x << ',' << transform.orientation.y << ','
        << transform.orientation.z << ',' << transform.orientation.w << ']';
    out << ",\"position_track\":"; writeTrack(out, positionTrack);
    out << ",\"rotation_track\":"; writeTrack(out, rotationTrack);
    out << ",\"sources\":[";
    for (std::size_t i = 0; i < sources.size(); ++i) {
        const auto& s = sources[i];
        if (i) out << ',';
        out << "{\"id\":" << s.id << ",\"name\":"; writeString(out, s.name);
        out << ",\"kind\":\"" << kindName(s.kind) << "\",\"position\":";
        writeVec(out, s.local.position);
        out << ",\"anchor\":"; writeVec(out, s.local.anchor);
        out << ",\"scale\":"; writeVec(out, s.local.scale);
        out << ",\"rotation\":"; writeVec(out, s.local.rotation);
        out << ",\"orientation\":[" << s.local.orientation.x << ',' << s.local.orientation.y << ','
            << s.local.orientation.z << ',' << s.local.orientation.w << ']';
        out << ",\"direction\":"; writeVec(out, s.direction);
        out << ",\"color\":[" << s.color.r << ',' << s.color.g << ',' << s.color.b << ",1"
            << "],\"intensity\":" << s.intensity << ",\"range\":" << s.range
            << ",\"decay\":" << s.decay << ",\"angle\":" << s.angle
            << ",\"penumbra\":" << s.penumbra << ",\"casts_shadow\":"
            << (s.castsShadow ? "true" : "false") << '}';
    }
    out << "]}";
    return out.str();
}

namespace lightRigPresets {
const std::vector<std::string_view>& catalog() {
    static const std::vector<std::string_view> values{
        "editorial_key_rim_fill", "four_spot_edge", "soft_top"};
    return values;
}

LightRig make(std::string_view presetId) {
    LightRig rig;
    rig.id = std::string(presetId);
    rig.name = rig.id;
    const auto add = [&](std::uint32_t id, std::string name, Vector3 position,
                         Color color, float intensity, bool shadow = false) {
        LightRigSource source;
        source.id = id; source.name = std::move(name); source.local.position = position;
        source.color = color; source.intensity = intensity; source.castsShadow = shadow;
        rig.sources.push_back(std::move(source));
    };
    if (presetId == "editorial_key_rim_fill") {
        add(1, "key", {-3.f, 4.f, 5.f}, {1.f, .84f, .68f}, 1.2f, true);
        add(2, "rim", {0.f, 3.f, -4.f}, {.35f, .65f, 1.f}, .8f);
        add(3, "fill", {4.f, 1.f, 3.f}, {.62f, .75f, 1.f}, .45f);
    } else if (presetId == "four_spot_edge") {
        add(1, "edge_nw", {-4.f, 3.f, -3.f}, {1.f, .4f, .25f}, .9f);
        add(2, "edge_ne", {4.f, 3.f, -3.f}, {.3f, .55f, 1.f}, .9f);
        add(3, "edge_sw", {-4.f, -2.f, -3.f}, {.65f, .3f, 1.f}, .65f);
        add(4, "edge_se", {4.f, -2.f, -3.f}, {1.f, .7f, .3f}, .65f);
    } else if (presetId == "soft_top") {
        add(1, "top_key", {0.f, 5.f, 1.f}, {1.f, .95f, .82f}, .9f, true);
        add(2, "front_fill", {0.f, 1.f, 5.f}, {.7f, .8f, 1.f}, .35f);
    } else {
        throw std::invalid_argument("LightRig: unknown preset id");
    }
    rig.validate();
    return rig;
}

void animateRotation(LightRig& rig, float startSeconds, float endSeconds,
                     const Vector3& from, const Vector3& to, const Easing& easing) {
    if (!std::isfinite(startSeconds) || !std::isfinite(endSeconds) || startSeconds < 0.f ||
        endSeconds <= startSeconds)
        throw std::invalid_argument("LightRig: rotation interval must be finite and increasing");
    rig.rotationTrack.add(startSeconds, from, Easing::linear());
    rig.rotationTrack.add(endSeconds, to, easing);
}
} // namespace lightRigPresets
} // namespace chrononmotion::motion
