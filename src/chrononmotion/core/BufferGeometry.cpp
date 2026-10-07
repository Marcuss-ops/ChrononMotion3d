
#include "chrononmotion/core/BufferGeometry.hpp"

#include "chrononmotion/math/MathUtils.hpp"
#include "chrononmotion/math/Matrix3.hpp"
#include "chrononmotion/math/Matrix4.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <utility>

using namespace chrononmotion;

namespace {

    std::unique_ptr<BufferAttribute> convertBufferAttribute(BufferAttribute& _attribute, const std::vector<unsigned int>& indices) {

        if (_attribute.typed<float>()) {

            auto attribute = _attribute.typed<float>();

            const auto& array = attribute->array();
            const auto itemSize = attribute->itemSize();
            const auto normalized = attribute->normalized();

            auto array2 = std::vector<float>(indices.size() * itemSize);

            unsigned index = 0, index2 = 0;

            for (unsigned i = 0, l = indices.size(); i < l; i++) {

                index = indices[i] * itemSize;

                for (auto j = 0; j < itemSize; j++) {

                    array2[index2++] = array[index++];
                }
            }

            return FloatBufferAttribute::create(array2, itemSize, normalized);
        }

        throw std::invalid_argument("BufferGeometry::toNonIndexed: only float attributes are supported");
    }

}// namespace

BufferGeometry::BufferGeometry()
    : uuid(math::generateUUID()) {}

std::string BufferGeometry::type() const {

    return "BufferGeometry";
}

bool BufferGeometry::hasIndex() const {

    return index_ != nullptr;
}

IntBufferAttribute* BufferGeometry::getIndex() {

    if (!index_) return nullptr;

    return this->index_.get();
}

const IntBufferAttribute* BufferGeometry::getIndex() const {

    if (!index_) return nullptr;

    return this->index_.get();
}

BufferAttribute* BufferGeometry::getAttribute(const std::string& name) {

    if (!hasAttribute(name)) return nullptr;

    return attributes_.at(name).get();
}

const BufferAttribute* BufferGeometry::getAttribute(const std::string& name) const {

    if (!hasAttribute(name)) return nullptr;

    return attributes_.at(name).get();
}

const std::unordered_map<std::string, std::shared_ptr<BufferAttribute>>& BufferGeometry::getAttributes() const {

    return attributes_;
}

void BufferGeometry::setAttribute(const std::string& name, std::shared_ptr<BufferAttribute> attribute) {

    checkMutable();
    attributes_[name] = std::move(attribute);
    ++attributesVersion_;
}

void BufferGeometry::deleteAttribute(const std::string& name) {

    checkMutable();
    if (attributes_.count(name)) {

        attributes_.erase(name);
        ++attributesVersion_;
    }
}

bool BufferGeometry::hasAttribute(const std::string& name) const {

    return attributes_.contains(name);
}

void BufferGeometry::addGroup(int start, int count, unsigned int materialIndex) {

    checkMutable();
    groups.emplace_back(GeometryGroup{start, count, materialIndex});
}

void BufferGeometry::clearGroups() {

    checkMutable();
    groups.clear();
}

BufferGeometry& BufferGeometry::applyMatrix4(const Matrix4& matrix) {

    checkMutable();
    if (hasAttribute("position")) {

        auto position = dynamic_cast<TypedBufferAttribute<float>*>(this->attributes_.at("position").get());

        position->applyMatrix4(matrix);

        position->needsUpdate();
    }


    if (hasAttribute("normal")) {

        auto normal = dynamic_cast<TypedBufferAttribute<float>*>(this->attributes_.at("normal").get());

        auto normalMatrix = Matrix3().getNormalMatrix(matrix);

        normal->applyNormalMatrix(normalMatrix);

        normal->needsUpdate();
    }


    if (hasAttribute("tangent")) {

        auto tangent = dynamic_cast<TypedBufferAttribute<float>*>(this->attributes_.at("tangent").get());

        tangent->transformDirection(matrix);

        tangent->needsUpdate();
    }

    // Unconditionally, where three.js recomputes only what was already
    // computed. The old test here was INVERTED — compute only when absent —
    // which left every cached box exactly where the geometry used to be:
    // center() computes the box to find the offset, translates, and handed
    // out an outline (and a raycast gate) sitting beside the vertices.
    this->computeBoundingBox();
    this->computeBoundingSphere();

    return *this;
}

BufferGeometry& BufferGeometry::applyQuaternion(const Quaternion& q) {

    checkMutable();
    Matrix4 _m1{};
    _m1.makeRotationFromQuaternion(q);

    this->applyMatrix4(_m1);

    return *this;
}

BufferGeometry& BufferGeometry::rotateX(float angle) {

    checkMutable();
    // rotate geometry around world x-axis

    Matrix4 _m1{};
    _m1.makeRotationX(angle);

    this->applyMatrix4(_m1);

    return *this;
}

