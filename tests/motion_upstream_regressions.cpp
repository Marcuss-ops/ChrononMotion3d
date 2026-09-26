// Adapted retained-API regression assertions from threepp at
// e2b165ece3904012d97a08029e0bd6cd5252892a. The original Catch2 assertions
// are expressed through ChrononMotion's dependency-free check harness; tests
// requiring deleted renderer/runtime APIs remain excluded (see
// docs/THREEPP_TEST_COMPATIBILITY.md).
#include "chrononmotion/animation/tracks/NumberKeyframeTrack.hpp"
#include "chrononmotion/animation/tracks/VectorKeyframeTrack.hpp"
#include "chrononmotion/core/BufferAttribute.hpp"
#include "chrononmotion/core/EventDispatcher.hpp"
#include "chrononmotion/core/Object3D.hpp"
#include "chrononmotion/math/Interpolant.hpp"
#include "chrononmotion/math/Box2.hpp"
#include "chrononmotion/math/Box3.hpp"
#include "chrononmotion/math/MathUtils.hpp"
#include "chrononmotion/math/Matrix4.hpp"
#include "chrononmotion/math/Quaternion.hpp"
#include "chrononmotion/math/Vector2.hpp"
#include "chrononmotion/math/Vector3.hpp"
#include "chrononmotion/math/Vector4.hpp"
#include "chrononmotion/math/interpolants/CubicInterpolant.hpp"
#include "chrononmotion/math/interpolants/DiscreteInterpolant.hpp"
#include "chrononmotion/math/interpolants/LinearInterpolant.hpp"
#include "chrononmotion/math/interpolants/QuaternionLinearInterpolant.hpp"
#include "motion_check.hpp"

#include <array>
#include <cmath>
#include <memory>
#include <stdexcept>
#include <vector>

using namespace chrononmotion;
using chrononmotion_test::check;
using chrononmotion_test::checkNear;
using chrononmotion_test::section;

