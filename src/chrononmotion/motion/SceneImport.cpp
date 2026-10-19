// ChrononMotion — strict project-file reader (see SceneImport.hpp).
//
// Accepts exactly what SceneExport.cpp writes. Key order does not matter (JSON
// objects are unordered by definition); everything else is pinned: the field
// sets below mirror the writer one for one, and any deviation fails closed.

#include "chrononmotion/motion/SceneImport.hpp"

#include "chrononmotion/motion/MotionScene.hpp"
#include "chrononmotion/motion/Lighting.hpp"

#include "JsonDom.hpp"

#include <cstdint>
#include <limits>
#include <memory>
#include <set>
#include <string>
#include <type_traits>

namespace chrononmotion::motion {
    namespace {

        using detail::JsonValue;
        using detail::at;
        using detail::find;
        using detail::floatValue;
        using detail::indexValue;
        using detail::JsonParser;
        using detail::rejectUnknownKeys;
        using detail::required;

        [[noreturn]] void reject(std::string_view where, std::string_view reason) {
            throw std::invalid_argument("scene import: " + std::string(where) + " " + std::string(reason));
        }

        // required() on an already-opened object: same missing-key failure,
        // without reopening what the caller already holds.
        const JsonValue& required(const JsonValue::Object& object, std::string_view key,
                                  std::string_view where) {
            const auto it = object.find(key);
            if (it == object.end())
                throw std::invalid_argument("scene import: " + std::string(where) + " is missing '" +
                                            std::string(key) + "'");
            return it->second;
        }

        std::uint32_t idValue(const JsonValue& value, std::string_view where) {
            const std::size_t raw = indexValue(value, where);
            if (raw > std::numeric_limits<std::uint32_t>::max())
                reject(where, "exceeds the 32-bit id range");
            return static_cast<std::uint32_t>(raw);
        }

        int intValue(const JsonValue& value, std::string_view where) {
            const double raw = value.number(where);
            if (std::floor(raw) != raw || raw < static_cast<double>(std::numeric_limits<int>::min()) ||
                raw > static_cast<double>(std::numeric_limits<int>::max()))
                reject(where, "must be an integer");
            return static_cast<int>(raw);
        }

        Vector3 readVector3(const JsonValue& value, std::string_view where) {
            const auto& items = value.array(where);
            if (items.size() != 3) reject(where, "must be a 3-element array");
            return Vector3(floatValue(items[0], where), floatValue(items[1], where), floatValue(items[2], where));
        }

        Quaternion readQuaternion(const JsonValue& value, std::string_view where) {
            const auto& items = value.array(where);
            if (items.size() != 4) reject(where, "must be a 4-element array");
            return Quaternion(floatValue(items[0], where), floatValue(items[1], where),
                              floatValue(items[2], where), floatValue(items[3], where));
        }

        Color readColor(const JsonValue& value, std::string_view where) {
            const auto& items = value.array(where);
            if (items.size() != 3) reject(where, "must be a 3-element array");
            return Color(floatValue(items[0], where), floatValue(items[1], where), floatValue(items[2], where));
        }

        Easing readEasing(const JsonValue& value, std::string_view where) {
            if (std::holds_alternative<std::nullptr_t>(value.value)) return Easing::linear();
            const std::string kind = required(value, "kind", where).string("easing kind");
            if (kind == "bezier") {
                rejectUnknownKeys(value, {"kind", "controlPoints"}, where);
                const auto& points = required(value, "controlPoints", where).array("bezier controlPoints");
                if (points.size() != 4) reject(where, "bezier needs exactly 4 control points");
                return Easing::cubicBezier(floatValue(points[0], where), floatValue(points[1], where),
                                           floatValue(points[2], where), floatValue(points[3], where));
            }
            if (kind == "steps") {
                rejectUnknownKeys(value, {"kind", "count"}, where);
                const std::size_t count = indexValue(required(value, "count", where), "steps count");
                if (count < 1 || count > static_cast<std::size_t>(std::numeric_limits<int>::max()))
                    reject(where, "steps count is out of range");
                return Easing::steps(static_cast<int>(count));
            }
            rejectUnknownKeys(value, {"kind"}, where);
            if (kind == "bounceOut") return Easing::bounceOut();
            if (kind == "elasticOut") return Easing::elasticOut();
            if (kind == "expoOut") return Easing::expoOut();
            if (kind == "circOut") return Easing::circOut();
            // A linear segment exports as null and never reaches this branch;
            // a spelled "linear" is not an export shape.
            reject(where, "has an unknown easing kind");
        }

