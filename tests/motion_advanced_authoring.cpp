#include "chrononmotion/chrononmotion.hpp"
#include "motion_check.hpp"

#include <array>
#include <cmath>
#include <limits>
#include <vector>

using namespace chrononmotion;
using namespace chrononmotion::motion;
using chrononmotion_test::check;
using chrononmotion_test::checkNear;
using chrononmotion_test::section;

namespace {

Vector3 worldPosition(const MotionScene& scene, Layer::Id id) {
    Vector3 position;
    position.setFromMatrixPosition(scene.worldMatrix(id));
    return position;
}

bool matrixNear(const Matrix4& a, const Matrix4& b, float tolerance) {
    for (std::size_t index = 0; index < a.elements.size(); ++index)
        if (std::fabs(a.elements[index] - b.elements[index]) > tolerance) return false;
    return true;
}

bool samePath(const BakedPath& a, const BakedPath& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (a[i].type != b[i].type || a[i].point != b[i].point ||
            a[i].control1 != b[i].control1 || a[i].control2 != b[i].control2) return false;
    }
    return true;
}

bool samePaths(const std::vector<std::vector<BakedPath>>& a,
               const std::vector<std::vector<BakedPath>>& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t frame = 0; frame < a.size(); ++frame) {
        if (a[frame].size() != b[frame].size()) return false;
        for (std::size_t path = 0; path < a[frame].size(); ++path)
            if (!samePath(a[frame][path], b[frame][path])) return false;
    }
    return true;
}

void constraintBlend() {
    section("animated constraint amount");
    MotionScene scene("constraint blend", 30.f);
    scene.addContent(1, "target", ContentRef::image("target", {1.f, 1.f})).transform.position.set(10.f, 0.f, 0.f);
    Layer& follower = scene.addContent(2, "follower", ContentRef::image("follower", {1.f, 1.f}));
    follower.tracks.position.add(0.f, Vector3{}).add(1.f, Vector3{});
    ConstraintSpec copy;
    copy.kind = ConstraintKind::CopyPosition;
    copy.targetId = 1;
    copy.amountTrack = std::make_shared<Track<float>>();
    copy.amountTrack->add(0.f, 0.f).add(1.f, 1.f);
    follower.constraints.push_back(copy);
    scene.evaluatePtsExact(0.f);
    checkNear(scene.state(2).local.position.x, 0.f, 1e-6f, "constraint begins at its unconstrained pose");
    scene.evaluatePtsExact(.5f);
    checkNear(scene.state(2).local.position.x, 5.f, 1e-6f, "constraint blends halfway without a discontinuity");
    scene.evaluatePtsExact(1.f);
    checkNear(scene.state(2).local.position.x, 10.f, 1e-6f, "constraint reaches its fully constrained endpoint");
    auto invalidTrack = std::make_shared<Track<float>>();
    invalidTrack->add(0.f, std::numeric_limits<float>::quiet_NaN());
    follower.constraints.front().amountTrack = invalidTrack;
    check(!scene.validate().empty(), "scene validation rejects non-finite animated constraint keys");
}

