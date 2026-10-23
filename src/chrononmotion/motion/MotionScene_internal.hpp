#pragma once

#include "chrononmotion/motion/MotionScene.hpp"

#include <cstddef>

namespace chrononmotion::motion::scene_detail {

enum class LinkType { Vector3Type, QuaternionType, ScalarType };

[[nodiscard]] LinkType linkTypeOf(TrackChannel channel);
[[nodiscard]] std::size_t saturatedAdd(std::size_t left, std::size_t right);
[[nodiscard]] std::size_t keyframeCount(const Layer& layer);
[[nodiscard]] std::size_t keyframeCount(const Light& light);
[[nodiscard]] std::size_t geometryVertexCount(const BufferGeometry& geometry);
[[nodiscard]] std::size_t primitiveSegmentCount(const BufferGeometry& geometry);

} // namespace chrononmotion::motion::scene_detail
