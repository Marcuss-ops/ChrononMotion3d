#include "chrononmotion/materials/Material.hpp"

#include "chrononmotion/math/MathUtils.hpp"

using namespace chrononmotion;


Material::Material()
    : uuid_(math::generateUUID()) {}

unsigned int Material::version() const {

    return version_;
}

void Material::dispose() {
    if (!disposed_) {
        disposed_ = true;
        dispatchEvent("dispose", this);
    }
}

void Material::needsUpdate() {

    this->version_++;
}

Material::~Material() {

    dispose();
}
