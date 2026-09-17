#include "threepp/scenes/Scene.hpp"

using namespace threepp;


std::string Scene::type() const {

    return "Scene";
}

std::shared_ptr<Scene> Scene::create() {

    return std::make_shared<Scene>();
}

std::shared_ptr<Object3D> Scene::createDefault() {

    return Scene::create();
}
