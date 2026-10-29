#ifndef CHRONONMOTION_ASSETS_GLTF_READER_HPP
#define CHRONONMOTION_ASSETS_GLTF_READER_HPP

#include "chrononmotion/objects/Mesh.hpp"
#include "chrononmotion/scenes/Scene.hpp"
#include "chrononmotion/motion/RigBaking.hpp"

#include <cstddef>
#include <algorithm>
#include <functional>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace chrononmotion {

    /// The resolver is called only for external buffer URIs. ChrononMotion does
    /// no filesystem or network I/O; the caller owns URI policy and byte loading.
    using GltfExternalBufferResolver =
        std::function<std::vector<std::byte>(std::string_view uri)>;

    struct GltfReadPolicy {
        bool enabled{true};
        bool allowStaticGltfImport{true};
        bool allowMotionGltfImport{true};
        std::size_t maxJsonBytes{16u * 1024u * 1024u};
        std::size_t maxBufferBytes{256u * 1024u * 1024u};
        std::size_t maxTotalBufferBytes{512u * 1024u * 1024u};
        std::size_t maxNodes{100'000};
        std::size_t maxMeshes{100'000};
        std::size_t maxPrimitives{100'000};
        std::size_t maxVertices{8'000'000};
        std::function<void(bool motion, std::string_view format)> audit{};
    };

    struct GltfAsset {
        std::shared_ptr<Scene> scene;
        // All primitives in the selected glTF scene, in deterministic node,
        // mesh, primitive order. Meshes are also attached under their node groups.
        std::vector<std::shared_ptr<Mesh>> meshes;
    };

    struct GltfMotionClip {
        std::string name{};
        std::vector<motion::RigAnimatedNode> animation{};
        float duration{0.f};
        struct MorphChannel { std::uint32_t geometryId{0}; std::vector<motion::Track<float>> weights{}; };
        std::vector<MorphChannel> morphs{};
    };
    struct GltfMotionSkin {
        std::vector<motion::RigNodeId> joints{};
        std::vector<Matrix4> inverseBindMatrices{};
        motion::RigNodeId skeleton{0};
    };
    struct GltfMotionAsset {
        motion::RigGraph graph{};
        std::vector<GltfMotionSkin> skins{};
        std::vector<motion::RigGeometryBinding> geometry{};
        std::vector<GltfMotionClip> clips{};
    };

    /// Named runtime authority over imported glTF clips. It builds one strict
    /// RigProgram evaluator per clip and exposes deterministic frame sampling;
    /// no clip is selected implicitly and no Scene animation state is mutated.
    class GltfMotionClipLibrary {
    public:
        explicit GltfMotionClipLibrary(const GltfMotionAsset& asset) {
            entries_.reserve(asset.clips.size());
            for (std::size_t index = 0; index < asset.clips.size(); ++index) {
                const auto& clip = asset.clips[index];
                std::string name = clip.name.empty() ? "clip_" + std::to_string(index) : clip.name;
                if (findEntry(name) != entries_.end())
                    throw std::invalid_argument("GltfMotionClipLibrary: duplicate clip name");
                motion::RigProgram program;
                program.graph = asset.graph;
                program.animation = clip.animation;
                program.geometry = asset.geometry;
                for (const auto& channel : clip.morphs) {
                    if (channel.geometryId == 0 || channel.geometryId > program.geometry.size())
                        throw std::invalid_argument("GltfMotionClipLibrary: morph channel geometry id is invalid");
                    auto& geometry = program.geometry[channel.geometryId - 1];
                    if (geometry.id != channel.geometryId || channel.weights.size() != geometry.morphs.size())
                        throw std::invalid_argument("GltfMotionClipLibrary: morph channel topology mismatch");
                    geometry.morphTracks = channel.weights;
                }
                Entry entry;
                entry.name = std::move(name);
                entry.duration = clip.duration;
                entry.evaluator = std::make_unique<motion::RigProgramEvaluator>(program);
                entries_.push_back(std::move(entry));
            }
        }

        [[nodiscard]] std::vector<std::string> clipNames() const {
            std::vector<std::string> result;
            result.reserve(entries_.size());
            for (const auto& entry : entries_) result.push_back(entry.name);
            return result;
        }

        [[nodiscard]] float duration(std::string_view name) const { return lookup(name).duration; }

        [[nodiscard]] motion::RigProgramFrame sample(std::string_view name, int frame, float fps) const {
            return lookup(name).evaluator->evaluate(frame, fps);
        }

    private:
        struct Entry {
            std::string name;
            float duration{0.f};
            std::unique_ptr<motion::RigProgramEvaluator> evaluator;
        };
        using Iterator = std::vector<Entry>::const_iterator;
        [[nodiscard]] Iterator findEntry(std::string_view name) const {
            return std::find_if(entries_.begin(), entries_.end(),
                [name](const Entry& entry) { return entry.name == name; });
        }
        [[nodiscard]] const Entry& lookup(std::string_view name) const {
            const auto it = findEntry(name);
            if (it == entries_.end()) throw std::out_of_range("GltfMotionClipLibrary: unknown clip name");
            return *it;
        }
        std::vector<Entry> entries_;
    };
    namespace motion {
        // chrononmotion.rig.program.v1: strict authoring round trip for the
        // static rig program (graph, controllers/drivers, poses, geometry with
        // deformers). Schedules (animation/spaces/IK/weights/secondary) and import-
        // derived handles are deliberately rejected: they are re-derived at authoring
        // time from their own authorities, not stored here.
        [[nodiscard]] std::string serializeRigProgram(const RigProgram& program);
        [[nodiscard]] RigProgram deserializeRigProgram(std::string_view json);
    } // namespace motion

    // Explicit motion-only import. The original static reader remains strict.
    // Narrow TRS hierarchy, triangle POSITION, skins and linear/step/cubic clips;
    // no materials, extensions, compression, sparse or implicit I/O.
    [[nodiscard]] GltfMotionAsset loadGltfMotion(
        std::string_view json, const GltfExternalBufferResolver& resolver = {},
        const GltfReadPolicy& policy = {});

    /// Load static glTF 2.0 JSON. Buffers may be embedded base64 data URIs or
    /// supplied by the explicit resolver. Materials, animations, skins,
    /// morph targets, extensions, and non-triangle primitives fail closed.
    [[nodiscard]] GltfAsset loadGltf(
        std::string_view json,
        const GltfExternalBufferResolver& resolveExternalBuffer = {},
        const GltfReadPolicy& policy = {});

    /// Load a GLB 2.0 container. JSON and BIN chunks are parsed in memory;
    /// external buffer URIs still require the explicit caller resolver.
    [[nodiscard]] GltfAsset loadGlb(
        std::span<const std::byte> bytes,
        const GltfExternalBufferResolver& resolveExternalBuffer = {},
        const GltfReadPolicy& policy = {});

} // namespace chrononmotion

#endif // CHRONONMOTION_ASSETS_GLTF_READER_HPP
