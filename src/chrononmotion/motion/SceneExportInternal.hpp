#pragma once

#include "JsonWriter.hpp"

#include "chrononmotion/motion/MotionScene.hpp"

#include <type_traits>

namespace chrononmotion::motion {
    namespace scene_export {

        using detail::Writer;

        void writeEasing(Writer& writer, const Easing& easing);
        void writeModifier(Writer& writer, const TrackModifier& modifier);
        void writeTimeRemap(Writer& writer, const TimeRemap& remap);
        void writeTransform(Writer& writer, const Transform& transform);
        void writeContent(Writer& writer, const ContentRef& content);
        void writeMesh(Writer& writer, const MeshAttachment& mesh);
        void writeConstraint(Writer& writer, const ConstraintSpec& constraint);
        void writeDriver(Writer& writer, const DriverSpec& driver);
        inline void writeVector3(Writer& writer, const Vector3& value) {
            writer.beginArray();
            writer.item();
            writer.number(value.x);
            writer.item();
            writer.number(value.y);
            writer.item();
            writer.number(value.z);
            writer.endArray();
        }

        inline void writeQuaternion(Writer& writer, const Quaternion& value) {
            writer.beginArray();
            writer.item();
            writer.number(value.x);
            writer.item();
            writer.number(value.y);
            writer.item();
            writer.number(value.z);
            writer.item();
            writer.number(value.w);
            writer.endArray();
        }

        inline void writeColor(Writer& writer, const Color& value) {
            writer.beginArray();
            writer.item();
            writer.number(value.r);
            writer.item();
            writer.number(value.g);
            writer.item();
            writer.number(value.b);
            writer.endArray();
        }

        template<class T>
        void writeValue(Writer& writer, const T& value) {

            if constexpr (std::is_same_v<T, float>) {
                writer.number(value);
            } else if constexpr (std::is_same_v<T, Vector3>) {
                writeVector3(writer, value);
            } else if constexpr (std::is_same_v<T, Quaternion>) {
                writeQuaternion(writer, value);
            } else if constexpr (std::is_same_v<T, Color>) {
                writeColor(writer, value);
            } else {
                writer.null();
            }
        }


        template<class T>
        void writeTrackBody(Writer& writer, const Track<T>& track) {

            writer.beginObject();

            writer.key("keys");
            writer.beginArray();
            for (const auto& keyframe : track.keys()) {
                writer.item();
                writer.beginObject();
                writer.key("time");
                writer.number(keyframe.time);
                writer.key("value");
                writeValue(writer, keyframe.value);
                writer.key("easing");
                writeEasing(writer, keyframe.easing);
                writer.key("discrete");
                writer.boolean(keyframe.discrete);
                writer.endObject();
            }
            writer.endArray();

            writer.key("modifiers");
            writer.beginArray();
            for (const auto& modifier : track.modifiers()) {
                writer.item();
                writeModifier(writer, modifier);
            }
            writer.endArray();

            writer.endObject();
        }

        template<class T>
        void writeTrack(Writer& writer, const char* key, const Track<T>& track) {

            writer.key(key);
            writeTrackBody(writer, track);
        }


    } // namespace scene_export
} // namespace chrononmotion::motion
