// Private JSON reader helpers shared by the scene import domains.
#pragma once
#include "chrononmotion/motion/MotionScene.hpp"
#include "chrononmotion/motion/Lighting.hpp"
#include "JsonDom.hpp"
#include <cstdint>
#include <cmath>
#include <stdexcept>
#include <string_view>
#include <variant>
#include <limits>
#include <memory>
#include <set>
#include <string>
#include <type_traits>
namespace chrononmotion::motion::scene_import_detail {
        using detail::JsonValue;
        using detail::at;
        using detail::find;
        using detail::floatValue;
        using detail::indexValue;
        using detail::JsonParser;
        using detail::rejectUnknownKeys;
        using detail::required;

        [[noreturn]] inline void reject(std::string_view where, std::string_view reason) {
            throw std::invalid_argument("scene import: " + std::string(where) + " " + std::string(reason));
        }

        // required() on an already-opened object: same missing-key failure,
        // without reopening what the caller already holds.
        inline const JsonValue& required(const JsonValue::Object& object, std::string_view key,
                                  std::string_view where) {
            const auto it = object.find(key);
            if (it == object.end())
                throw std::invalid_argument("scene import: " + std::string(where) + " is missing '" +
                                            std::string(key) + "'");
            return it->second;
        }

        inline std::uint32_t idValue(const JsonValue& value, std::string_view where) {
            const std::size_t raw = indexValue(value, where);
            if (raw > std::numeric_limits<std::uint32_t>::max())
                reject(where, "exceeds the 32-bit id range");
            return static_cast<std::uint32_t>(raw);
        }

        inline int intValue(const JsonValue& value, std::string_view where) {
            const double raw = value.number(where);
            if (std::floor(raw) != raw || raw < static_cast<double>(std::numeric_limits<int>::min()) ||
                raw > static_cast<double>(std::numeric_limits<int>::max()))
                reject(where, "must be an integer");
            return static_cast<int>(raw);
        }

        inline Vector3 readVector3(const JsonValue& value, std::string_view where) {
            const auto& items = value.array(where);
            if (items.size() != 3) reject(where, "must be a 3-element array");
            return Vector3(floatValue(items[0], where), floatValue(items[1], where), floatValue(items[2], where));
        }

        inline Quaternion readQuaternion(const JsonValue& value, std::string_view where) {
            const auto& items = value.array(where);
            if (items.size() != 4) reject(where, "must be a 4-element array");
            return Quaternion(floatValue(items[0], where), floatValue(items[1], where),
                              floatValue(items[2], where), floatValue(items[3], where));
        }

        inline Color readColor(const JsonValue& value, std::string_view where) {
            const auto& items = value.array(where);
            if (items.size() != 3) reject(where, "must be a 3-element array");
            return Color(floatValue(items[0], where), floatValue(items[1], where), floatValue(items[2], where));
        }

        inline Easing readEasing(const JsonValue& value, std::string_view where) {
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
            if (kind == "inOutCubic") return Easing::inOutCubic();
            if (kind == "bounceOut") return Easing::bounceOut();
            if (kind == "elasticOut") return Easing::elasticOut();
            if (kind == "expoOut") return Easing::expoOut();
            if (kind == "circOut") return Easing::circOut();
            // A linear segment exports as null and never reaches this branch;
            // a spelled "linear" is not an export shape.
            reject(where, "has an unknown easing kind");
        }

        inline TrackChannel readChannel(const JsonValue& value, std::string_view where) {
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
        inline float readTrackValue<float>(const JsonValue& value, std::string_view where) {
            return floatValue(value, where);
        }

        template<>
        inline Vector3 readTrackValue<Vector3>(const JsonValue& value, std::string_view where) {
            return readVector3(value, where);
        }

        template<>
        inline Quaternion readTrackValue<Quaternion>(const JsonValue& value, std::string_view where) {
            return readQuaternion(value, where);
        }

        template<>
        inline Color readTrackValue<Color>(const JsonValue& value, std::string_view where) {
            return readColor(value, where);
        }

        void readModifier(Track<float>& track, const JsonValue& value, std::string_view where);
        void readModifier(Track<Vector3>& track, const JsonValue& value, std::string_view where);
        void readModifier(Track<Quaternion>& track, const JsonValue& value, std::string_view where);
        void readModifier(Track<Color>& track, const JsonValue& value, std::string_view where);

        inline TrackModifier readModifierValue(const JsonValue& value, std::string_view where) {
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
        inline void readTrackModifiers(Track<T>& track, const JsonValue& value, std::string_view where) {
            for (const auto& item : value.array(where))
                std::visit([&track](const auto& modifier) { track.addModifier(modifier); },
                           readModifierValue(item, where));
        }

        inline void readModifier(Track<float>& track, const JsonValue& value, std::string_view where) {
            readTrackModifiers(track, value, where);
        }
        inline void readModifier(Track<Vector3>& track, const JsonValue& value, std::string_view where) {
            readTrackModifiers(track, value, where);
        }
        inline void readModifier(Track<Quaternion>& track, const JsonValue& value, std::string_view where) {
            readTrackModifiers(track, value, where);
        }
        inline void readModifier(Track<Color>& track, const JsonValue& value, std::string_view where) {
            readTrackModifiers(track, value, where);
        }

        template<class T>
        inline void readTrack(Track<T>& track, const JsonValue& value, std::string_view where) {
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


        inline Transform readTransform(const JsonValue& value, std::string_view where) {
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



MotionScene readScene(const JsonValue& value, std::string_view where, int depth);
ContentRef readContent(const JsonValue& value, std::string_view where);
Layer readLayer(const JsonValue& value, std::string_view where, int depth);
LightKind readLightKind(const JsonValue& value, std::string_view where);
void addLight(MotionScene& scene, LightKind kind, const JsonValue& value, std::string_view where);
} // namespace chrononmotion::motion::scene_import_detail