namespace {

void vectorMath() {
    section("upstream threepp math regressions: Vector3");
    constexpr float x = 2.f, y = 3.f, z = 4.f;
    Vector3 a{x, y, z};
    const Vector3 b{-x, -y, -z};
    a.add(b);
    check(a == Vector3{}, "Vector3 addition cancels opposite vectors");
    check(Vector3{}.addVectors(b, b) == Vector3(-2.f * x, -2.f * y, -2.f * z),
          "Vector3 addVectors computes both operands");
    check(Vector3{x, y, z}.sub(b) == Vector3(2.f * x, 2.f * y, 2.f * z),
          "Vector3 subtraction preserves signed components");
    checkNear(Vector3(1.f, 0.f, 0.f).angleTo(Vector3(0.f, 1.f, 0.f)),
              math::PI * 0.5f, 1e-6f, "Vector3 angleTo returns radians");
    Vector3 clamped{-0.01f, 0.5f, 1.5f};
    clamped.clampScalar(0.1f, 1.f);
    checkNear(clamped, Vector3(0.1f, 0.5f, 1.f), 1e-6f,
              "Vector3 scalar clamp clips each component");
    const Vector3 cross = Vector3(1.f, 0.f, 0.f).cross(Vector3(0.f, 1.f, 0.f));
    check(cross == Vector3(0.f, 0.f, 1.f), "Vector3 cross product follows right-hand rule");
}

void vector2Math() {
    section("upstream threepp math regressions: Vector2");
    Vector2 vector{3.f, 4.f};
    checkNear(vector.length(), 5.f, 1e-6f, "Vector2 Euclidean length");
    vector.normalize();
    checkNear(vector, Vector2(0.6f, 0.8f), 1e-6f, "Vector2 normalization");
    checkNear(Vector2(1.f, 0.f).angleTo(Vector2(0.f, 1.f)),
              math::PI * 0.5f, 1e-6f, "Vector2 angleTo returns radians");
    checkNear(Vector2(2.f, 3.f).rotateAround(Vector2(1.f, 1.f), math::PI * 0.5f),
              Vector2(-1.f, 2.f), 1e-6f, "Vector2 rotation is relative to its supplied center");
}

void matrixMath() {
    section("upstream threepp math regressions: Matrix4");
    Matrix4 matrix;
    checkNear(matrix.determinant(), 1.f, 1e-6f, "identity determinant");
    matrix.elements[0] = 2.f;
    checkNear(matrix.determinant(), 2.f, 1e-6f, "diagonal determinant");
    matrix.set(2, 3, 4, 5, -1, -21, -3, -4, 6, 7, 8, 10, -8, -9, -10, -12);
    checkNear(matrix.determinant(), 76.f, 1e-5f, "general determinant fixture");

    Matrix4 left;
    left.makeRotationY(0.37f).setPosition(3.f, -2.f, 5.f);
    Matrix4 right;
    right.makeScale(2.f, 3.f, 4.f);
    Matrix4 product;
    product.multiplyMatrices(left, right);
    Matrix4 inverse;
    inverse.copy(product).invert();
    Matrix4 identity;
    identity.multiplyMatrices(product, inverse);
    bool inverseIsIdentity = true;
    for (std::size_t index = 0; index < identity.elements.size(); ++index) {
        const float expected = index % 5 == 0 ? 1.f : 0.f;
        inverseIsIdentity = inverseIsIdentity && std::abs(identity.elements[index] - expected) < 1e-5f;
    }
    check(inverseIsIdentity, "matrix inverse composes to identity within float tolerance");

    Matrix4 aliasedLeft(left);
    aliasedLeft.multiply(right);
    Matrix4 aliasedRight(right);
    aliasedRight.premultiply(left);
    bool aliasesMatch = true;
    for (std::size_t index = 0; index < product.elements.size(); ++index)
        aliasesMatch = aliasesMatch && aliasedLeft.elements[index] == product.elements[index] &&
                       aliasedRight.elements[index] == product.elements[index];
    check(aliasesMatch, "multiply and premultiply remain correct when destination aliases an operand");

    Matrix4 zero;
    zero.set(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    Matrix4 singular;
    singular.set(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0).invert();
    check(singular == zero, "singular inversion returns the documented zero matrix");
}

void vector4AndQuaternionMath() {
    section("upstream threepp math regressions: Vector4 and Quaternion");
    Matrix4 translation;
    translation.makeTranslation(2.f, -3.f, 4.f);
    Vector4 point{1.f, 2.f, 3.f, 1.f};
    point.applyMatrix4(translation);
    check(point == Vector4(3.f, -1.f, 7.f, 1.f), "Vector4 applies homogeneous matrix transforms");

    Quaternion quarterTurn;
    quarterTurn.setFromAxisAngle(Vector3(0.f, 0.f, 1.f), math::PI * 0.5f);
    Quaternion halfway;
    halfway.slerpQuaternions(Quaternion{}, quarterTurn, 0.5f);
    checkNear(halfway.length(), 1.f, 1e-6f, "Quaternion spherical interpolation remains normalized");
    checkNear(halfway.angleTo(Quaternion{}), math::PI * 0.25f, 1e-5f,
              "Quaternion slerp reaches the expected angular midpoint");
}

void boxMath() {
    section("upstream threepp math regressions: Box2 and Box3");
    Box2 box2;
    box2.setFromPoints(std::vector<Vector2>{{-2.f, 1.f}, {4.f, 5.f}, {0.f, -3.f}});
    check(box2.getMin() == Vector2(-2.f, -3.f) && box2.getMax() == Vector2(4.f, 5.f),
          "Box2 bounds enclose every source point");
    check(box2.containsPoint(Vector2(0.f, 0.f)) && !box2.containsPoint(Vector2(5.f, 0.f)),
          "Box2 point containment includes interior and excludes exterior points");
    Box2 other2(Vector2(3.f, 4.f), Vector2(8.f, 9.f));
    check(box2.intersectsBox(other2), "Box2 detects overlapping bounds");
    box2.intersect(other2);
    check(box2.getMin() == Vector2(3.f, 4.f) && box2.getMax() == Vector2(4.f, 5.f),
          "Box2 intersection produces the common extent");

    Box3 box3(Vector3(-1.f, -2.f, -3.f), Vector3(1.f, 2.f, 3.f));
    Matrix4 transform;
    transform.makeTranslation(4.f, 0.f, -2.f);
    box3.applyMatrix4(transform);
    check(box3.min() == Vector3(3.f, -2.f, -5.f) && box3.max() == Vector3(5.f, 2.f, 1.f),
          "Box3 transforms its axis-aligned extent");
    check(box3.containsPoint(Vector3(4.f, 0.f, 0.f)), "Box3 contains a point within transformed bounds");
}

void objectGraph() {
    section("upstream threepp core regressions: Object3D");
    auto parent = Object3D::create();
    auto child = Object3D::create();
    parent->position.set(1.f, 2.f, 3.f);
    child->position.set(4.f, 5.f, 6.f);
    parent->add(child);
    parent->updateMatrixWorld();
    checkNear(child->matrixWorld->elements[12], 5.f, 1e-6f,
              "child world X composes parent translation");
    checkNear(child->matrixWorld->elements[13], 7.f, 1e-6f,
              "child world Y composes parent translation");
    checkNear(child->matrixWorld->elements[14], 9.f, 1e-6f,
              "child world Z composes parent translation");

    Vector3 point{2.f, -1.f, 3.f};
    const Vector3 original = point;
    child->localToWorld(point);
    child->worldToLocal(point);
    checkNear(point, original, 1e-5f, "localToWorld and worldToLocal round-trip");

    auto external = Object3D::create();
    external->matrixAutoUpdate = false;
    parent->add(external);
    parent->updateMatrixWorld();
    external->matrix->makeTranslation(7.f, -4.f, 2.f);
    parent->updateMatrixWorld();
    checkNear(external->matrixWorld->elements[12], 8.f, 1e-6f,
              "externally driven local matrix propagates without a forced update");
}

void eventDispatch() {
    section("upstream threepp core regressions: EventDispatcher");
    EventDispatcher dispatcher;
    int calls = 0;
    auto subscription = dispatcher.subscribe("tick", [&calls](Event&) { ++calls; });
    dispatcher.dispatchEvent("tick");
    check(calls == 1 && subscription.active(), "subscription receives dispatched event");
    subscription.unsubscribe();
    subscription.unsubscribe();
    dispatcher.dispatchEvent("tick");
    check(calls == 1 && !subscription.active(), "unsubscribe is idempotent");

    int removedListenerCalls = 0;
    Subscription removed;
    auto remover = dispatcher.subscribe("mutation", [&removed](Event&) { removed.unsubscribe(); });
    removed = dispatcher.subscribe("mutation", [&removedListenerCalls](Event&) {
        ++removedListenerCalls;
    });
    dispatcher.dispatchEvent("mutation");
    check(removedListenerCalls == 0,
          "listener removed during dispatch is skipped rather than invoked stale");
}

void interpolants() {
    section("upstream threepp animation regressions: interpolants");
    const Sample times{0.f, 1.f, 2.f};
    const Sample values{0.f, 0.f, 0.f, 1.f, 2.f, 3.f, 2.f, 4.f, 6.f};
    Sample result;
    LinearInterpolant linear(times, values, 3, &result);
    const auto midpoint = linear.evaluate(0.5f);
    check(midpoint.size() == 3, "linear interpolant preserves vector stride");
    checkNear(midpoint[0], 0.5f, 1e-6f, "linear vector interpolation X");
    checkNear(midpoint[1], 1.f, 1e-6f, "linear vector interpolation Y");
    checkNear(midpoint[2], 1.5f, 1e-6f, "linear vector interpolation Z");
    checkNear(linear.evaluate(-10.f)[0], 0.f, 1e-6f, "linear interpolant clamps before first key");
    checkNear(linear.evaluate(99.f)[2], 6.f, 1e-6f, "linear interpolant clamps after last key");

    const Sample discreteValues{10.f, 20.f, 30.f};
    Sample discreteResult;
    DiscreteInterpolant discrete(Sample{0.f, 1.f, 2.f}, discreteValues, 1, &discreteResult);
    checkNear(discrete.evaluate(0.9f)[0], 10.f, 1e-6f, "discrete interpolation holds previous key");
    checkNear(discrete.evaluate(1.f)[0], 20.f, 1e-6f, "discrete interpolation changes at key");

    const Sample cubicTimes{0.f, 1.f, 2.f, 3.f};
    const Sample cubicValues{0.f, 1.f, 0.f, 1.f};
    Sample cubicResult;
    CubicInterpolant cubic(cubicTimes, cubicValues, 1, &cubicResult);
    for (std::size_t index = 0; index < cubicTimes.size(); ++index)
        checkNear(cubic.evaluate(cubicTimes[index])[0], cubicValues[index], 1e-4f,
                  "cubic interpolant passes through source key");

    constexpr float halfSqrt2 = 0.7071067811865475f;
    Sample quaternionResult;
    QuaternionLinearInterpolant quaternion(
        Sample{0.f, 1.f}, Sample{0.f, 0.f, 0.f, 1.f, 0.f, halfSqrt2, 0.f, halfSqrt2},
        4, &quaternionResult);
    const auto middleQuaternion = quaternion.evaluate(0.5f);
    const float norm = std::sqrt(middleQuaternion[0] * middleQuaternion[0] +
                                 middleQuaternion[1] * middleQuaternion[1] +
                                 middleQuaternion[2] * middleQuaternion[2] +
                                 middleQuaternion[3] * middleQuaternion[3]);
    checkNear(norm, 1.f, 1e-4f, "quaternion interpolation remains normalized");
    checkNear(middleQuaternion[1], std::sin(math::PI / 8.f), 1e-3f,
              "quaternion interpolant follows spherical midpoint");
}

void keyframeTracksAndAttributes() {
    section("upstream threepp animation/core regressions: tracks and attributes");
    bool emptyRejected = false;
    try { (void)NumberKeyframeTrack("empty", {}, {}); }
    catch (const std::invalid_argument&) { emptyRejected = true; }
    check(emptyRejected, "empty keyframe track is rejected eagerly");

    bool mismatchedRejected = false;
    try { (void)VectorKeyframeTrack("mismatch", {0.f, 1.f}, {1.f, 2.f, 3.f}); }
    catch (const std::invalid_argument&) { mismatchedRejected = true; }
    check(mismatchedRejected, "keyframe values must form complete samples");

    VectorKeyframeTrack track("position", {0.f, 1.f}, {0.f, 0.f, 0.f, 2.f, 4.f, 6.f},
                              Interpolation::Discrete);
    check(track.getValueSize() == 3 && track.getInterpolation() == Interpolation::Discrete,
          "vector keyframe track retains stride and requested interpolation mode");
    std::vector<float> sampled;
    auto interpolant = track.createInterpolant(&sampled);
    checkNear(interpolant->evaluate(0.5f)[1], 0.f, 1e-6f,
              "discrete vector track holds its earlier value between keys");

    auto attribute = FloatBufferAttribute::create(std::vector<float>{1.f, 2.f, 3.f, 4.f, 5.f, 6.f}, 3);
    attribute->setXYZ(1, 7.f, 8.f, 9.f);
    check(attribute->count() == 2 && attribute->getY(1) == 8.f,
          "buffer attribute vector access respects item stride");
    const auto clone = attribute->clone();
    check(clone->getX(1) == 7.f && clone->getZ(1) == 9.f,
          "buffer attribute clone preserves independent component data");
    clone->setX(1, 10.f);
    check(attribute->getX(1) == 7.f && clone->getX(1) == 10.f,
          "buffer attribute clone owns independent backing storage");
}

} // namespace

int main() {
    vectorMath();
    vector2Math();
    matrixMath();
    vector4AndQuaternionMath();
    boxMath();
    objectGraph();
    eventDispatch();
    interpolants();
    keyframeTracksAndAttributes();
    return chrononmotion_test::report();
}