        TrackChannel readChannel(const JsonValue& value, std::string_view where) {
            const std::string name = value.string(where);
            if (name == "position") return TrackChannel::Position;
            if (name == "anchor") return TrackChannel::Anchor;
            if (name == "scale") return TrackChannel::Scale;
            if (name == "rotation") return TrackChannel::Rotation;
            if (name == "orientation") return TrackChannel::Orientation;
            if (name == "opacity") return TrackChannel::Opacity;
            if (name == "timeRemap") return TrackChannel::TimeRemap;
            reject(where, "names an unknown channel");
        }

        template<class T>
        T readTrackValue(const JsonValue& value, std::string_view where);

        template<>
        float readTrackValue<float>(const JsonValue& value, std::string_view where) {
            return floatValue(value, where);
        }

        template<>
        Vector3 readTrackValue<Vector3>(const JsonValue& value, std::string_view where) {
            return readVector3(value, where);
        }

        template<>
        Quaternion readTrackValue<Quaternion>(const JsonValue& value, std::string_view where) {
            return readQuaternion(value, where);
        }

        template<>
        Color readTrackValue<Color>(const JsonValue& value, std::string_view where) {
            return readColor(value, where);
        }

        void readModifier(Track<float>& track, const JsonValue& value, std::string_view where);
        void readModifier(Track<Vector3>& track, const JsonValue& value, std::string_view where);
        void readModifier(Track<Quaternion>& track, const JsonValue& value, std::string_view where);
        void readModifier(Track<Color>& track, const JsonValue& value, std::string_view where);

        TrackModifier readModifierValue(const JsonValue& value, std::string_view where) {
            const std::string kind = required(value, "kind", where).string("modifier kind");
            if (kind == "loop") {
                rejectUnknownKeys(value, {"kind", "start", "end", "mode"}, where);
                const std::string mode = required(value, "mode", where).string("loop mode");
                if (mode != "repeat" && mode != "pingPong") reject(where, "has an unknown loop mode");
                return TrackModifier{LoopModifier{floatValue(required(value, "start", where), "loop start"),
                                                  floatValue(required(value, "end", where), "loop end"),
                                                  mode == "pingPong" ? LoopMode::PingPong : LoopMode::Repeat}};
            }
            if (kind == "posterize") {
                rejectUnknownKeys(value, {"kind", "samplesPerSecond"}, where);
                return TrackModifier{
                    PosterizeModifier{floatValue(required(value, "samplesPerSecond", where), "posterize rate")}};
            }
            if (kind == "delay") {
                rejectUnknownKeys(value, {"kind", "seconds"}, where);
                return TrackModifier{DelayModifier{floatValue(required(value, "seconds", where), "delay")}};
            }
            if (kind == "link") {
                rejectUnknownKeys(value, {"kind", "layerId", "channel", "originTime"}, where);
                return TrackModifier{LinkModifier{idValue(required(value, "layerId", where), "link layer"),
                                                  readChannel(required(value, "channel", where), "link channel"),
                                                  floatValue(required(value, "originTime", where), "link origin")}};
            }
            if (kind == "window") {
                rejectUnknownKeys(value, {"kind", "start", "end"}, where);
                return TrackModifier{WindowModifier{floatValue(required(value, "start", where), "window start"),
                                                    floatValue(required(value, "end", where), "window end")}};
            }
            if (kind == "influence") {
                rejectUnknownKeys(value, {"kind", "amount", "originTime"}, where);
                return TrackModifier{InfluenceModifier{floatValue(required(value, "amount", where), "influence"),
                                                       floatValue(required(value, "originTime", where),
                                                                  "influence origin")}};
            }
            if (kind == "smooth") {
                rejectUnknownKeys(value, {"kind", "radius", "samples"}, where);
                const std::size_t samples = indexValue(required(value, "samples", where), "smooth samples");
                if (samples > static_cast<std::size_t>(std::numeric_limits<int>::max()))
                    reject(where, "smooth samples are out of range");
                return TrackModifier{SmoothModifier{floatValue(required(value, "radius", where), "smooth radius"),
                                                    static_cast<int>(samples)}};
            }
            reject(where, "has an unknown modifier kind");
        }