void falloffAndWaves() {
    section("falloff and deterministic wave deformation");
    FalloffField field;
    field.origin = {0.f, 0.f};
    field.radius = 10.f;
    field.kind = FalloffField::Kind::Linear;
    checkNear(field.evaluate({0.f, 0.f}), 1.f, 1e-6f, "falloff is one at the origin");
    checkNear(field.evaluate({5.f, 0.f}), .5f, 1e-6f, "linear falloff has the expected midpoint");
    checkNear(field.evaluate({10.f, 0.f}), 0.f, 1e-6f, "falloff is zero at and beyond its radius");
    field.innerRadius = 2.f;
    checkNear(field.evaluate({1.f, 0.f}), 1.f, 1e-6f, "inner radius forms a constant influence core");
    field.kind = FalloffField::Kind::Smooth;
    checkNear(field.evaluate({6.f, 0.f}), .5f, 1e-6f, "smooth falloff uses smoothstep between radii");
    field.kind = FalloffField::Kind::Radial;
    checkNear(field.evaluate({6.f, 0.f}), .25f, 1e-6f, "radial falloff squares its inverse distance");
    field.kind = FalloffField::Kind::Box;
    checkNear(field.evaluate({6.f, 6.f}), .5f, 1e-6f, "box falloff measures Chebyshev distance");
    field.radius = field.innerRadius;
    checkNear(field.evaluate({0.f, 0.f}), 0.f, 0.f, "invalid falloff radii fail closed");
    field.radius = 10.f;
    field.innerRadius = 0.f;
    field.kind = static_cast<FalloffField::Kind>(99);
    checkNear(field.evaluate({1.f, 0.f}), 0.f, 0.f, "unknown falloff kinds fail closed");

    const BakedPath rest = parseSvgPath("M0 0 C1 1 2 1 3 0");
    WaveDeformerSpec wave;
    wave.axis = WaveAxis::Y;
    wave.amplitude = 2.f;
    wave.wavelength = 3.f;
    wave.speed = math::PI;
    wave.phase = 0.f;
    wave.falloff = FalloffField{{1.5f, 0.f}, 10.f, 0.f, FalloffField::Kind::Linear};
    const auto baked = bakeWaveDeform(rest, wave, 0, 10, 10.f);
    const auto repeated = bakeWaveDeform(rest, wave, 0, 10, 10.f);
    check(baked.size() == 11 && samePath(baked.front(), rest), "wave bake includes rest pose and inclusive frames");
    check(samePath(baked[5], repeated[5]), "wave output repeats deterministically");
    check(std::fabs(baked[5][1].point.y - rest[1].point.y) > 0.1f,
          "wave displaces geometry inside its falloff");
    wave.falloff.radius = 0.25f;
    check(samePath(bakeWaveDeform(rest, wave, 0, 10, 10.f).back(), rest),
          "wave leaves geometry outside the falloff unchanged");
}

void fieldSimulation() {
    section("closed-form spatial fields");
    const std::array<Vector2, 3> points{{{.25f, 0.f}, {4.f, 0.f}, {0.f, 0.f}}};
    const std::array<FieldEffectorSpec, 2> fields{{
        {FieldEffectorSpec::Kind::Attractor, {0.f, 0.f}, 1.f, 2.f, .5f},
        {FieldEffectorSpec::Kind::Repulsor, {3.f, 0.f}, 1.f, 2.f, .5f}}};
    const auto frames = bakeFieldSimulation(points, fields, 0, 30, 30.f);
    const auto repeated = bakeFieldSimulation(points, fields, 0, 30, 30.f);
    check(frames.size() == 31 && frames.back() == repeated.back(), "field bake is inclusive and deterministic");
    check(frames.back()[0].x < points[0].x, "combined nearby fields move the first point net inward");
    check(frames.back()[1].x > points[1].x, "combined nearby fields move the second point net outward");
    for (const auto& frame : frames) for (const auto& point : frame)
        check(std::isfinite(point.x) && std::isfinite(point.y), "field outputs remain finite");

    FieldEffectorSpec vortex{FieldEffectorSpec::Kind::Vortex, {0.f, 0.f}, 2.f, 8.f, 1.f};
    const std::array<Vector2, 2> vortexPoints{{{4.f, 0.f}, {.25f, 0.f}}};
    const auto vortexFrames = bakeFieldSimulation(vortexPoints, vortex, 0, 30, 30.f);
    check(vortexFrames.back()[0].y > 0.f, "vortex imparts a deterministic tangential orbit");
    check(vortexFrames.back()[1].length() > vortexPoints[1].length(), "vortex expels points inside its core");

    FieldEffectorSpec box{FieldEffectorSpec::Kind::Repulsor, {0.f, 0.f}, 1.f, 10.f, .5f};
    box.falloffKind = FalloffField::Kind::Box;
    const std::array<Vector2, 1> diagonalPoint{{{6.f, 6.f}}};
    const auto boxFalloff = bakeFieldSimulation(diagonalPoint, box, 0, 30, 30.f);
    box.falloffKind = FalloffField::Kind::Linear;
    const auto radialFalloff = bakeFieldSimulation(diagonalPoint, box, 0, 30, 30.f);
    check(boxFalloff.back()[0] != radialFalloff.back()[0],
          "field effectors share FalloffField's selectable box-versus-radial distance semantics");
    box.falloffKind = FalloffField::Kind::Smooth;
    box.innerFalloffRadius = 2.f;
    const auto plateau = bakeFieldSimulation(vortexPoints, box, 0, 30, 30.f);
    check(plateau.back()[1] != vortexPoints[1],
          "field effectors honor an inner full-strength falloff plateau");
    box.innerFalloffRadius = box.falloffRadius;
    bool rejectedFalloff = false;
    try { (void)bakeFieldSimulation(vortexPoints, box, 0, 1, 30.f); }
    catch (const std::invalid_argument&) { rejectedFalloff = true; }
    check(rejectedFalloff, "invalid field falloff settings fail closed");
}

