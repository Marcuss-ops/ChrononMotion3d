
#include "chrononmotion/cameras/Camera.hpp"

using namespace chrononmotion;

Camera::Camera(float _near, float _far)
    : nearPlane(_near), farPlane(_far) {}

void Camera::getWorldDirection(Vector3& target) {

    Object3D::getWorldDirection(target);
    target.negate();
}

void Camera::updateMatrixWorld(bool force) {

    Object3D::updateMatrixWorld(force);

    this->matrixWorldInverse.copy(*this->matrixWorld).invert();
}

void Camera::updateWorldMatrix(bool updateParents, bool updateChildren) {

    Object3D::updateWorldMatrix(updateParents, updateChildren);

    this->matrixWorldInverse.copy(*this->matrixWorld).invert();
}

// Adapted from threepp commit 61d02da6c5c00e7718216bdd89f98d437aab3c99 (MIT).
void Camera::copy(const Object3D& source, bool recursive) {
    Object3D::copy(source, recursive);
    if (const auto* camera = source.as<Camera>()) {
        zoom = camera->zoom;
        nearPlane = camera->nearPlane;
        farPlane = camera->farPlane;
        view = camera->view;
        matrixWorldInverse.copy(camera->matrixWorldInverse);
        projectionMatrix.copy(camera->projectionMatrix);
        projectionMatrixInverse.copy(camera->projectionMatrixInverse);
    }
}

std::shared_ptr<Object3D> Camera::createDefault() {
    return std::make_shared<Camera>();
}
