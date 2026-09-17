#include "chrononmotion/objects/Mesh.hpp"

#include <memory>
#include <utility>


using namespace chrononmotion;


Mesh::Mesh(std::shared_ptr<BufferGeometry> geometry, std::shared_ptr<Material> material)
    : ObjectWithMaterials(material ? std::vector<std::shared_ptr<Material>>{std::move(material)}
                                   : std::vector<std::shared_ptr<Material>>{}),
      geometry_(geometry ? std::move(geometry) : BufferGeometry::create()) {}

Mesh::Mesh(std::shared_ptr<BufferGeometry> geometry, std::vector<std::shared_ptr<Material>> materials)
    : ObjectWithMaterials(std::move(materials)), geometry_(std::move(geometry)) {}

std::string Mesh::type() const {

    return "Mesh";
}

std::shared_ptr<BufferGeometry> Mesh::geometry() const {

    return geometry_;
}

void Mesh::setGeometry(const std::shared_ptr<BufferGeometry>& geometry) {

    geometry_ = geometry;
}

std::shared_ptr<Mesh> Mesh::create(std::shared_ptr<BufferGeometry> geometry, std::shared_ptr<Material> material) {

    return std::make_shared<Mesh>(std::move(geometry), std::move(material));
}

std::shared_ptr<Mesh> Mesh::create(std::shared_ptr<BufferGeometry> geometry, std::vector<std::shared_ptr<Material>> materials) {

    return std::make_shared<Mesh>(std::move(geometry), std::move(materials));
}

void Mesh::copy(const Object3D& source, bool recursive) {
    Object3D::copy(source, recursive);

    if (const auto m = source.as<Mesh>()) {

        materials_ = m->materials_;
        geometry_ = m->geometry_;
    }
}

std::shared_ptr<Object3D> Mesh::createDefault() {
    return create();
}