void skinAndPins() {
    section("SVG skinning and pin deformation");
    const BakedPath rest = parseSvgPath("M0 0 C1 0 2 0 3 0");
    const std::array<SvgBone, 1> bones{{{{0.f, 0.f}, {3.f, 0.f}, -1}}};
    std::vector<std::vector<SvgSkinBinding>> bindings(rest.size() * 3, {{0, 1.f}});
    std::vector<std::vector<Matrix4>> matrices(2, std::vector<Matrix4>(1));
    matrices[1][0].makeTranslation(0.f, 2.f, 0.f);
    const auto skin = bakeSvgSkinDeform(rest, bones, bindings, matrices, 0, 1);
    check(skin.size() == 2 && samePath(skin[0][0], rest), "skinning preserves its bind pose at identity");
    checkNear(skin[1][0][1].point.y, 2.f, 1e-6f, "skinning transforms control points by the weighted bone matrix");

    const std::array<PathPin, 1> pins{{{0.f, {0.f, 0.f}, 1.f, {0.f, 1.f}}}};
    const auto pinned = bakePathPinDeform(rest, pins, .2f, 0, 8);
    check(pinned.size() == 9, "pin baker emits every inclusive frame");
    check(pinned.front()[0][0].point == rest[0].point, "pinned endpoint remains fixed");
    check(pinned[2][0][1].point.y > pinned[0][0][1].point.y,
          "pin influence transfers animated displacement to nearby path controls");
    const auto pinnedAgain = bakePathPinDeform(rest, pins, .2f, 0, 8);
    check(samePaths(pinned, pinnedAgain), "pin deformation is bit-repeatable");
}

