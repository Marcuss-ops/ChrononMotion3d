// ChrononMotion — scene export.
//
// A deterministic, dependency-free JSON export of a scene's **space and time**.
//
// What crosses this boundary is the motion model and nothing else: layer
// identity, hierarchy, lifetime, transforms, keyed tracks and their modifiers,
// time remap, constraints, procedural drivers, light state and camera-lens
// numbers. Content never does. A `ContentRef` is exported as exactly the four
// things this tree is allowed to know about it — id, kind, the neutral
// rectangle and the opaque measurement fingerprint — and a mesh is exported as
// an asset id plus the presence of a geometry handle. No vertex buffer, no
// pixel, no decoded frame, no font, no glyph ever appears in the output.
//
// The export is one-way on purpose: there is no importer, so the format is a
// record of what the motion engine decided, not a second authoring front door.

#ifndef CHRONONMOTION_MOTION_SCENEEXPORT_HPP
#define CHRONONMOTION_MOTION_SCENEEXPORT_HPP

#include <string>

namespace chrononmotion::motion {

    class MotionScene;

    /// Export `scene` as JSON.
    ///
    /// The output is byte-for-byte reproducible: object keys are written in a
    /// fixed order, arrays follow authoring order, and every number is the
    /// shortest decimal that round-trips back to the same `float` (with `-0`
    /// normalized to `0`, and a non-finite value written as `null`, since JSON
    /// has no representation for it).
    ///
    /// `indentSpaces` is the indentation per nesting level; 0 produces a single
    /// line. Nested compositions are exported inline, with a depth limit and a
    /// recursion guard: a composition that (transitively) contains itself falls
    /// back to a name-only reference instead of looping.
    [[nodiscard]] std::string sceneToJson(const MotionScene& scene, int indentSpaces = 2);

}// namespace chrononmotion::motion

#endif//CHRONONMOTION_MOTION_SCENEEXPORT_HPP