        template<class T>
        void readTrackModifiers(Track<T>& track, const JsonValue& value, std::string_view where) {
            for (const auto& item : value.array(where))
                std::visit([&track](const auto& modifier) { track.addModifier(modifier); },
                           readModifierValue(item, where));
        }

        void readModifier(Track<float>& track, const JsonValue& value, std::string_view where) {
            readTrackModifiers(track, value, where);
        }
        void readModifier(Track<Vector3>& track, const JsonValue& value, std::string_view where) {
            readTrackModifiers(track, value, where);
        }
        void readModifier(Track<Quaternion>& track, const JsonValue& value, std::string_view where) {
            readTrackModifiers(track, value, where);
        }
        void readModifier(Track<Color>& track, const JsonValue& value, std::string_view where) {
            readTrackModifiers(track, value, where);
        }

        template<class T>
        void readTrack(Track<T>& track, const JsonValue& value, std::string_view where) {
            rejectUnknownKeys(value, {"keys", "modifiers"}, where);
            for (const auto& item : required(value, "keys", where).array("track keys")) {
                const auto& key = item.object("track key");
                rejectUnknownKeys(item, {"time", "value", "easing", "discrete"}, "track key");
                const float time = floatValue(required(key, "time", where), "key time");
                const T keyValue = readTrackValue<T>(required(key, "value", where), "key value");
                const Easing easing = readEasing(required(key, "easing", where), "key easing");
                if (required(key, "discrete", where).boolean("key discrete")) track.addDiscrete(time, keyValue);
                else track.add(time, keyValue, easing);
            }
            readModifier(track, required(value, "modifiers", where), "track modifiers");
        }

        ContentRef readContent(const JsonValue& value, std::string_view where) {
            if (std::holds_alternative<std::nullptr_t>(value.value)) return ContentRef{};
            rejectUnknownKeys(value, {"id", "kind", "naturalSize", "anchor", "fingerprint"}, where);
            const std::string id = required(value, "id", where).string("content id");
            const std::string kind = required(value, "kind", where).string("content kind");
            ContentKind contentKind = ContentKind::None;
            if (kind == "text") contentKind = ContentKind::Text;
            else if (kind == "image") contentKind = ContentKind::Image;
            else if (kind == "video") contentKind = ContentKind::Video;
            else if (kind == "mesh") contentKind = ContentKind::Mesh;
            else if (kind != "none") reject(where, "has an unknown content kind");
            const auto& size = required(value, "naturalSize", where).array("content size");
            if (size.size() != 2) reject(where, "content size must be a 2-element array");
            const auto& pivot = required(value, "anchor", where).array("content anchor");
            if (pivot.size() != 2) reject(where, "content anchor must be a 2-element array");
            const JsonValue& print = required(value, "fingerprint", where);
            ContentMetrics metrics;
            if (!std::holds_alternative<std::nullptr_t>(print.value)) metrics.fingerprint = print.string(where);
            metrics.naturalSize = Vector2(floatValue(size[0], where), floatValue(size[1], where));
            metrics.anchor = Vector2(floatValue(pivot[0], where), floatValue(pivot[1], where));
            return ContentRef{std::move(id), contentKind, std::move(metrics)};
        }

        MaterialDescriptor readMaterial(const JsonValue& value, std::string_view where) {
            rejectUnknownKeys(value, {"name", "kind", "baseColor", "emissiveColor", "emissiveStrength", "roughness",
                                      "metalness", "opacity", "transparent"},
                              where);
            MaterialDescriptor material;
            material.name = required(value, "name", where).string("material name");
            const std::string kind = required(value, "kind", where).string("material kind");
            if (kind == "unlit") material.kind = MaterialKind::Unlit;
            else if (kind == "lambert") material.kind = MaterialKind::Lambert;
            else if (kind == "pbr") material.kind = MaterialKind::PBR;
            else if (kind == "emissive") material.kind = MaterialKind::Emissive;
            else reject(where, "has an unknown material kind");
            material.baseColor = readColor(required(value, "baseColor", where), "material base");
            material.emissiveColor = readColor(required(value, "emissiveColor", where), "material emissive");
            material.emissiveStrength = floatValue(required(value, "emissiveStrength", where), "material glow");
            material.roughness = floatValue(required(value, "roughness", where), "material roughness");
            material.metalness = floatValue(required(value, "metalness", where), "material metalness");
            material.opacity = floatValue(required(value, "opacity", where), "material opacity");
            material.transparent = required(value, "transparent", where).boolean("material transparent");
            material.validate();
            return material;
        }