void collectionSelector() {
    section("deterministic collection selector");
    MotionScene scene("collection selector", 30.f);
    scene.addNull(10, "group");
    Layer& first = scene.addContent(11, "card_primary", ContentRef::image("first", {2.f, 1.f}), 10);
    first.alive(2, 4).flat();
    Layer& second = scene.addContent(12, "card_secondary", ContentRef::text("second", {1.f, 1.f}), 10);
    second.alive(0, 5);
    scene.addContent(13, "other_card", ContentRef::image("third", {1.f, 1.f}));
    scene.addMesh(14, "mesh_asset", MeshAttachment{.assetId = "mesh"});

    CollectionSelector selector;
    selector.namePrefix = "card_";
    selector.parentId = 10;
    selector.visibleAtFrame = 3;
    const auto cards = selectCollection(scene, selector);
    check(cards.size() == 2 && cards[0]->id == 11 && cards[1]->id == 12,
          "intersected parent, prefix, and lifetime criteria retain scene insertion order");

    selector.contentKind = ContentKind::Image;
    selector.is3D = false;
    const auto flatImages = selectCollection(scene, selector);
    check(flatImages.size() == 1 && flatImages.front()->id == 11,
          "content kind and dimensionality refine the collection query");

    selector = {};
    selector.layerKind = CollectionSelector::LayerKind::Null;
    check(selectCollection(scene, selector).size() == 1 &&
          selectCollection(scene, selector).front()->id == 10,
          "null-layer selection distinguishes controller layers from content");
    selector.layerKind = CollectionSelector::LayerKind::Mesh;
    check(selectCollection(scene, selector).size() == 1 &&
          selectCollection(scene, selector).front()->id == 14,
          "mesh-layer selection uses the existing opaque mesh boundary");

    selector = {};
    selector.includeIds = {11, 12, 13};
    selector.excludeIds = {12};
    const auto explicitIds = selectCollection(scene, selector);
    check(explicitIds.size() == 2 && explicitIds[0]->id == 11 && explicitIds[1]->id == 13,
          "explicit IDs form an allowlist and exclusions take precedence");

    const auto rejects = [&](CollectionSelector invalid) {
        try {
            (void)selectCollection(scene, invalid);
        } catch (const std::invalid_argument&) {
            return true;
        }
        return false;
    };
    selector = {};
    selector.includeIds = {11, 11};
    check(rejects(selector), "duplicate selector IDs are rejected rather than ambiguously deduplicated");
    selector = {};
    selector.visibleAtFrame = -1;
    check(rejects(selector), "negative selector frame is rejected");
    selector = {};
    selector.layerKind = static_cast<CollectionSelector::LayerKind>(99);
    check(rejects(selector), "unknown selector enums fail closed");
}

void cloner() {
    section("deterministic scene cloner");
    MotionScene scene("cloner", 30.f);
    Layer& prototype = scene.addContent(10, "prototype", ContentRef::image("prototype", {2.f, 2.f}));
    prototype.transform.position.set(1.f, 0.f, 0.f);
    prototype.tracks.position.add(0.f, {1.f, 0.f, 0.f}).add(.5f, {2.f, 0.f, 0.f});
    ClonerSpec spec;
    spec.count = 200;
    spec.pattern = ClonerSpec::Pattern::Spiral;
    spec.spiralRadiusStep = .1f;
    spec.spiralAngleStepRad = .25f;
    spec.timeOffsetSeconds = .02f;
    const auto clones = instantiateCloner(scene, prototype, spec, "clone_");
    check(clones.size() == 200 && scene.layerCount() == 201, "cloner instantiates the requested scene layers");
    check(clones[0]->id == 1 && clones[1]->id == 2 && clones[9]->id == 11 && clones.back()->id == 201,
          "scene allocator assigns stable unused IDs in insertion order while skipping occupied IDs");
    checkNear(clones[1]->transform.position, Vector3(1.f + .1f * std::cos(.25f),
             .1f * std::sin(.25f), 0.f), 1e-5f, "spiral copy pose matches its index");
    checkNear(clones[1]->tracks.position.startTime(), .02f, 1e-6f, "copy reveal keys are staggered by time offset");
    check(clones[0]->id != prototype.id, "the source is not overwritten by clone allocation");

    MotionScene transactional("transactional cloner", 30.f);
    Layer& unstable = transactional.addContent(1, "unstable", ContentRef::image("unstable", {1.f, 1.f}));
    ClonerSpec overflowing;
    overflowing.count = 3;
    overflowing.scaleStep = {std::numeric_limits<float>::max(), 1.f, 1.f};
    const std::size_t layerCountBefore = transactional.layerCount();
    bool rejectedOverflow = false;
    try { (void)instantiateCloner(transactional, unstable, overflowing, "overflow_"); }
    catch (const std::invalid_argument&) { rejectedOverflow = true; }
    check(rejectedOverflow && transactional.layerCount() == layerCountBefore,
          "cloner validates the complete batch before mutating the scene");
}

