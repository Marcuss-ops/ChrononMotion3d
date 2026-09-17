// https://github.com/mrdoob/three.js/blob/r129/src/materials/Material.js
//
// ChrononMotion: upstream Material is the base of ~40 concrete materials plus
// capability mixins, all of it renderer/GLTF-facing. What survives here is the
// part a node can actually be asked about: identity, a version counter for
// change detection, visibility and the alpha/side fields a motion engine
// lowers. Blending, stencil, clipping, uniforms, textures and the whole
// MaterialValue/setValues/copyCompatibleFrom surface are gone.

#ifndef THREEPP_MATERIAL_HPP
#define THREEPP_MATERIAL_HPP

#include "threepp/constants.hpp"
#include "threepp/core/EventDispatcher.hpp"

#include <atomic>
#include <memory>
#include <string>

namespace threepp {

    class Material: public EventDispatcher {

    public:
        // Atomic — see Object3D::id. Identity is read across the graph.
        const unsigned int id = materialId.fetch_add(1, std::memory_order_relaxed);

        std::string name;

        Side side{Side::Front};

        float opacity = 1;

        bool transparent = false;

        bool visible = true;

        // Not drawn below this alpha. Chroma/coverage handling belongs to the
        // consumer that rasterizes, not here.
        float alphaTest = 0;

        Material(Material&&) = delete;
        Material& operator=(Material&&) = delete;
        Material(const Material&) = delete;
        Material& operator=(const Material&) = delete;

        // Stable identity for a material instance, generated once. Consumers use
        // it as a cache/fingerprint key; nothing here parses it.
        [[nodiscard]] const std::string& uuid() const { return uuid_; }

        [[nodiscard]] unsigned int version() const;

        void needsUpdate();

        void dispose();

        [[nodiscard]] virtual std::string type() const = 0;

        template<class T>
            requires std::derived_from<T, Material>
        T* as() {

            return dynamic_cast<T*>(this);
        }

        template<class T>
            requires std::derived_from<T, Material>
        [[nodiscard]] bool is() const {

            return dynamic_cast<const T*>(this) != nullptr;
        }

        ~Material() override;

    protected:
        Material();

    private:
        bool disposed_ = false;
        const std::string uuid_;
        unsigned int version_ = 0;
        inline static std::atomic<unsigned int> materialId{0};
    };


}// namespace threepp

#endif//THREEPP_MATERIAL_HPP
