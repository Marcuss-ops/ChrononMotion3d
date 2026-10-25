#include "SceneExportInternal.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace chrononmotion::motion {
    namespace scene_export {

        [[nodiscard]] const char* channelName(TrackChannel channel) {

            switch (channel) {
                case TrackChannel::Position: return "\"position\"";
                case TrackChannel::Anchor: return "\"anchor\"";
                case TrackChannel::Scale: return "\"scale\"";
                case TrackChannel::Rotation: return "\"rotation\"";
                case TrackChannel::Orientation: return "\"orientation\"";
                case TrackChannel::Opacity: return "\"opacity\"";
                case TrackChannel::TimeRemap: return "\"timeRemap\"";
            }
            return "\"opacity\"";
        }

        void writeEasing(Writer& writer, const Easing& easing) {

            // A linear segment is the default shape, so it exports as `null`
            // rather than as an object saying nothing.
            if (easing.isLinear()) {
                writer.null();
                return;
            }

            writer.beginObject();
            writer.key("kind");
            switch (easing.kind()) {
                case Easing::Kind::Linear: writer.raw("\"linear\""); break;
                case Easing::Kind::CubicBezier: writer.raw("\"bezier\""); break;
                case Easing::Kind::CubicInOut: writer.raw("\"inOutCubic\""); break;
                case Easing::Kind::BounceOut: writer.raw("\"bounceOut\""); break;
                case Easing::Kind::ElasticOut: writer.raw("\"elasticOut\""); break;
                case Easing::Kind::ExpoOut: writer.raw("\"expoOut\""); break;
                case Easing::Kind::CircOut: writer.raw("\"circOut\""); break;
                case Easing::Kind::Steps: writer.raw("\"steps\""); break;
            }

            if (easing.kind() == Easing::Kind::Steps) {
                writer.key("count");
                writer.integer(easing.stepCount());
            } else if (easing.kind() == Easing::Kind::CubicBezier) {
                writer.key("controlPoints");
                writer.beginArray();
                writer.item();
                writer.number(easing.x1());
                writer.item();
                writer.number(easing.y1());
                writer.item();
                writer.number(easing.x2());
                writer.item();
                writer.number(easing.y2());
                writer.endArray();
            }
            writer.endObject();
        }

        void writeModifier(Writer& writer, const TrackModifier& modifier) {

            std::visit(
                    [&writer](const auto& value) {
                        using M = std::decay_t<decltype(value)>;
                        writer.beginObject();
                        if constexpr (std::is_same_v<M, LoopModifier>) {
                            writer.key("kind");
                            writer.raw("\"loop\"");
                            writer.key("start");
                            writer.number(value.start);
                            writer.key("end");
                            writer.number(value.end);
                            writer.key("mode");
                            writer.raw(value.mode == LoopMode::PingPong ? "\"pingPong\"" : "\"repeat\"");
                        } else if constexpr (std::is_same_v<M, PosterizeModifier>) {
                            writer.key("kind");
                            writer.raw("\"posterize\"");
                            writer.key("samplesPerSecond");
                            writer.number(value.samplesPerSecond);
                        } else if constexpr (std::is_same_v<M, DelayModifier>) {
                            writer.key("kind");
                            writer.raw("\"delay\"");
                            writer.key("seconds");
                            writer.number(value.seconds);
                        } else if constexpr (std::is_same_v<M, LinkModifier>) {
                            writer.key("kind");
                            writer.raw("\"link\"");
                            writer.key("layerId");
                            writer.integer(static_cast<long long>(value.layerId));
                            writer.key("channel");
                            writer.raw(channelName(value.channel));
                            writer.key("originTime");
                            writer.number(value.originTime);
                        } else if constexpr (std::is_same_v<M, WindowModifier>) {
                            writer.key("kind");
                            writer.raw("\"window\"");
                            writer.key("start");
                            writer.number(value.start);
                            writer.key("end");
                            writer.number(value.end);
                        } else if constexpr (std::is_same_v<M, InfluenceModifier>) {
                            writer.key("kind");
                            writer.raw("\"influence\"");
                            writer.key("amount");
                            writer.number(value.amount);
                            writer.key("originTime");
                            writer.number(value.originTime);
                        } else if constexpr (std::is_same_v<M, SmoothModifier>) {
                            writer.key("kind");
                            writer.raw("\"smooth\"");
                            writer.key("radius");
                            writer.number(value.radius);
                            writer.key("samples");
                            writer.integer(static_cast<long long>(value.samples));
                        }
                        writer.endObject();
                    },
                    modifier);
        }

        // writeTrackBody/writeTrack: template inline in SceneExportInternal.hpp

        void writeTransform(Writer& writer, const Transform& transform) {
            writer.beginObject();
            writer.key("position");
            writeVector3(writer, transform.position);
            writer.key("anchor");
            writeVector3(writer, transform.anchor);
            writer.key("scale");
            writeVector3(writer, transform.scale);
            writer.key("rotation");
            writeVector3(writer, transform.rotation);
            writer.key("orientation");
            writeQuaternion(writer, transform.orientation);
            writer.key("opacity");
            writer.number(transform.opacity);
            writer.endObject();
        }

        void writeTimeRemap(Writer& writer, const TimeRemap& remap) {

            writer.beginObject();
            writer.key("offsetFrames");
            writer.number(remap.offsetFrames);
            writer.key("stretch");
            writer.number(remap.stretch);
            writer.key("reverse");
            writer.boolean(remap.reverse);
            writer.key("loopStart");
            writer.integer(remap.loopStart);
            writer.key("loopEnd");
            writer.integer(remap.loopEnd);
            writer.key("pingPong");
            writer.boolean(remap.pingPong);
            writer.key("freeze");
            writer.boolean(remap.freeze);
            writer.endObject();
        }
    } // namespace scene_export
} // namespace chrononmotion::motion