        MeshAttachment readMesh(const JsonValue& value, std::string_view where) {
            if (std::holds_alternative<std::nullptr_t>(value.value)) return MeshAttachment{};
            rejectUnknownKeys(value, {"assetId", "geometry", "material"}, where);
            // Vertex buffers are never serialized: a mesh that carried geometry
            // cannot be rebuilt from the record, so it fails instead of
            // reappearing with silently different pixels.
            if (required(value, "geometry", where).boolean("mesh geometry"))
                reject(where, "carries geometry whose buffers were never serialized");
            MeshAttachment mesh;
            mesh.assetId = required(value, "assetId", where).string("mesh asset");
            mesh.material = readMaterial(required(value, "material", where), "mesh material");
            return mesh;
        }

        Transform readTransform(const JsonValue& value, std::string_view where) {
            rejectUnknownKeys(value, {"position", "anchor", "scale", "rotation", "orientation", "opacity"}, where);
            Transform transform;
            transform.position = readVector3(required(value, "position", where), "transform position");
            transform.anchor = readVector3(required(value, "anchor", where), "transform anchor");
            transform.scale = readVector3(required(value, "scale", where), "transform scale");
            transform.rotation = readVector3(required(value, "rotation", where), "transform rotation");
            transform.orientation = readQuaternion(required(value, "orientation", where), "transform orientation");
            transform.opacity = floatValue(required(value, "opacity", where), "transform opacity");
            return transform;
        }

        TimeRemap readTimeRemap(const JsonValue& value, std::string_view where) {
            rejectUnknownKeys(value, {"offsetFrames", "stretch", "reverse", "loopStart", "loopEnd", "pingPong", "freeze"},
                              where);
            TimeRemap remap;
            remap.offsetFrames = floatValue(required(value, "offsetFrames", where), "remap offset");
            remap.stretch = floatValue(required(value, "stretch", where), "remap stretch");
            remap.reverse = required(value, "reverse", where).boolean("remap reverse");
            remap.loopStart = intValue(required(value, "loopStart", where), "remap loopStart");
            remap.loopEnd = intValue(required(value, "loopEnd", where), "remap loopEnd");
            remap.pingPong = required(value, "pingPong", where).boolean("remap pingPong");
            remap.freeze = required(value, "freeze", where).boolean("remap freeze");
            return remap;
        }

        ConstraintKind readConstraintKind(const std::string& name, std::string_view where) {
            if (name == "billboard") return ConstraintKind::Billboard;
            if (name == "aim") return ConstraintKind::Aim;
            if (name == "keepDistance") return ConstraintKind::KeepDistance;
            if (name == "followPath") return ConstraintKind::FollowPath;
            if (name == "copyPosition") return ConstraintKind::CopyPosition;
            if (name == "copyRotation") return ConstraintKind::CopyRotation;
            if (name == "copyScale") return ConstraintKind::CopyScale;
            if (name == "parent") return ConstraintKind::Parent;
            if (name == "orient") return ConstraintKind::Orient;
            if (name == "point") return ConstraintKind::Point;
            if (name == "stretchTo") return ConstraintKind::StretchTo;
            if (name == "limitPosition") return ConstraintKind::LimitPosition;
            if (name == "limitRotation") return ConstraintKind::LimitRotation;
            if (name == "limitScale") return ConstraintKind::LimitScale;
            if (name == "dampedAim") return ConstraintKind::DampedAim;
            if (name == "surfaceAttach") return ConstraintKind::SurfaceAttach;
            if (name == "lookAtWithRoll") return ConstraintKind::LookAtWithRoll;
            reject(where, "names an unknown constraint kind");
        }

