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
                case ContentKind::Particles: writer.raw("\"particles\""); break;
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
            writer.key("baseColorTextureAsset");
            writer.text(mesh.baseColorTextureAsset);
            writer.key("metallicRoughnessTextureAsset");
            writer.text(mesh.metallicRoughnessTextureAsset);
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
            writer.key("doubleSided");
            writer.boolean(mesh.material.doubleSided);
            writer.key("alphaCutoff");
            writer.number(mesh.material.alphaCutoff);
            // Physical extension: omitted while neutral so existing scene
            // documents keep their exact bytes.
            if (mesh.material.hasPhysicalProperties()) {
                writer.key("physical");
                writer.beginObject();
                writer.key("transmission");
                writer.number(mesh.material.transmission);
                writer.key("ior");
                writer.number(mesh.material.ior);
                writer.key("thickness");
                writer.number(mesh.material.thickness);
                writer.key("dispersion");
                writer.number(mesh.material.dispersion);
                writer.key("clearcoat");
                writer.number(mesh.material.clearcoat);
                writer.endObject();
            }
            writer.endObject();
            if (mesh.instances) {
                mesh.instances->validate();
                writer.key("instances");
                writer.beginObject();
                writer.key("localTransforms");
                writer.beginArray();
                for (const Matrix4& local : mesh.instances->localTransforms) {
                    writer.item();
                    writer.beginArray();
                    for (const float value : local.elements) {
                        writer.item();
                        writer.number(value);
                    }
                    writer.endArray();
                }
                writer.endArray();
                writer.endObject();
            }
            writer.endObject();
        }

        void writeParticles(Writer& writer, const ParticleEmitter& particles) {

            writer.beginObject();
            writer.key("count");
            writer.unsignedInteger(particles.count);
            writer.key("seed");
            writer.unsignedInteger(particles.seed);
            writer.key("emitterSize");
            writeVector2(writer, particles.emitterSize);
            writer.key("velocityMin");
            writeVector2(writer, particles.velocityMin);
            writer.key("velocityMax");
            writeVector2(writer, particles.velocityMax);
            writer.key("lifetime");
            writer.beginArray();
            writer.item();
            writer.number(particles.lifetimeMin);
            writer.item();
            writer.number(particles.lifetimeMax);
            writer.endArray();
            writer.key("size");
            writer.beginArray();
            writer.item();
            writer.number(particles.sizeMin);
            writer.item();
            writer.number(particles.sizeMax);
            writer.endArray();
            writer.key("gravity");
            writer.number(particles.gravity);
            writer.key("turbulence");
            writer.number(particles.turbulence);
            writer.key("shape");
            switch (particles.shape) {
                case ParticleEmitterShape::Box: writer.raw("\"box\""); break;
                case ParticleEmitterShape::Ellipse: writer.raw("\"ellipse\""); break;
                case ParticleEmitterShape::Edge: writer.raw("\"edge\""); break;
                case ParticleEmitterShape::Path: writer.raw("\"path\""); break;
            }
            writer.key("pathId");
            writer.text(particles.pathId);
            writer.key("pathPoints");
            writer.beginArray();
            for (const auto point : particles.pathPoints) {
                writer.item();
                writeVector2(writer, point);
            }
            writer.endArray();
            writer.key("spriteAsset");
            writer.text(particles.spriteAsset);
            writer.key("spriteScale");
            writer.beginArray();
            writer.item();
            writer.number(particles.spriteScaleMin);
            writer.item();
            writer.number(particles.spriteScaleMax);
            writer.endArray();
            writer.key("rotation");
            writer.beginArray();
            writer.item();
            writer.number(particles.rotationMin);
            writer.item();
            writer.number(particles.rotationMax);
            writer.endArray();
            writer.key("angularVelocity");
            writer.beginArray();
            writer.item();
            writer.number(particles.angularVelocityMin);
            writer.item();
            writer.number(particles.angularVelocityMax);
            writer.endArray();
            writer.key("blendMode");
            switch (particles.blendMode) {
                case ParticleBlendMode::Normal: writer.raw("\"normal\""); break;
                case ParticleBlendMode::Add: writer.raw("\"add\""); break;
                case ParticleBlendMode::Screen: writer.raw("\"screen\""); break;
            }
            writer.key("environment");
            writer.boolean(particles.environment);
            writer.key("depthRange");
            writer.beginArray();
            writer.item();
            writer.number(particles.depthMin);
            writer.item();
            writer.number(particles.depthMax);
            writer.endArray();
            writer.key("bokeh");
            writer.boolean(particles.bokeh);
            writer.key("colorRamp");
            writer.beginArray();
            for (const auto& stop : particles.colorRamp) {
                writer.item();
                writer.beginObject();
                writer.key("position");
                writer.number(stop.position);
                writer.key("color");
                writer.beginArray();
                writer.item();
                writer.number(stop.r);
                writer.item();
                writer.number(stop.g);
                writer.item();
                writer.number(stop.b);
                writer.item();
                writer.number(stop.a);
                writer.endArray();
                writer.endObject();
            }
            writer.endArray();
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
    } // namespace scene_export
} // namespace chrononmotion::motion
