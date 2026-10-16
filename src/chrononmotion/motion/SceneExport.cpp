#include "chrononmotion/motion/SceneExport.hpp"

#include "chrononmotion/motion/MotionScene.hpp"

// The one deterministic JSON writer this tree emits through. Shared on purpose
// with the offline RenderPlan emitter: a second writer is how one document's
// number formatting would drift from the other's.
#include "JsonWriter.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace chrononmotion::motion {
    namespace {

        using detail::Writer;

        void writeVector3(Writer& writer, const Vector3& value) {
            writer.beginArray();
            writer.item();
            writer.number(value.x);
            writer.item();
            writer.number(value.y);
            writer.item();
            writer.number(value.z);
            writer.endArray();
        }

        void writeQuaternion(Writer& writer, const Quaternion& value) {
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

        void writeColor(Writer& writer, const Color& value) {
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

        void writeContent(Writer& writer, const ContentRef& content) {

            if (content.empty()) {
                writer.null();
                return;
            }

            writer.beginObject();
            writer.key("id");
            writer.text(content.id);
            writer.key("kind");
            switch (content.kind) {
                case ContentKind::None: writer.raw("\"none\""); break;
                case ContentKind::Text: writer.raw("\"text\""); break;
                case ContentKind::Image: writer.raw("\"image\""); break;
                case ContentKind::Video: writer.raw("\"video\""); break;
                case ContentKind::Mesh: writer.raw("\"mesh\""); break;
            }

            // The whole of what this tree is allowed to know: the neutral
            // rectangle, its pivot, and the digest of the content it was
            // measured from. Never the content itself.
            writer.key("naturalSize");
            writer.beginArray();
            writer.item();
            writer.number(content.metrics.naturalSize.x);
            writer.item();
            writer.number(content.metrics.naturalSize.y);
            writer.endArray();

            writer.key("anchor");
            writer.beginArray();
            writer.item();
            writer.number(content.metrics.anchor.x);
            writer.item();
            writer.number(content.metrics.anchor.y);
            writer.endArray();

            writer.key("fingerprint");
            if (content.metrics.fingerprint.empty()) {
                writer.null();
            } else {
                writer.text(content.metrics.fingerprint);
            }
            writer.endObject();
        }

        void writeMesh(Writer& writer, const MeshAttachment& mesh) {

            if (mesh.empty()) {
                writer.null();
                return;
            }

            writer.beginObject();
            writer.key("assetId");
            writer.text(mesh.assetId);
            // Presence only: a geometry handle is data this tree places and
            // transforms, and its buffers are never serialized here.
            writer.key("geometry");
            writer.boolean(mesh.geometry != nullptr);
            writer.key("material");
            writer.beginObject();
            writer.key("name");
            writer.text(mesh.material.name);
            writer.key("kind");
            switch (mesh.material.kind) {
                case MaterialKind::Unlit: writer.raw("\"unlit\""); break;
                case MaterialKind::Lambert: writer.raw("\"lambert\""); break;
                case MaterialKind::PBR: writer.raw("\"pbr\""); break;
                case MaterialKind::Emissive: writer.raw("\"emissive\""); break;
            }
            writer.key("baseColor");
            writeColor(writer, mesh.material.baseColor);
            writer.key("emissiveColor");
            writeColor(writer, mesh.material.emissiveColor);
            writer.key("emissiveStrength");
            writer.number(mesh.material.emissiveStrength);
            writer.key("roughness");
            writer.number(mesh.material.roughness);
            writer.key("metalness");
            writer.number(mesh.material.metalness);
            writer.key("opacity");
            writer.number(mesh.material.opacity);
            writer.key("transparent");
            writer.boolean(mesh.material.transparent);
            writer.endObject();
            writer.endObject();
        }

        [[nodiscard]] const char* constraintKindName(ConstraintKind kind) {

            switch (kind) {
                case ConstraintKind::Billboard: return "\"billboard\"";
                case ConstraintKind::Aim: return "\"aim\"";
                case ConstraintKind::KeepDistance: return "\"keepDistance\"";
                case ConstraintKind::FollowPath: return "\"followPath\"";
                case ConstraintKind::CopyPosition: return "\"copyPosition\"";
                case ConstraintKind::CopyRotation: return "\"copyRotation\"";
                case ConstraintKind::CopyScale: return "\"copyScale\"";
                case ConstraintKind::Parent: return "\"parent\"";
                case ConstraintKind::Orient: return "\"orient\"";
                case ConstraintKind::Point: return "\"point\"";
                case ConstraintKind::StretchTo: return "\"stretchTo\"";
                case ConstraintKind::LimitPosition: return "\"limitPosition\"";
                case ConstraintKind::LimitRotation: return "\"limitRotation\"";
                case ConstraintKind::LimitScale: return "\"limitScale\"";
                case ConstraintKind::DampedAim: return "\"dampedAim\"";
                case ConstraintKind::SurfaceAttach: return "\"surfaceAttach\"";
                case ConstraintKind::LookAtWithRoll: return "\"lookAtWithRoll\"";
            }
            return "\"aim\"";
        }

        [[nodiscard]] const char* driverKindName(DriverKind kind) {

            switch (kind) {
                case DriverKind::Spring: return "\"spring\"";
                case DriverKind::DampedFollow: return "\"dampedFollow\"";
                case DriverKind::Wiggle: return "\"wiggle\"";
                case DriverKind::Orbit: return "\"orbit\"";
                case DriverKind::Float: return "\"float\"";
                case DriverKind::Shake: return "\"shake\"";
                case DriverKind::Pulse: return "\"pulse\"";
            }
            return "\"wiggle\"";
        }

        [[nodiscard]] const char* driverPropertyName(DriverProperty property) {

            switch (property) {
                case DriverProperty::Position: return "\"position\"";
                case DriverProperty::Rotation: return "\"rotation\"";
                case DriverProperty::Scale: return "\"scale\"";
                case DriverProperty::Opacity: return "\"opacity\"";
            }
            return "\"position\"";
        }

        void writeConstraint(Writer& writer, const ConstraintSpec& constraint) {

            writer.beginObject();
            writer.key("kind");
            writer.raw(constraintKindName(constraint.kind));
            writer.key("targetId");
            writer.integer(static_cast<long long>(constraint.targetId));
            writer.key("axis");
            writeVector3(writer, constraint.axis);
            writer.key("distance");
            writer.number(constraint.distance);
            writer.key("amount");
            writer.number(constraint.amount);
            if (constraint.amountTrack) {
                writer.key("amountTrack");
                writeTrackBody(writer, *constraint.amountTrack);
            }
            writer.key("progress");
            writer.number(constraint.progress);
            writer.key("orientToPath");
            writer.boolean(constraint.orientToPath);
            if (constraint.kind == ConstraintKind::DampedAim) { writer.key("damping"); writer.number(constraint.damping); }
            if (constraint.kind == ConstraintKind::LookAtWithRoll) { writer.key("roll"); writer.number(constraint.roll); }
            if (constraint.kind == ConstraintKind::SurfaceAttach) {
                writer.key("surfaceNormal"); writeVector3(writer, constraint.surfaceNormal);
                writer.key("surfaceOffset"); writeVector3(writer, constraint.surfaceOffset);
            }
            if (constraint.kind == ConstraintKind::StretchTo) {
                writer.key("restLength"); writer.number(constraint.restLength);
            }
            if (constraint.kind == ConstraintKind::LimitPosition || constraint.kind == ConstraintKind::LimitRotation ||
                constraint.kind == ConstraintKind::LimitScale) {
                writer.key("minimum"); writeVector3(writer, constraint.minimum);
                writer.key("maximum"); writeVector3(writer, constraint.maximum);
            }

            // A path is exported as its polyline, not as curve authoring data:
            // this is a record of where the motion went, and the exact control
            // handles of a Curve3 are not part of the motion contract.
            writer.key("path");
            if (!constraint.path) {
                writer.null();
            } else {
                writer.beginObject();
                writer.key("closed");
                writer.boolean(constraint.path->closed());
                writer.key("length");
                writer.number(constraint.path->length());
                writer.key("polyline");
                writer.beginArray();
                for (const Vector3& point : constraint.path->points()) {
                    writer.item();
                    writeVector3(writer, point);
                }
                writer.endArray();
                writer.endObject();
            }
            writer.endObject();
        }

        void writeDriver(Writer& writer, const DriverSpec& driver) {

            writer.beginObject();
            writer.key("kind");
            writer.raw(driverKindName(driver.kind));
            writer.key("property");
            writer.raw(driverPropertyName(driver.property));
            writer.key("seed");
            writer.integer(static_cast<long long>(driver.seed));
            writer.key("targetId");
            writer.integer(static_cast<long long>(driver.targetId));
            writer.key("axis");
            writeVector3(writer, driver.axis);
            writer.key("center");
            writeVector3(writer, driver.center);
            writer.key("amplitude");
            writer.number(driver.amplitude);
            writer.key("frequency");
            writer.number(driver.frequency);
            writer.key("damping");
            writer.number(driver.damping);
            writer.key("stiffness");
            writer.number(driver.stiffness);
            writer.key("radius");
            writer.number(driver.radius);
            writer.key("phase");
            writer.number(driver.phase);
            writer.key("startTime");
            writer.number(driver.startTime);
            writer.key("endTime");
            writer.number(driver.endTime);
            writer.endObject();
        }

        void writeScene(Writer& writer, const MotionScene& scene,
                        std::vector<const MotionScene*>& ancestors, int depth);

        void writeLayer(Writer& writer, const Layer& layer,
                        std::vector<const MotionScene*>& ancestors, int depth) {

            writer.beginObject();
            writer.key("id");
            writer.integer(static_cast<long long>(layer.id));
            writer.key("name");
            writer.text(layer.name);
            writer.key("parentId");
            writer.integer(static_cast<long long>(layer.parentId));

            writer.key("content");
            writeContent(writer, layer.content);
            writer.key("mesh");
            writeMesh(writer, layer.mesh);

            writer.key("precomp");
            if (layer.precomp.empty()) {
                writer.null();
            } else {
                // A composition that contains itself is unrepresentable as a
                // tree, and so is one nested past the depth limit: either way
                // the export falls back to a reference instead of looping.
                const MotionScene* nested = layer.precomp.scene.get();
                const bool seen = std::find(ancestors.begin(), ancestors.end(), nested) != ancestors.end();
                const bool inlineable = nested && !seen && depth < 8;

                writer.beginObject();
                writer.key("localFps");
                writer.number(layer.precomp.localFps);
                writer.key("name");
                writer.text(nested ? nested->name() : std::string{});
                writer.key("inline");
                writer.boolean(inlineable);
                if (inlineable) {
                    writer.key("scene");
                    writeScene(writer, *nested, ancestors, depth + 1);
                }
                writer.endObject();
            }

            writer.key("lifetime");
            writer.beginObject();
            writer.key("inFrame");
            writer.integer(layer.lifetime.inFrame);
            writer.key("outFrame");
            writer.integer(layer.lifetime.outFrame);
            writer.endObject();

            writer.key("is3D");
            writer.boolean(layer.is3D);

            writer.key("shadow");
            writer.beginObject();
            writer.key("casts");
            writer.boolean(layer.castsShadow);
            writer.key("accepts");
            writer.boolean(layer.acceptsShadow);
            writer.key("opacity");
            writer.number(layer.shadowOpacity);
            writer.key("blur");
            writer.number(layer.shadowBlur);
            writer.endObject();

            writer.key("timeRemap");
            writeTimeRemap(writer, layer.timeRemap);
            writer.key("timeRemapTrack");
            writeTrackBody(writer, layer.timeRemapTrack);

            writer.key("transform");
            writeTransform(writer, layer.transform);

            writer.key("tracks");
            writer.beginObject();
            writeTrack(writer, "position", layer.tracks.position);
            writeTrack(writer, "anchor", layer.tracks.anchor);
            writeTrack(writer, "scale", layer.tracks.scale);
            writeTrack(writer, "rotation", layer.tracks.rotation);
            writeTrack(writer, "orientation", layer.tracks.orientation);
            writeTrack(writer, "opacity", layer.tracks.opacity);
            writer.endObject();

            writer.key("constraints");
            writer.beginArray();
            for (const auto& constraint : layer.constraints) {
                writer.item();
                writeConstraint(writer, constraint);
            }
            writer.endArray();

            if (!layer.spaceSwitches.empty()) {
                writer.key("spaceSwitches");
                writer.beginArray();
                for (const auto& point : layer.spaceSwitches) {
                    writer.item();
                    writer.beginObject();
                    writer.key("seconds"); writer.number(point.seconds);
                    writer.key("parentId"); writer.integer(static_cast<long long>(point.parentId));
                    writer.endObject();
                }
                writer.endArray();
            }

            writer.key("drivers");
            writer.beginArray();
            for (const auto& driver : layer.drivers) {
                writer.item();
                writeDriver(writer, driver);
            }
            writer.endArray();

            writer.endObject();
        }

        void writeLight(Writer& writer, const Light& light) {

            writer.beginObject();
            writer.key("id");
            writer.integer(static_cast<long long>(light.id));
            writer.key("name");
            writer.text(light.name);
            writer.key("kind");
            switch (light.kind()) {
                case LightKind::Ambient: writer.raw("\"ambient\""); break;
                case LightKind::Directional: writer.raw("\"directional\""); break;
                case LightKind::Point: writer.raw("\"point\""); break;
                case LightKind::Spot: writer.raw("\"spot\""); break;
            }
            writer.key("parentId");
            writer.integer(static_cast<long long>(light.parentId));

            if (const auto* directional = dynamic_cast<const DirectionalLight*>(&light)) {
                writer.key("targetId");
                writer.integer(static_cast<long long>(directional->targetId));
            } else if (const auto* spot = dynamic_cast<const SpotLight*>(&light)) {
                writer.key("targetId");
                writer.integer(static_cast<long long>(spot->targetId));
            }

            writer.key("color");
            writeColor(writer, light.color);
            writer.key("intensity");
            writer.number(light.intensity);
            writer.key("transform");
            writeTransform(writer, light.transform);

            writer.key("castsShadow");
            writer.boolean(light.castsShadow);
            writer.key("shadowBias");
            writer.number(light.shadowBias);
            writer.key("shadowNormalBias");
            writer.number(light.shadowNormalBias);
            writer.key("shadowRadius");
            writer.number(light.shadowRadius);

            if (const auto* point = dynamic_cast<const PointLight*>(&light)) {
                writer.key("range");
                writer.number(point->range);
                writer.key("decay");
                writer.number(point->decay);
                writer.key("rangeTrack");
                writeTrackBody(writer, point->rangeTrack);
                writer.key("decayTrack");
                writeTrackBody(writer, point->decayTrack);
            } else if (const auto* spot = dynamic_cast<const SpotLight*>(&light)) {
                writer.key("range");
                writer.number(spot->range);
                writer.key("angle");
                writer.number(spot->angle);
                writer.key("penumbra");
                writer.number(spot->penumbra);
                writer.key("decay");
                writer.number(spot->decay);
                writer.key("rangeTrack");
                writeTrackBody(writer, spot->rangeTrack);
                writer.key("angleTrack");
                writeTrackBody(writer, spot->angleTrack);
                writer.key("penumbraTrack");
                writeTrackBody(writer, spot->penumbraTrack);
                writer.key("decayTrack");
                writeTrackBody(writer, spot->decayTrack);
            }

            writer.key("tracks");
            writer.beginObject();
            writeTrack(writer, "intensity", light.intensityTrack);
            writeTrack(writer, "color", light.colorTrack);
            writeTrack(writer, "position", light.positionTrack);
            writeTrack(writer, "rotation", light.rotationTrack);
            writeTrack(writer, "direction", light.directionTrack);
            writeTrack(writer, "shadowBias", light.shadowBiasTrack);
            writeTrack(writer, "shadowNormalBias", light.shadowNormalBiasTrack);
            writeTrack(writer, "shadowRadius", light.shadowRadiusTrack);
            writer.endObject();

            writer.endObject();
        }

        void writeScene(Writer& writer, const MotionScene& scene,
                        std::vector<const MotionScene*>& ancestors, int depth) {

            writer.beginObject();
            writer.key("format");
            writer.raw("\"chrononmotion.scene\"");
            writer.key("version");
            writer.integer(1);
            writer.key("name");
            writer.text(scene.name());
            writer.key("fps");
            writer.number(scene.fps());

            writer.key("layers");
            writer.beginArray();
            ancestors.push_back(&scene);
            for (const auto& layer : scene.layers()) {
                writer.item();
                writeLayer(writer, layer, ancestors, depth);
            }
            ancestors.pop_back();
            writer.endArray();

            writer.key("lights");
            writer.beginArray();
            for (const auto& light : scene.lights()) {
                writer.item();
                writeLight(writer, *light);
            }
            writer.endArray();

            writer.endObject();
        }

    }// namespace

    std::string sceneToJson(const MotionScene& scene, int indentSpaces) {

        Writer writer(indentSpaces);
        std::vector<const MotionScene*> ancestors;
        writeScene(writer, scene, ancestors, 0);
        return writer.take();
    }

    std::string MotionScene::toJson(int indentSpaces) const {

        return sceneToJson(*this, indentSpaces);
    }

}// namespace chrononmotion::motion
