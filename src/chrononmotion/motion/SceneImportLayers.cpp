#include "SceneImportInternal.hpp"
namespace chrononmotion::motion::scene_import_detail {
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
                                      "metalness", "opacity", "transparent", "doubleSided", "alphaCutoff"},
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
            if (const auto* twoSided = find(value, "doubleSided"))
                material.doubleSided = twoSided->boolean("material doubleSided");
            if (const auto* cutoff = find(value, "alphaCutoff"))
                material.alphaCutoff = floatValue(*cutoff, "material alphaCutoff");
            material.validate();
            return material;
        }

        MeshAttachment readMesh(const JsonValue& value, std::string_view where) {
            if (std::holds_alternative<std::nullptr_t>(value.value)) return MeshAttachment{};
            rejectUnknownKeys(value, {"assetId", "baseColorTextureAsset", "metallicRoughnessTextureAsset", "geometry", "material"}, where);
            // Vertex buffers are never serialized: a mesh that carried geometry
            // cannot be rebuilt from the record, so it fails instead of
            // reappearing with silently different pixels.
            if (required(value, "geometry", where).boolean("mesh geometry"))
                reject(where, "carries geometry whose buffers were never serialized");
            MeshAttachment mesh;
            mesh.assetId = required(value, "assetId", where).string("mesh asset");
            if (const auto* texture = find(value, "baseColorTextureAsset"))
                mesh.baseColorTextureAsset = texture->string("mesh base color texture");
            if (const auto* texture = find(value, "metallicRoughnessTextureAsset"))
                mesh.metallicRoughnessTextureAsset = texture->string("mesh metallic-roughness texture");
            mesh.material = readMaterial(required(value, "material", where), "mesh material");
            return mesh;
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

} // namespace chrononmotion::motion::scene_import_detail
