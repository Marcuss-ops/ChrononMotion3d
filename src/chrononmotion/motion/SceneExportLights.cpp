#include "SceneExportInternal.hpp"

#include "chrononmotion/motion/Lighting.hpp"

namespace chrononmotion::motion::scene_export {

using namespace chrononmotion::motion;

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

} // namespace chrononmotion::motion::scene_export