void spaceSwitching() {
    section("deterministic multi-space switching");
    MotionScene scene("space switch", 30.f);
    Layer& torso = scene.addNull(1, "torso");
    torso.tracks.position.add(0.f, {0.f, 0.f, 0.f}).add(1.f, {10.f, 0.f, 0.f});
    Layer& object = scene.addNull(2, "object");
    object.tracks.position.add(0.f, {0.f, 0.f, 0.f}).add(1.f, {0.f, 10.f, 0.f});
    Layer& hand = scene.addContent(3, "hand", ContentRef::image("hand", {1.f, 1.f}));
    hand.transform.position.set(2.f, 0.f, 0.f);
    hand.spaceSwitches = {{.5f, 1}, {.75f, 2}};
    scene.evaluatePtsExact(.49f);
    const Matrix4 beforeFirst = scene.worldMatrix(3);
    check(hand.parent == nullptr, "before the first handoff the graph remains in root space");
    scene.evaluatePtsExact(.5f);
    const Matrix4 atFirst = scene.worldMatrix(3);
    checkNear(worldPosition(scene, 3), Vector3(2.f, 0.f, 0.f), 1e-4f, "first handoff preserves world position");
    check(matrixNear(beforeFirst, atFirst, .011f), "first handoff preserves the complete pose matrix");
    check(hand.parent == &torso, "evaluated graph parent follows the active torso space");
    scene.addNull(4, "forces order rebuild after torso handoff");
    (void)scene.evaluationOrder();
    check(hand.parent == &torso, "an order rebuild preserves the last evaluated graph parent");

    scene.evaluatePtsExact(.749f);
    const Matrix4 beforeSecond = scene.worldMatrix(3);
    scene.evaluatePtsExact(.75f);
    const Matrix4 atSecond = scene.worldMatrix(3);
    checkNear(worldPosition(scene, 3), Vector3(4.5f, 0.f, 0.f), 2e-2f,
              "second handoff accounts for torso motion through the exact switch instant");
    check(matrixNear(beforeSecond, atSecond, .011f), "second handoff preserves the complete pose matrix");
    check(hand.parent == &object, "evaluated graph parent follows the active object space");
    scene.addNull(5, "forces order rebuild after object handoff");
    (void)scene.evaluationOrder();
    check(hand.parent == &object, "an order rebuild preserves the last evaluated object parent");

    scene.evaluatePtsExact(1.f);
    checkNear(worldPosition(scene, 3), Vector3(4.5f, 2.5f, 0.f), 1e-4f,
              "switched hand follows its final parent using the maintained offset");
    const Matrix4 expected = scene.worldMatrix(3);
    scene.evaluatePtsExact(.6f);
    check(hand.parent == &torso, "random access restores the torso as the active graph parent");
    scene.evaluatePtsExact(1.f);
    check(scene.worldMatrix(3) == expected, "space switching is random-access deterministic");
    check(hand.parent == &object, "random access restores the final active graph parent");

    MotionScene affine("affine multi-space handoff", 30.f);
    Layer& affineTorso = affine.addNull(1, "rotated non-uniform torso");
    affineTorso.transform.position.set(3.f, -2.f, 1.f);
    affineTorso.transform.rotation.z = .7f;
    affineTorso.transform.scale.set(1.5f, .75f, 1.2f);
    Layer& affineObject = affine.addNull(2, "animated affine object");
    affineObject.transform.position.set(-4.f, 5.f, 2.f);
    affineObject.transform.rotation.z = -.35f;
    affineObject.transform.scale.set(.8f, 1.3f, 1.1f);
    affineObject.tracks.position.add(0.f, {-4.f, 5.f, 2.f}).add(1.f, {2.f, 8.f, 2.f});
    affineObject.tracks.rotation.add(0.f, {0.f, 0.f, -.35f}).add(1.f, {0.f, 0.f, .4f});
    Layer& affineHand = affine.addContent(3, "affine hand", ContentRef::image("hand", {1.f, 1.f}));
    affineHand.transform.position.set(2.f, -1.f, .5f);
    affineHand.transform.rotation.z = .2f;
    affineHand.transform.scale.set(1.1f, .9f, 1.f);
    affineHand.transform.anchor.set(.25f, -.5f, 0.f);
    affineHand.spaceSwitches = {{.25f, 1}, {.5f, 2}};
    affine.evaluatePtsExact(0.f);
    const Matrix4 rootPose = affine.worldMatrix(3);
    affine.evaluatePtsExact(.25f);
    check(matrixNear(affine.worldMatrix(3), rootPose, 2e-5f),
          "root-to-rotated-scaled torso handoff preserves all affine pose terms");
    const Matrix4 torsoHandoffPose = affine.worldMatrix(3);
    affine.evaluatePtsExact(.5f);
    check(matrixNear(affine.worldMatrix(3), torsoHandoffPose, 2e-5f),
          "torso-to-object handoff composes non-commuting offsets in the correct order");
    const Matrix4 objectAtHandoff = affine.worldMatrix(2);
    Matrix4 inverseObjectAtHandoff = objectAtHandoff;
    inverseObjectAtHandoff.invert();
    Matrix4 maintainedLocal;
    maintainedLocal.multiplyMatrices(inverseObjectAtHandoff, affine.worldMatrix(3));
    affine.evaluatePtsExact(.75f);
    Matrix4 expectedFollowing;
    expectedFollowing.multiplyMatrices(affine.worldMatrix(2), maintainedLocal);
    check(matrixNear(affine.worldMatrix(3), expectedFollowing, 2e-5f),
          "after handoff the affine object carries the maintained local pose while it moves");
    const Matrix4 affineExpected = affine.worldMatrix(3);
    affine.evaluatePtsExact(.1f);
    affine.evaluatePtsExact(.75f);
    check(affine.worldMatrix(3) == affineExpected,
          "affine space-switch evaluation is deterministic under arbitrary seek order");

    MotionScene reordered("switch before its parent", 30.f);
    Layer& earlyHand = reordered.addContent(1, "hand", ContentRef::image("hand", {1.f, 1.f}));
    earlyHand.transform.position.set(2.f, 0.f, 0.f);
    earlyHand.spaceSwitches = {{.5f, 2}};
    Layer& laterParent = reordered.addNull(2, "later parent");
    laterParent.tracks.position.add(0.f, {}).add(1.f, {10.f, 0.f, 0.f});
    reordered.evaluatePtsExact(.5f);
    checkNear(worldPosition(reordered, 1), Vector3(2.f, 0.f, 0.f), 1e-5f,
              "evaluation orders a switched-to layer before its switching child");

    MotionScene invalid("invalid switch", 30.f);
    invalid.addNull(1, "parent");
    invalid.addNull(2, "child");
    Layer& child = *invalid.findLayer(2);
    child.spaceSwitches = {{.5f, 99}};
    check(!invalid.validate().empty(), "validation reports unknown switched-to parents");
    Layer& cycleParent = invalid.addNull(3, "cycle parent");
    cycleParent.parentId = 2;
    cycleParent.spaceSwitches = {{.25f, 2}};
    child.parentId = 3;
    child.spaceSwitches = {{.5f, 3}};
    check(!invalid.validate().empty(), "evaluation dependencies include switched-to spaces and detect cycles");
}

} // namespace

int main() {
    constraintBlend();
    falloffAndWaves();
    fieldSimulation();
    skinAndPins();
    collectionSelector();
    cloner();
    spaceSwitching();
    return chrononmotion_test::report();
}