        ConstraintSpec readConstraint(const JsonValue& value, std::string_view where) {
            ConstraintSpec spec;
            spec.kind = readConstraintKind(required(value, "kind", where).string("constraint kind"), where);
            std::vector<std::string_view> allowed = {"kind",       "targetId", "axis",     "distance", "amount",
                                                     "amountTrack", "progress", "orientToPath", "path"};
            if (spec.kind == ConstraintKind::DampedAim) allowed.push_back("damping");
            if (spec.kind == ConstraintKind::LookAtWithRoll) allowed.push_back("roll");
            if (spec.kind == ConstraintKind::SurfaceAttach) {
                allowed.push_back("surfaceNormal");
                allowed.push_back("surfaceOffset");
            }
            if (spec.kind == ConstraintKind::StretchTo) allowed.push_back("restLength");
            if (spec.kind == ConstraintKind::LimitPosition || spec.kind == ConstraintKind::LimitRotation ||
                spec.kind == ConstraintKind::LimitScale) {
                allowed.push_back("minimum");
                allowed.push_back("maximum");
            }
            rejectUnknownKeys(value, allowed, where);
            spec.targetId = idValue(required(value, "targetId", where), "constraint target");
            spec.axis = readVector3(required(value, "axis", where), "constraint axis");
            spec.distance = floatValue(required(value, "distance", where), "constraint distance");
            spec.amount = floatValue(required(value, "amount", where), "constraint amount");
            if (const JsonValue* amount = find(value, "amountTrack")) {
                auto track = std::make_shared<Track<float>>();
                readTrack(*track, *amount, "constraint amount track");
                spec.amountTrack = std::move(track);
            }
            spec.progress = floatValue(required(value, "progress", where), "constraint progress");
            spec.orientToPath = required(value, "orientToPath", where).boolean("constraint orientToPath");
            if (spec.kind == ConstraintKind::DampedAim)
                spec.damping = floatValue(required(value, "damping", where), "dampedAim damping");
            if (spec.kind == ConstraintKind::LookAtWithRoll)
                spec.roll = floatValue(required(value, "roll", where), "aim roll");
            if (spec.kind == ConstraintKind::SurfaceAttach) {
                spec.surfaceNormal = readVector3(required(value, "surfaceNormal", where), "attach normal");
                spec.surfaceOffset = readVector3(required(value, "surfaceOffset", where), "attach offset");
            }
            if (spec.kind == ConstraintKind::StretchTo)
                spec.restLength = floatValue(required(value, "restLength", where), "stretch rest length");
            if (spec.kind == ConstraintKind::LimitPosition || spec.kind == ConstraintKind::LimitRotation ||
                spec.kind == ConstraintKind::LimitScale) {
                spec.minimum = readVector3(required(value, "minimum", where), "limit minimum");
                spec.maximum = readVector3(required(value, "maximum", where), "limit maximum");
            }
            // A path exports as a sampled polyline — a record of where the
            // motion went, not the curve that produced it. Rebuilding a curve
            // from it would invent authoring data, so it fails closed.
            const JsonValue& path = required(value, "path", where);
            if (!std::holds_alternative<std::nullptr_t>(path.value))
                reject(where, "carries a sampled path polyline, which is a record rather than authoring data");
            return spec;
        }

        DriverSpec readDriver(const JsonValue& value, std::string_view where) {
            rejectUnknownKeys(value,
                              {"kind", "property", "seed", "targetId", "axis", "center", "amplitude", "frequency",
                               "damping", "stiffness", "radius", "phase", "startTime", "endTime"},
                              where);
            DriverSpec spec;
            const std::string kind = required(value, "kind", where).string("driver kind");
            if (kind == "spring") spec.kind = DriverKind::Spring;
            else if (kind == "dampedFollow") spec.kind = DriverKind::DampedFollow;
            else if (kind == "wiggle") spec.kind = DriverKind::Wiggle;
            else if (kind == "orbit") spec.kind = DriverKind::Orbit;
            else if (kind == "float") spec.kind = DriverKind::Float;
            else if (kind == "shake") spec.kind = DriverKind::Shake;
            else if (kind == "pulse") spec.kind = DriverKind::Pulse;
            else reject(where, "has an unknown driver kind");
            const std::string property = required(value, "property", where).string("driver property");
            if (property == "position") spec.property = DriverProperty::Position;
            else if (property == "rotation") spec.property = DriverProperty::Rotation;
            else if (property == "scale") spec.property = DriverProperty::Scale;
            else if (property == "opacity") spec.property = DriverProperty::Opacity;
           
            else reject(where, "has an unknown driver property");
            spec.seed = idValue(required(value, "seed", where), "driver seed");
            spec.targetId = idValue(required(value, "targetId", where), "driver target");
            spec.axis = readVector3(required(value, "axis", where), "driver axis");
            spec.center = readVector3(required(value, "center", where), "driver center");
            spec.amplitude = floatValue(required(value, "amplitude", where), "driver amplitude");
            spec.frequency = floatValue(required(value, "frequency", where), "driver frequency");
            spec.damping = floatValue(required(value, "damping", where), "driver damping");
            spec.stiffness = floatValue(required(value, "stiffness", where), "driver stiffness");
            spec.radius = floatValue(required(value, "radius", where), "driver radius");
            spec.phase = floatValue(required(value, "phase", where), "driver phase");
            spec.startTime = floatValue(required(value, "startTime", where), "driver start");
            spec.endTime = floatValue(required(value, "endTime", where), "driver end");
            return spec;
        }