BufferGeometry& BufferGeometry::rotateY(float angle) {

    checkMutable();
    // rotate geometry around world y-axis

    Matrix4 _m1{};
    _m1.makeRotationY(angle);

    this->applyMatrix4(_m1);

    return *this;
}

BufferGeometry& BufferGeometry::rotateZ(float angle) {

    checkMutable();
    // rotate geometry around world z-axis

    Matrix4 _m1{};
    _m1.makeRotationZ(angle);

    this->applyMatrix4(_m1);

    return *this;
}

BufferGeometry& BufferGeometry::translate(float x, float y, float z) {

    checkMutable();
    // translate geometry

    Matrix4 _m1{};
    _m1.makeTranslation(x, y, z);

    this->applyMatrix4(_m1);

    return *this;
}

BufferGeometry& BufferGeometry::scale(float x, float y, float z) {

    checkMutable();
    // scale geometry

    Matrix4 _m1{};
    _m1.makeScale(x, y, z);

    this->applyMatrix4(_m1);

    return *this;
}

BufferGeometry& BufferGeometry::center() {

    checkMutable();
    this->computeBoundingBox();

    Vector3 _offset{};
    this->boundingBox->getCenter(_offset);
    _offset.negate();

    this->translate(_offset.x, _offset.y, _offset.z);

    return *this;
}

BufferGeometry& BufferGeometry::setFromPoints(const std::vector<Vector2>& points) {

    checkMutable();
    std::vector<float> position(points.size() * 3);

    for (unsigned i = 0, j = 0, l = points.size(); i < l; i++) {

        const auto& point = points[i];
        position[j++] = point.x;
        position[j++] = point.y;
        position[j++] = 0;
    }

    this->setAttribute("position", FloatBufferAttribute::create(position, 3));

    return *this;
}

BufferGeometry& BufferGeometry::setFromPoints(const std::vector<Vector3>& points) {

    checkMutable();
    std::vector<float> position(points.size() * 3);

    for (unsigned i = 0, j = 0, l = points.size(); i < l; i++) {

        const auto& point = points[i];
        position[j++] = point.x;
        position[j++] = point.y;
        position[j++] = point.z;
    }

    this->setAttribute("position", FloatBufferAttribute::create(position, 3));

    return *this;
}

void BufferGeometry::computeBoundingBox() {

    if (!this->boundingBox) {

        this->boundingBox = Box3();
    }

    if (const auto position = this->getAttribute<float>("position")) {

        position->setFromBufferAttribute(*this->boundingBox);

    } else {

        this->boundingBox->makeEmpty();
    }

    if (this->boundingBox->min().isNan()) {

        std::cerr << "THREE.BufferGeometry.computeBoundingBox(): Computed min/max have NaN values. The 'position' attribute is likely to have NaN values." << std::endl;
    }
}

void BufferGeometry::computeBoundingSphere() {

    if (!this->boundingSphere) {

        this->boundingSphere = Sphere();
    }

    Vector3 _vector;

    if (const auto position = this->getAttribute<float>("position")) {

        // first, find the center of the bounding sphere

        auto& center = this->boundingSphere->center;

        Box3 _box;
        position->setFromBufferAttribute(_box);

        _box.getCenter(center);

        // second, try to find a boundingSphere with a radius smaller than the
        // boundingSphere of the boundingBox: sqrt(3) smaller in the best case

        float maxRadiusSq = 0;

        for (unsigned i = 0, il = position->count(); i < il; i++) {

            position->setFromBufferAttribute(_vector, i);

            maxRadiusSq = std::max(maxRadiusSq, center.distanceToSquared(_vector));
        }

        this->boundingSphere->radius = std::sqrt(maxRadiusSq);

        if (std::isnan(this->boundingSphere->radius)) {

            std::cerr << "THREE.BufferGeometry.computeBoundingSphere(): Computed radius is NaN. The 'position' attribute is likely to have NaN values." << std::endl;
        }
    }
}

void BufferGeometry::normalizeNormals() {

    checkMutable();
    auto normals = getAttribute<float>("normal");
    if (!normals) {
        throw std::invalid_argument("BufferGeometry::normalizeNormals: missing float normal attribute");
    }

    Vector3 _vector{};
    for (int i = 0, il = normals->count(); i < il; i++) {

        normals->setFromBufferAttribute(_vector, i);

        _vector.normalize();

        normals->setXYZ(i, _vector.x, _vector.y, _vector.z);
    }
}

