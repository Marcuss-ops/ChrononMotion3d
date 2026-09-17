
#include <memory>

#include "chrononmotion/objects/Group.hpp"

using namespace chrononmotion;

std::string Group::type() const {

    return "Group";
}

std::shared_ptr<Group> Group::create() {

    return std::make_shared<Group>();
}

std::shared_ptr<Object3D> Group::createDefault() {

    return create();
}