        MotionScene readScene(const JsonValue& value, std::string_view where, int depth);

        CompositionRef readPrecomp(const JsonValue& value, std::string_view where, int depth) {
            if (std::holds_alternative<std::nullptr_t>(value.value)) return CompositionRef{};
            rejectUnknownKeys(value, {"localFps", "name", "inline", "scene"}, where);
            CompositionRef ref;
            ref.localFps = floatValue(required(value, "localFps", where), "precomp fps");
            // A name-only reference carries no scene bytes to resolve: without
            // the nested document there is nothing to import, so it fails
            // instead of reappearing as an empty composition.
            if (!required(value, "inline", where).boolean("precomp inline"))
                reject(where, "is a name-only reference with no scene to resolve");
            if (depth >= 8) reject(where, "nests past the composition depth limit");
            ref.scene = std::make_shared<MotionScene>(readScene(required(value, "scene", where), "nested scene", depth + 1));
            return ref;
        }

        Layer readLayer(const JsonValue& value, std::string_view where, int depth) {
            rejectUnknownKeys(value,
                              {"id", "name", "parentId", "content", "mesh", "precomp", "lifetime", "is3D",
                               "shadow", "timeRemap", "timeRemapTrack", "transform", "tracks", "constraints",
                               "spaceSwitches", "drivers"},
                              where);
            Layer layer(idValue(required(value, "id", where), "layer id"),
                        required(value, "name", where).string("layer name"));
            layer.parentId = idValue(required(value, "parentId", where), "layer parent");
            layer.content = readContent(required(value, "content", where), "layer content");
            layer.mesh = readMesh(required(value, "mesh", where), "layer mesh");
            layer.precomp = readPrecomp(required(value, "precomp", where), "layer precomp", depth);
            const JsonValue& lifetime = required(value, "lifetime", where);
            rejectUnknownKeys(lifetime, {"inFrame", "outFrame"}, "layer lifetime");
            layer.lifetime = FrameRange{intValue(required(lifetime.object(where), "inFrame", where), "lifetime in"),
                                         intValue(required(lifetime.object(where), "outFrame", where), "lifetime out")};
            layer.is3D = required(value, "is3D", where).boolean("layer is3D");
            const JsonValue& shadow = required(value, "shadow", where);
            rejectUnknownKeys(shadow, {"casts", "accepts", "opacity", "blur"}, "layer shadow");
            layer.castsShadow = required(shadow.object(where), "casts", where).boolean("shadow casts");
            layer.acceptsShadow = required(shadow.object(where), "accepts", where).boolean("shadow accepts");
            layer.shadowOpacity = floatValue(required(shadow.object(where), "opacity", where), "shadow opacity");
            layer.shadowBlur = floatValue(required(shadow.object(where), "blur", where), "shadow blur");
            layer.timeRemap = readTimeRemap(required(value, "timeRemap", where), "layer timeRemap");
            readTrack(layer.timeRemapTrack, required(value, "timeRemapTrack", where), "layer remap track");
            layer.transform = readTransform(required(value, "transform", where), "layer transform");
            const JsonValue& tracks = required(value, "tracks", where);
            rejectUnknownKeys(tracks, {"position", "anchor", "scale", "rotation", "orientation", "opacity"},
                              "layer tracks");
            readTrack(layer.tracks.position, required(tracks.object(where), "position", where), "position track");
            readTrack(layer.tracks.anchor, required(tracks.object(where), "anchor", where), "anchor track");
            readTrack(layer.tracks.scale, required(tracks.object(where), "scale", where), "scale track");
            readTrack(layer.tracks.rotation, required(tracks.object(where), "rotation", where), "rotation track");
            readTrack(layer.tracks.orientation, required(tracks.object(where), "orientation", where),
                      "orientation track");
            readTrack(layer.tracks.opacity, required(tracks.object(where), "opacity", where), "opacity track");
            for (const auto& item : required(value, "constraints", where).array("layer constraints"))
                layer.constraints.push_back(readConstraint(item, "layer constraint"));
            if (const JsonValue* switches = find(value, "spaceSwitches")) {
                for (const auto& item : switches->array("space switches")) {
                    const auto& point = item.object("space switch");
                    rejectUnknownKeys(item, {"seconds", "parentId"}, "space switch");
                    SpaceSwitchPoint switchPoint;
                    switchPoint.seconds = floatValue(required(point, "seconds", where), "switch time");
                    switchPoint.parentId = idValue(required(point, "parentId", where), "switch parent");
                    layer.spaceSwitches.push_back(switchPoint);
                }
            }
            for (const auto& item : required(value, "drivers", where).array("layer drivers"))
                layer.drivers.push_back(readDriver(item, "layer driver"));
            return layer;
        }