void BufferGeometry::copy(const BufferGeometry& source) {
    checkMutable();
    // reset

    this->index_ = nullptr;
    this->attributes_.clear();
    this->groups.clear();
    this->boundingBox = std::nullopt;
    this->boundingSphere = std::nullopt;

    // name

    this->name = source.name;

    // index

    auto& index = source.index_;

    if (index) {

        this->index_ = index->clone();
    }

    // attributes

    auto& attributes = source.attributes_;

    for (const auto& [name, attribute] : attributes) {

        // cloneUntyped() dispatches on the attribute's own scalar type, so this
        // copies narrow (uint8/int8/uint16/int16) attributes without widening
        // them — and without needing a branch per supported type.
        this->setAttribute(name, attribute->cloneUntyped());
    }


    // groups

    for (auto& group : source.groups) {

        this->addGroup(group.start, group.count, group.materialIndex);
    }

    // bounding box

    auto& boundingBox = source.boundingBox;

    if (boundingBox) {

        this->boundingBox = boundingBox;
    }

    // bounding sphere

    auto& boundingSphere = source.boundingSphere;

    if (boundingSphere) {

        this->boundingSphere = boundingSphere;
    }

}

std::shared_ptr<BufferGeometry> BufferGeometry::toNonIndexed() const {

    if (!this->hasIndex()) {

        std::cerr << "THREE.BufferGeometry.toNonIndexed(): BufferGeometry is already non-indexed." << std::endl;
        return nullptr;
    }

    auto geometry2 = BufferGeometry::create();

    const auto& indices = this->index_->array();
    const auto& attributes = this->attributes_;

    // attributes

    for (const auto& [name, attribute] : attributes) {

        auto newAttribute = convertBufferAttribute(*attribute, indices);

        geometry2->setAttribute(name, std::move(newAttribute));
    }

    // groups

    const auto& groups = this->groups;

    for (unsigned i = 0, l = groups.size(); i < l; i++) {

        const auto group = groups[i];
        geometry2->addGroup(group.start, group.count, group.materialIndex);
    }

    return geometry2;
}


void BufferGeometry::computeVertexNormals() {

    checkMutable();
    auto index = getIndex();

    const auto positionAttribute = this->getAttribute<float>("position");

    if (positionAttribute) {

        auto normalAttribute = this->getAttribute<float>("normal");

        if (!normalAttribute) {

            this->setAttribute("normal", FloatBufferAttribute::create(std::vector<float>(positionAttribute->count() * 3), 3));
            normalAttribute = this->getAttribute<float>("normal");

        } else {

            // reset existing normals to zero

            for (unsigned i = 0, il = normalAttribute->count(); i < il; i++) {

                normalAttribute->setXYZ(i, 0, 0, 0);
            }
        }

        Vector3 pA, pB, pC;
        Vector3 nA, nB, nC;
        Vector3 cb, ab;

        // indexed elements

        if (index) {

            for (unsigned i = 0, il = index->count(); i < il; i += 3) {

                auto vA = index->getX(i + 0);
                auto vB = index->getX(i + 1);
                auto vC = index->getX(i + 2);

                positionAttribute->setFromBufferAttribute(pA, vA);
                positionAttribute->setFromBufferAttribute(pB, vB);
                positionAttribute->setFromBufferAttribute(pC, vC);

                cb.subVectors(pC, pB);
                ab.subVectors(pA, pB);
                cb.cross(ab);

                normalAttribute->setFromBufferAttribute(nA, vA);
                normalAttribute->setFromBufferAttribute(nB, vB);
                normalAttribute->setFromBufferAttribute(nC, vC);

                nA.add(cb);
                nB.add(cb);
                nC.add(cb);

                normalAttribute->setXYZ(vA, nA.x, nA.y, nA.z);
                normalAttribute->setXYZ(vB, nB.x, nB.y, nB.z);
                normalAttribute->setXYZ(vC, nC.x, nC.y, nC.z);
            }

        } else {

            // non-indexed elements (unconnected triangle soup)
            // il is rounded down to a multiple of 3 — a trailing partial
            // triangle (e.g. a non-triangle-mode glTF primitive misread as
            // a triangle list) would otherwise read 1-2 vertices past the
            // end of positionAttribute.

            for (unsigned i = 0, il = positionAttribute->count() - positionAttribute->count() % 3; i < il; i += 3) {

                positionAttribute->setFromBufferAttribute(pA, i + 0);
                positionAttribute->setFromBufferAttribute(pB, i + 1);
                positionAttribute->setFromBufferAttribute(pC, i + 2);

                cb.subVectors(pC, pB);
                ab.subVectors(pA, pB);
                cb.cross(ab);

                normalAttribute->setXYZ(i + 0, cb.x, cb.y, cb.z);
                normalAttribute->setXYZ(i + 1, cb.x, cb.y, cb.z);
                normalAttribute->setXYZ(i + 2, cb.x, cb.y, cb.z);
            }
        }

        this->normalizeNormals();

        normalAttribute->needsUpdate();
    }
}

std::shared_ptr<BufferGeometry> BufferGeometry::clone() const {
    auto g = std::make_shared<BufferGeometry>();
    g->copy(*this);

    return g;
}

void BufferGeometry::dispose() {

    if (!disposed_) {
        disposed_ = true;
        this->dispatchEvent("dispose", this);
    }
}

BufferGeometry::~BufferGeometry() {
    dispose();
}

std::shared_ptr<BufferGeometry> BufferGeometry::create() {

    return std::make_shared<BufferGeometry>();
}
