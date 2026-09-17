// https://github.com/mrdoob/three.js/blob/r129/src/scenes/Scene.js
//
// ChrononMotion: a Scene is the root of a node tree, nothing more. Upstream's
// background (Color/Texture/CubeTexture), environment, Fog/FogExp2 variant and
// overrideMaterial all describe how a renderer composites, so they live with
// the consumer that composites.

#ifndef THREEPP_SCENE_HPP
#define THREEPP_SCENE_HPP

#include "threepp/core/Object3D.hpp"

#include <memory>

namespace threepp {

    class Scene: public Object3D {

    public:
        [[nodiscard]] std::string type() const override;

        static std::shared_ptr<Scene> create();

        ~Scene() override = default;

    protected:
        std::shared_ptr<Object3D> createDefault() override;
    };

}// namespace threepp

#endif//THREEPP_SCENE_HPP
