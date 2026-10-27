// Strict project-file reader; the format-specific readers live by domain.
#include "SceneImportInternal.hpp"
#include <set>
namespace chrononmotion::motion::scene_import_detail {
using detail::JsonValue;
using detail::at;
using detail::find;
using detail::indexValue;
using detail::rejectUnknownKeys;
using detail::required;
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

 } // namespace chrononmotion::motion::scene_import_detail
namespace chrononmotion::motion {
MotionScene sceneFromJson(std::string_view json) {
    const scene_import_detail::JsonValue root = detail::JsonParser(json).parse();
    return scene_import_detail::readScene(root, "scene", 0);
}
} // namespace chrononmotion::motion
