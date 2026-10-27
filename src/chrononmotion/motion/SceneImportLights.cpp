#include "SceneImportInternal.hpp"
namespace chrononmotion::motion::scene_import_detail {
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

} // namespace chrononmotion::motion::scene_import_detail
