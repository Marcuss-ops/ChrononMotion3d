#include "chrononmotion/motion/SceneExport.hpp"

#include "chrononmotion/motion/MotionScene.hpp"
#include "SceneExportInternal.hpp"

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

        using namespace scene_export;

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