        void readLightTracks(Light& light, const JsonValue& value, std::string_view where) {
            rejectUnknownKeys(value,
                              {"intensity", "color", "position", "rotation", "direction", "shadowBias",
                               "shadowNormalBias", "shadowRadius"},
                              where);
            readTrack(light.intensityTrack, required(value, "intensity", where), "light intensity");
            readTrack(light.colorTrack, required(value, "color", where), "light color");
            readTrack(light.positionTrack, required(value, "position", where), "light position");
            readTrack(light.rotationTrack, required(value, "rotation", where), "light rotation");
            readTrack(light.directionTrack, required(value, "direction", where), "light direction");
            readTrack(light.shadowBiasTrack, required(value, "shadowBias", where), "light shadow bias");
            readTrack(light.shadowNormalBiasTrack, required(value, "shadowNormalBias", where), "light normal bias");
            readTrack(light.shadowRadiusTrack, required(value, "shadowRadius", where), "light shadow radius");
        }

        void readLightCommon(Light& light, const JsonValue& value, std::string_view where) {
            light.color = readColor(required(value, "color", where), "light color");
            light.intensity = floatValue(required(value, "intensity", where), "light intensity");
            light.transform = readTransform(required(value, "transform", where), "light transform");
            light.castsShadow = required(value, "castsShadow", where).boolean("light shadow");
            light.shadowBias = floatValue(required(value, "shadowBias", where), "light bias");
            light.shadowNormalBias =
                floatValue(required(value, "shadowNormalBias", where), "light normal bias");
            light.shadowRadius = floatValue(required(value, "shadowRadius", where), "light radius");
            readLightTracks(light, required(value, "tracks", where), "light tracks");
        }

        LightKind readLightKind(const JsonValue& value, std::string_view where) {
            const std::string kind = required(value.object(where), "kind", where).string("light kind");
            if (kind == "ambient") return LightKind::Ambient;
            if (kind == "directional") return LightKind::Directional;
            if (kind == "point") return LightKind::Point;
            if (kind == "spot") return LightKind::Spot;
            reject(where, "has an unknown light kind");
        }

