// ChrononMotion — strict project-file reader for the SceneExport format.
//
// `sceneToJson` is a record of what the motion engine decided; this is the way
// back in. It accepts exactly what the export writes — `chrononmotion.scene`
// version 1 — and nothing else: unknown format/version, unknown fields,
// duplicate keys, missing required fields and malformed values fail closed
// with a std::invalid_argument naming the field, never half-accepted.
//
// Three export shapes are records, not authoring data, and are rejected
// rather than approximated:
//
//   * a constraint carrying a `path` (the export stores a sampled polyline,
//     not the curve that produced it; rebuilding authoring data from the
//     record would invent semantics);
//   * a `precomp` name-only reference (`inline: false` carries no scene bytes
//     to resolve);
//   * a `mesh` with `geometry: true` (vertex buffers are never serialized).
//
// Everything else round-trips: export → import → re-export is byte-identical,
// and both scenes evaluate to identical world state. That is the project-file
// contract, pinned by `chrononmotion_scene_import`.

#ifndef CHRONONMOTION_MOTION_SCENEIMPORT_HPP
#define CHRONONMOTION_MOTION_SCENEIMPORT_HPP

#include <string>
#include <string_view>

namespace chrononmotion::motion {

    class MotionScene;

    /// Read a `chrononmotion.scene` v1 document produced by `sceneToJson`.
    ///
    /// Strict: `format` must be `"chrononmotion.scene"`, `version` must be 1,
    /// every object carries exactly its documented fields, layer ids are
    /// unique, and the assembled scene passes `MotionScene::validate()`.
    /// Anything else throws std::invalid_argument. The input budget matches
    /// the other readers (16 MiB, 128 nesting levels, 8 precomp levels).
    [[nodiscard]] MotionScene sceneFromJson(std::string_view json);

}// namespace chrononmotion::motion

#endif//CHRONONMOTION_MOTION_SCENEIMPORT_HPP