        void addLight(MotionScene& scene, LightKind kind, const JsonValue& value, std::string_view where) {
            const std::uint32_t id = idValue(required(value, "id", where), "light id");
            const std::string name = required(value, "name", where).string("light name");
            const std::uint32_t parent = idValue(required(value, "parentId", where), "light parent");
            if (kind == LightKind::Ambient) {
                rejectUnknownKeys(value,
                                  {"id", "name", "kind", "parentId", "color", "intensity", "transform",
                                   "castsShadow", "shadowBias", "shadowNormalBias", "shadowRadius", "tracks"},
                                  where);
                AmbientLight& light = scene.addAmbientLight(id, name, parent);
                readLightCommon(light, value, where);
                return;
            }
            if (kind == LightKind::Directional) {
                rejectUnknownKeys(value,
                                  {"id", "name", "kind", "parentId", "targetId", "color", "intensity",
                                   "transform", "castsShadow", "shadowBias", "shadowNormalBias", "shadowRadius",
                                   "tracks"},
                                  where);
                DirectionalLight& light = scene.addDirectionalLight(id, name, parent);
                light.targetId = idValue(required(value, "targetId", where), "light target");
                readLightCommon(light, value, where);
                return;
            }
            if (kind == LightKind::Point) {
                rejectUnknownKeys(value,
                                  {"id", "name", "kind", "parentId", "color", "intensity", "transform",
                                   "castsShadow", "shadowBias", "shadowNormalBias", "shadowRadius", "range",
                                   "decay", "rangeTrack", "decayTrack", "tracks"},
                                  where);
                PointLight& light = scene.addPointLight(id, name, parent);
                light.range = floatValue(required(value, "range", where), "point range");
                light.decay = floatValue(required(value, "decay", where), "point decay");
                readTrack(light.rangeTrack, required(value, "rangeTrack", where), "point range track");
                readTrack(light.decayTrack, required(value, "decayTrack", where), "point decay track");
                readLightCommon(light, value, where);
                return;
            }
            rejectUnknownKeys(value,
                              {"id", "name", "kind", "parentId", "targetId", "color", "intensity", "transform",
                               "castsShadow", "shadowBias", "shadowNormalBias", "shadowRadius", "range", "angle",
                               "penumbra", "decay", "rangeTrack", "angleTrack", "penumbraTrack", "decayTrack",
                               "tracks"},
                              where);
            SpotLight& light = scene.addSpotLight(id, name, parent);
            light.targetId = idValue(required(value, "targetId", where), "light target");
            light.range = floatValue(required(value, "range", where), "spot range");
            light.angle = floatValue(required(value, "angle", where), "spot angle");
            light.penumbra = floatValue(required(value, "penumbra", where), "spot penumbra");
            light.decay = floatValue(required(value, "decay", where), "spot decay");
            readTrack(light.rangeTrack, required(value, "rangeTrack", where), "spot range track");
            readTrack(light.angleTrack, required(value, "angleTrack", where), "spot angle track");
            readTrack(light.penumbraTrack, required(value, "penumbraTrack", where), "spot penumbra track");
            readTrack(light.decayTrack, required(value, "decayTrack", where), "spot decay track");
            readLightCommon(light, value, where);
        }

        MotionScene readScene(const JsonValue& value, std::string_view where, int depth) {
            rejectUnknownKeys(value, {"format", "version", "name", "fps", "layers", "lights"}, where);
            if (required(value, "format", where).string("scene format") != "chrononmotion.scene")
                reject(where, "is not a chrononmotion.scene document");
            if (indexValue(required(value, "version", where), "scene version") != 1)
                reject(where, "declares an unsupported version");
            MotionScene scene(required(value, "name", where).string("scene name"),
                              floatValue(required(value, "fps", where), "scene fps"));
            std::set<std::uint32_t> seenLayers;
            for (const auto& item : required(value, "layers", where).array("scene layers")) {
                Layer layer = readLayer(item, "scene layer", depth);
                // addLayer() replaces on duplicate ids; a project file that
                // says two things about one id is corrupt, not an update.
                if (!seenLayers.insert(layer.id).second) reject(where, "declares a duplicate layer id");
                scene.addLayer(std::move(layer));
            }
            std::set<std::uint32_t> seenLights;
            for (const auto& item : required(value, "lights", where).array("scene lights")) {
                const std::uint32_t id = idValue(required(item.object("scene light"), "id", where), "light id");
                if (!seenLights.insert(id).second) reject(where, "declares a duplicate light id");
                addLight(scene, readLightKind(item, "scene light"), item, "scene light");
            }
            const std::vector<std::string> problems = scene.validate();
            if (!problems.empty()) reject(where, problems.front());
            return scene;
        }

    }// namespace

    MotionScene sceneFromJson(std::string_view json) {
        const JsonValue root = JsonParser(json).parse();
        return readScene(root, "scene", 0);
    }

}// namespace chrononmotion::motion