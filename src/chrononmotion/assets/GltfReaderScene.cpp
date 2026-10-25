#include "GltfReaderInternal.hpp"

namespace chrononmotion {
namespace gltf_detail {

GltfAsset Document::load() {
    rejectUnknownKeys(root_, {"asset", "scene", "scenes", "nodes", "buffers", "bufferViews",
                             "accessors", "meshes", "extensionsUsed", "extensionsRequired",
                             "extensions", "extras"}, "root");
    const auto& asset = required(root_, "asset", "root");
    rejectUnknownKeys(asset, {"version", "minVersion", "generator", "copyright", "extras"}, "asset");
    if (required(asset, "version", "asset").string("asset.version") != "2.0") invalid("only glTF 2.0 is supported");
    if (find(asset, "minVersion")) invalid("asset.minVersion is outside the static glTF reader contract");
    if (find(root_, "extensions")) invalid("glTF extensions are outside the static reader contract");
    if (find(root_, "animations")) invalid("animated glTF assets are outside the static reader contract");
    if (find(root_, "skins")) invalid("skinned meshes are not supported");
    if (find(root_, "materials") || find(root_, "textures") || find(root_, "images") ||
        find(root_, "samplers") || find(root_, "cameras"))
        invalid("materials, textures, images, samplers, and cameras are not supported");
    if (find(root_, "extensionsRequired")) {
        const auto& extensions = required(root_, "extensionsRequired", "root").array("extensionsRequired");
        if (!extensions.empty()) invalid("required extensions are not supported");
    }
    if (find(root_, "extensionsUsed")) {
        const auto& extensions = required(root_, "extensionsUsed", "root").array("extensionsUsed");
        if (!extensions.empty()) invalid("glTF extensions are outside the static reader contract");
    }
    parseBuffers();
    parseViewsAndAccessors();
    parseMeshes();
    return parseScene();
}

void Document::parseBuffers() {
    const auto* entries = find(root_, "buffers");
    if (!entries) invalid("buffers array is required");
    const auto& list = entries->array("buffers");
    if (list.size() > 256 || buffers_.size() > list.size()) invalid("too many buffers");
    if (buffers_.size() < list.size()) buffers_.resize(list.size());
    declaredBufferLengths_.resize(list.size());
    std::size_t totalBytes = 0;
    std::size_t totalDeclaredBytes = 0;
    for (const auto& raw : list) {
        const auto declared = indexValue(required(raw, "byteLength", "buffer"), "buffer.byteLength");
        if (declared > policy_.maxTotalBufferBytes - std::min(totalDeclaredBytes, policy_.maxTotalBufferBytes))
            invalid("aggregate declared buffer bytes exceed the 512 MiB limit");
        totalDeclaredBytes += declared;
    }
    if (hasGlbBinChunk_ && list.empty()) invalid("GLB BIN chunk has no corresponding buffer declaration");
    for (std::size_t i = 0; i < list.size(); ++i) {
        rejectUnknownKeys(list[i], {"byteLength", "uri", "name", "extras"}, "buffer");
        const std::size_t expected = indexValue(required(list[i], "byteLength", "buffer"), "buffer.byteLength");
        if (expected > policy_.maxBufferBytes) invalid("buffer exceeds the configured per-buffer limit");
        declaredBufferLengths_[i] = expected;
        if (i == 0 && hasGlbBinChunk_ && find(list[i], "uri"))
            invalid("GLB buffer 0 must omit uri when a BIN chunk is present");
        if (const auto* uriValue = find(list[i], "uri")) {
            const std::string uri = uriValue->string("buffer.uri");
            constexpr std::string_view octetPrefix = "data:application/octet-stream;base64,";
            constexpr std::string_view gltfPrefix = "data:application/gltf-buffer;base64,";
            if (uri.starts_with(octetPrefix)) buffers_[i] = decodeBase64(std::string_view(uri).substr(octetPrefix.size()));
            else if (uri.starts_with(gltfPrefix)) buffers_[i] = decodeBase64(std::string_view(uri).substr(gltfPrefix.size()));
            else {
                if (!externalResolver_ || !static_cast<bool>(*externalResolver_))
                    invalid("external buffer URI requires a caller-supplied resolver");
                buffers_[i] = (*externalResolver_)(uri);
            }
        } else if (buffers_[i].empty() && expected != 0) invalid("buffer has no URI or GLB BIN chunk");
        if (buffers_[i].size() < expected) invalid("buffer data is shorter than declared byteLength");
        if (i == 0 && hasGlbBinChunk_) {
            if (buffers_[i].size() - expected > 3)
                invalid("GLB BIN chunk may contain at most three padding bytes beyond buffer.byteLength");
        } else if (buffers_[i].size() != expected) {
            invalid("non-GLB buffer data length must equal declared byteLength");
        }
        if (buffers_[i].size() > policy_.maxBufferBytes) invalid("buffer exceeds the configured per-buffer limit");
        if (buffers_[i].size() > policy_.maxTotalBufferBytes - std::min(totalBytes, policy_.maxTotalBufferBytes))
            invalid("aggregate buffer bytes exceed the 512 MiB limit");
        totalBytes += buffers_[i].size();
    }
}

void Document::parseViewsAndAccessors() {
    const auto* viewList = find(root_, "bufferViews");
    if (!viewList) invalid("bufferViews array is required");
    const auto& rawViews = viewList->array("bufferViews");
    if (rawViews.size() > 1'000'000) invalid("too many bufferViews");
    views_.reserve(rawViews.size());
    for (const auto& raw : rawViews) {
        rejectUnknownKeys(raw, {"buffer", "byteOffset", "byteLength", "byteStride", "target", "name", "extras"}, "bufferView");
        const std::size_t buffer = indexValue(required(raw, "buffer", "bufferView"), "bufferView.buffer");
        const std::size_t offset = find(raw, "byteOffset") ? indexValue(*find(raw, "byteOffset"), "bufferView.byteOffset") : 0;
        const std::size_t length = indexValue(required(raw, "byteLength", "bufferView"), "bufferView.byteLength");
        const std::size_t stride = find(raw, "byteStride") ? indexValue(*find(raw, "byteStride"), "bufferView.byteStride") : 0;
        if (buffer >= buffers_.size() || offset > declaredBufferLengths_[buffer] ||
            length > declaredBufferLengths_[buffer] - offset)
            invalid("bufferView range exceeds its declared buffer byteLength");
        if (stride != 0 && (stride < 4 || stride > 252 || stride % 4 != 0)) invalid("bufferView byteStride must be a multiple of 4 in [4,252]");
        std::optional<std::size_t> target;
        if (find(raw, "target")) {
            target = indexValue(*find(raw, "target"), "bufferView.target");
            if (*target != 34962 && *target != 34963) invalid("bufferView target must be ARRAY_BUFFER or ELEMENT_ARRAY_BUFFER");
            if (*target == 34963 && stride != 0)
                invalid("ELEMENT_ARRAY_BUFFER bufferViews cannot specify byteStride");
        }
        views_.push_back({buffer, offset, length, stride, target});
    }
    const auto* accessorList = find(root_, "accessors");
    if (!accessorList) invalid("accessors array is required");
    const auto& rawAccessors = accessorList->array("accessors");
    if (rawAccessors.size() > 1'000'000) invalid("too many accessors");
    accessors_.reserve(rawAccessors.size());
    for (const auto& raw : rawAccessors) {
        rejectUnknownKeys(raw, {"bufferView", "byteOffset", "componentType", "normalized", "count",
                                "type", "max", "min", "name", "sparse", "extras"}, "accessor");
        if (find(raw, "sparse")) invalid("sparse accessors are not supported");
        const std::size_t view = indexValue(required(raw, "bufferView", "accessor"), "accessor.bufferView");
        const std::size_t offset = find(raw, "byteOffset") ? indexValue(*find(raw, "byteOffset"), "accessor.byteOffset") : 0;
        const std::size_t count = indexValue(required(raw, "count", "accessor"), "accessor.count");
        const auto component = indexValue(required(raw, "componentType", "accessor"), "accessor.componentType");
        if (component > std::numeric_limits<std::uint32_t>::max()) invalid("accessor componentType is out of range");
        const std::string type = required(raw, "type", "accessor").string("accessor.type");
        const std::size_t components = componentCount(type);
        const std::size_t bytes = componentSize(static_cast<std::uint32_t>(component));
        const bool normalized = find(raw, "normalized") ? find(raw, "normalized")->boolean("accessor.normalized") : false;
        if (view >= views_.size() || count == 0 || count > kMaxVerticesPerAccessor) invalid("accessor view or count is invalid");
        std::array<std::vector<double>, 2> declaredBounds;
        std::size_t boundIndex = 0;
        for (const auto metadata : {std::string_view("min"), std::string_view("max")}) {
            if (const auto* bounds = find(raw, metadata)) {
                const auto& values = bounds->array(std::string("accessor.") + std::string(metadata));
                if (values.size() != components) invalid("accessor min/max must match the accessor type");
                auto& stored = declaredBounds[boundIndex];
                stored.reserve(values.size());
                for (const auto& bound : values) {
                    const double value = bound.number("accessor min/max");
                    if (!std::isfinite(value)) invalid("accessor min/max values must be finite");
                    stored.push_back(value);
                }
            }
            ++boundIndex;
        }
        if (find(raw, "min") && find(raw, "max")) {
            for (std::size_t c = 0; c < components; ++c)
                if (declaredBounds[0][c] > declaredBounds[1][c])
                    invalid("accessor min component exceeds max component");
        }
        const auto& bufferView = views_[view];
        const std::size_t elementSize = components * bytes;
        const std::size_t stride = bufferView.stride == 0 ? elementSize : bufferView.stride;
        if (stride < elementSize || stride % bytes != 0 || offset % bytes != 0 || bufferView.offset % bytes != 0 ||
            offset > std::numeric_limits<std::size_t>::max() - bufferView.offset ||
            (bufferView.offset + offset) % bytes != 0)
            invalid("accessor alignment or stride is invalid");
        if (count - 1 > (std::numeric_limits<std::size_t>::max() - elementSize) / stride)
            invalid("accessor byte range overflows");
        const std::size_t requiredBytes = (count - 1) * stride + elementSize;
        if (offset > bufferView.length || requiredBytes > bufferView.length - offset) invalid("accessor range exceeds bufferView");
        if (type == "VEC3" && bytes == 4 && component == 5126 && !normalized) {
            for (const auto metadata : {std::string_view("min"), std::string_view("max")}) {
                const auto* bounds = find(raw, metadata);
                if (!bounds) continue;
                const auto& declaredValues = bounds->array("accessor min/max");
                for (std::size_t componentIndex = 0; componentIndex < components; ++componentIndex) {
                    float actualMin = std::numeric_limits<float>::infinity();
                    float actualMax = -std::numeric_limits<float>::infinity();
                    for (std::size_t elementIndex = 0; elementIndex < count; ++elementIndex) {
                        const auto* data = buffers_[bufferView.buffer].data() + bufferView.offset + offset +
                            elementIndex * stride + componentIndex * bytes;
                        const float decoded = std::bit_cast<float>(readU32(data));
                        if (!std::isfinite(decoded)) invalid("float accessor contains non-finite values");
                        actualMin = std::min(actualMin, decoded);
                        actualMax = std::max(actualMax, decoded);
                    }
                    const double declared = declaredValues[componentIndex].number("accessor min/max");
                    const float actual = metadata == "min" ? actualMin : actualMax;
                    if (std::abs(declared - static_cast<double>(actual)) >
                        1e-5 * std::max({1.0, std::abs(declared), std::abs(static_cast<double>(actual))}))
                        invalid("accessor min/max does not match decoded VEC3 data");
                }
            }
        }
        accessors_.push_back({view, offset, count, components, bytes,
                              static_cast<std::uint32_t>(component), type, normalized,
                              find(raw, "min") != nullptr, find(raw, "max") != nullptr});
    }
}

std::vector<float> Document::floatAttribute(const Accessor& accessor, std::size_t expectedComponents,
                                            std::string_view semantic) const {
    const bool floatValues = accessor.componentType == 5126 && !accessor.normalized;
    const bool normalizedNormal = semantic == "NORMAL" && accessor.normalized &&
        (accessor.componentType == 5120 || accessor.componentType == 5122);
    const bool normalizedUv = semantic == "TEXCOORD_0" && accessor.normalized &&
        (accessor.componentType == 5121 || accessor.componentType == 5123);
    if (accessor.components != expectedComponents ||
        (semantic == "POSITION" && !floatValues) ||
        (semantic != "POSITION" && !floatValues && !normalizedNormal && !normalizedUv))
        invalid(std::string(semantic) + " has an unsupported accessor type/normalization combination");
    if (accessor.count > std::numeric_limits<std::size_t>::max() / expectedComponents) invalid("attribute allocation overflow");
    std::vector<float> result(accessor.count * expectedComponents);
    for (std::size_t i = 0; i < accessor.count; ++i)
        for (std::size_t c = 0; c < expectedComponents; ++c) {
            const std::byte* element = accessorElement(accessor, i) + c * accessor.componentBytes;
            float value{};
            if (accessor.componentType == 5126 && !accessor.normalized) {
                value = std::bit_cast<float>(readU32(element));
            } else if (semantic == "NORMAL" && accessor.normalized && accessor.componentType == 5120) {
                const auto raw = static_cast<std::int8_t>(std::to_integer<std::uint8_t>(*element));
                value = std::max(-1.f, static_cast<float>(raw) / 127.f);
            } else if (semantic == "NORMAL" && accessor.normalized && accessor.componentType == 5122) {
                const auto raw = static_cast<std::int16_t>(readU16(element));
                value = std::max(-1.f, static_cast<float>(raw) / 32767.f);
            } else if (semantic == "TEXCOORD_0" && accessor.normalized && accessor.componentType == 5121) {
                value = static_cast<float>(std::to_integer<std::uint8_t>(*element)) / 255.f;
            } else if (semantic == "TEXCOORD_0" && accessor.normalized && accessor.componentType == 5123) {
                value = static_cast<float>(readU16(element)) / 65535.f;
            } else {
                invalid(std::string(semantic) + " has an unsupported component type or normalization mode");
            }
            if (!std::isfinite(value)) invalid(std::string(semantic) + " contains non-finite values");
            result[i * expectedComponents + c] = value;
        }
    return result;
}

void Document::parseMeshes() {
    const auto& rawMeshes = required(root_, "meshes", "root").array("meshes");
    if (rawMeshes.size() > std::min(kMaxMeshes, policy_.maxMeshes)) invalid("mesh count exceeds the configured limit");
    std::size_t primitiveCount = 0;
    std::size_t totalVertices = 0;
    std::size_t totalIndices = 0;
    meshes_.reserve(rawMeshes.size());
    for (std::size_t meshIndex = 0; meshIndex < rawMeshes.size(); ++meshIndex) {
        const auto& raw = rawMeshes[meshIndex];
        rejectUnknownKeys(raw, {"name", "primitives", "weights", "extras"}, "mesh");
        MeshData mesh;
        mesh.name = find(raw, "name") ? find(raw, "name")->string("mesh.name") : "mesh_" + std::to_string(meshIndex);
        if (find(raw, "weights")) invalid("mesh morph weights are not supported");
        const auto& primitives = required(raw, "primitives", "mesh").array("mesh.primitives");
        if (primitives.empty() || primitives.size() > std::min(kMaxPrimitives, policy_.maxPrimitives) - std::min(primitiveCount, std::min(kMaxPrimitives, policy_.maxPrimitives))) invalid("primitive count exceeds the configured limit");
        primitiveCount += primitives.size();
        for (std::size_t primitiveIndex = 0; primitiveIndex < primitives.size(); ++primitiveIndex) {
            const auto& primitive = primitives[primitiveIndex];
            rejectUnknownKeys(primitive, {"attributes", "indices", "mode", "material", "targets", "extras"}, "primitive");
            if (find(primitive, "material")) invalid("primitive materials are outside the geometry-only reader contract");
            const auto mode = find(primitive, "mode") ? indexValue(*find(primitive, "mode"), "primitive.mode") : 4;
            if (mode != 4) invalid("only TRIANGLES primitive mode is supported");
            if (find(primitive, "targets")) invalid("morph targets are not supported");
            const auto& attributes = required(primitive, "attributes", "primitive").object("primitive.attributes");
            for (const auto& [semantic, unused] : attributes) {
                (void)unused;
                if (semantic != "POSITION" && semantic != "NORMAL" && semantic != "TEXCOORD_0")
                    invalid("unsupported vertex attribute '" + semantic + "'");
            }
            const auto positionIt = attributes.find("POSITION");
            if (positionIt == attributes.end()) invalid("primitive has no POSITION attribute");
            const std::size_t positionIndex = indexValue(positionIt->second, "POSITION accessor");
            if (positionIndex >= accessors_.size()) invalid("POSITION accessor index is out of range");
            const auto& positionAccessor = accessors_[positionIndex];
            if (!positionAccessor.hasMin || !positionAccessor.hasMax)
                invalid("POSITION accessor requires min and max bounds");
            const auto& positionView = views_[positionAccessor.view];
            if (positionView.target && *positionView.target != 34962)
                invalid("vertex attributes require ARRAY_BUFFER bufferViews");
            if (positionAccessor.count > std::min(kMaxTotalVertices, policy_.maxVertices) - std::min(totalVertices, std::min(kMaxTotalVertices, policy_.maxVertices)))
                invalid("total decoded vertices exceed the configured limit");
            totalVertices += positionAccessor.count;
            auto geometry = BufferGeometry::create();
            auto positions = floatAttribute(positionAccessor, 3, "POSITION");
            geometry->setAttribute("position", FloatBufferAttribute::create(std::move(positions), 3));
            for (const auto semantic : {std::string_view("NORMAL"), std::string_view("TEXCOORD_0")}) {
                const auto it = attributes.find(semantic);
                if (it == attributes.end()) continue;
                const std::size_t accessorIndex = indexValue(it->second, semantic);
                if (accessorIndex >= accessors_.size()) invalid(std::string(semantic) + " accessor index is out of range");
                const std::size_t components = semantic == "NORMAL" ? 3 : 2;
                const auto& accessor = accessors_[accessorIndex];
                if (views_[accessor.view].target && *views_[accessor.view].target != 34962)
                    invalid("vertex attributes require ARRAY_BUFFER bufferViews");
                if (accessor.count != positionAccessor.count) invalid(std::string(semantic) + " count must match POSITION");
                auto data = floatAttribute(accessor, components, semantic);
                geometry->setAttribute(std::string(semantic == "NORMAL" ? "normal" : "uv"),
                                       FloatBufferAttribute::create(std::move(data), static_cast<int>(components)));
            }
            if (const auto* indexValueJson = find(primitive, "indices")) {
                const std::size_t index = indexValue(*indexValueJson, "primitive.indices");
                if (index >= accessors_.size()) invalid("index accessor is out of range");
                const auto& accessor = accessors_[index];
                const auto& indexView = views_[accessor.view];
                if (indexView.target && *indexView.target != 34963)
                    invalid("primitive indices require ELEMENT_ARRAY_BUFFER bufferViews");
                if (indexView.stride != 0)
                    invalid("primitive index accessors cannot use byteStride");
                if (accessor.type != "SCALAR" || accessor.normalized ||
                    (accessor.componentType != 5121 && accessor.componentType != 5123 && accessor.componentType != 5125))
                    invalid("indices must be an unnormalized unsigned byte, short, or int SCALAR accessor");
                if (accessor.count % 3 != 0) invalid("triangle index count must be divisible by three");
                if (accessor.count > kMaxTotalIndices - totalIndices)
                    invalid("total decoded indices exceed the 24000000 limit");
                totalIndices += accessor.count;
                std::vector<unsigned int> indices;
                indices.reserve(accessor.count);
                for (std::size_t i = 0; i < accessor.count; ++i) {
                    const auto* element = accessorElement(accessor, i);
                    std::uint32_t value = 0;
                    if (accessor.componentType == 5121) value = std::to_integer<std::uint8_t>(*element);
                    else if (accessor.componentType == 5123) value = readU16(element);
                    else value = readU32(element);
                    if (value >= positionAccessor.count) invalid("index exceeds POSITION vertex count");
                    indices.push_back(value);
                }
                geometry->setIndex(std::move(indices));
            } else if (positionAccessor.count % 3 != 0) invalid("non-indexed triangle vertex count must be divisible by three");
            geometry->computeBoundingBox();
            geometry->computeBoundingSphere();
            mesh.primitives.push_back({std::move(geometry)});
        }
        meshes_.push_back(std::move(mesh));
    }
}

GltfAsset Document::parseScene() {
    GltfAsset asset;
    asset.scene = Scene::create();
    asset.scene->name = "glTF";
    const auto& rawNodes = required(root_, "nodes", "root").array("nodes");
    if (rawNodes.size() > std::min(kMaxNodes, policy_.maxNodes)) invalid("node count exceeds the configured limit");
    for (const auto& node : rawNodes)
        rejectUnknownKeys(node, {"name", "mesh", "children", "translation", "rotation", "scale", "matrix", "camera", "skin", "weights", "extensions", "extras"}, "node");
    for (const auto& mesh : required(root_, "meshes", "root").array("meshes")) {
        rejectUnknownKeys(mesh, {"name", "primitives", "weights", "extras"}, "mesh");
        for (const auto& primitive : required(mesh, "primitives", "mesh").array("mesh.primitives"))
            rejectUnknownKeys(primitive, {"attributes", "indices", "mode", "material", "targets", "extras"}, "primitive");
    }
    const auto& scenes = required(root_, "scenes", "root").array("scenes");
    for (const auto& scene : scenes) {
        rejectUnknownKeys(scene, {"name", "nodes", "extensions", "extras"}, "scene");
        if (find(scene, "extensions")) invalid("scene extensions are not supported");
    }
    if (scenes.empty()) invalid("scenes must contain at least one scene");
    const std::size_t sceneIndex = find(root_, "scene") ? indexValue(*find(root_, "scene"), "root.scene") : 0;
    const auto& sceneDefinition = at(scenes, sceneIndex, "scenes");
    const auto& roots = required(sceneDefinition, "nodes", "scene").array("scene.nodes");
    std::vector<unsigned char> state(rawNodes.size(), 0);
    std::vector<bool> hasParent(rawNodes.size(), false);
    for (const auto& node : rawNodes) {
        if (const auto* children = find(node, "children")) {
            for (const auto& childValue : children->array("node.children")) {
                const std::size_t child = indexValue(childValue, "node child");
                if (child >= rawNodes.size()) invalid("node child index is out of range");
                if (hasParent[child]) invalid("node has multiple parents");
                hasParent[child] = true;
            }
        }
    }
    std::size_t nodesVisited = 0;
    std::size_t sceneMeshInstances = 0;
    constexpr std::size_t kMaxSceneMeshInstances = 100'000;
    constexpr std::size_t kMaxNodeDepth = 256;
    const auto visit = [&](const auto& self, std::size_t nodeIndex,
                           const std::shared_ptr<Object3D>& parent, std::size_t depth) -> void {
        if (depth > kMaxNodeDepth) invalid("selected scene node depth exceeds 256");
        if (nodeIndex >= rawNodes.size()) invalid("scene root node index is out of range");
        if (state[nodeIndex] == 1) invalid("node hierarchy contains a cycle");
        if (state[nodeIndex] == 2) invalid("scene contains a node more than once");
        if (++nodesVisited > kMaxNodes) invalid("scene node visit limit exceeded");
        state[nodeIndex] = 1;
        const auto& raw = rawNodes[nodeIndex];
        if (find(raw, "camera") || find(raw, "skin") || find(raw, "weights"))
            invalid("camera, skin, and morph-weight nodes are unsupported");
        if (find(raw, "matrix") && find(raw, "matrix")->array("node.matrix").size() != 16)
            invalid("node.matrix must contain 16 values");
        if (find(raw, "translation") && find(raw, "translation")->array("node.translation").size() != 3)
            invalid("node.translation must contain three values");
        if (find(raw, "rotation") && find(raw, "rotation")->array("node.rotation").size() != 4)
            invalid("node.rotation must contain four values");
        if (find(raw, "scale") && find(raw, "scale")->array("node.scale").size() != 3)
            invalid("node.scale must contain three values");
        if (find(raw, "extensions")) invalid("node extensions are not supported");
        auto group = Group::create();
        group->name = find(raw, "name") ? find(raw, "name")->string("node.name") : "node_" + std::to_string(nodeIndex);
        const bool hasMatrix = find(raw, "matrix") != nullptr;
        const bool hasTrs = find(raw, "translation") || find(raw, "rotation") || find(raw, "scale");
        if (hasMatrix && hasTrs) invalid("node may specify matrix or TRS, not both");
        if (hasMatrix) {
            const auto& matrix = find(raw, "matrix")->array("node.matrix");
            if (matrix.size() != 16) invalid("node.matrix must contain 16 values");
            std::array<float, 16> elements{};
            for (std::size_t i = 0; i < 16; ++i) elements[i] = floatValue(matrix[i], "node.matrix");
            if (std::abs(elements[3]) > 1e-6f || std::abs(elements[7]) > 1e-6f ||
                std::abs(elements[11]) > 1e-6f || std::abs(elements[15] - 1.f) > 1e-6f)
                invalid("node.matrix must be affine");
            group->matrix->fromArray(elements);
            group->matrixAutoUpdate = false;
        } else {
            if (const auto* translation = find(raw, "translation")) group->position.copy(vector3(*translation, "node.translation"));
            if (const auto* scale = find(raw, "scale")) group->scale.copy(vector3(*scale, "node.scale"));
            if (const auto* rotation = find(raw, "rotation")) {
                const auto& values = rotation->array("node.rotation");
                if (values.size() != 4) invalid("node.rotation must contain four values");
                Quaternion quaternion(floatValue(values[0], "node.rotation"), floatValue(values[1], "node.rotation"),
                                      floatValue(values[2], "node.rotation"), floatValue(values[3], "node.rotation"));
                const float length = quaternion.length();
                if (!std::isfinite(length) || std::abs(length - 1.f) > 1e-3f) invalid("node.rotation quaternion must be normalized");
                quaternion.normalize();
                group->quaternion.copy(quaternion);
                group->rotation.setFromQuaternion(quaternion);
            }
        }
        parent->add(group);
        if (const auto* meshValue = find(raw, "mesh")) {
            const std::size_t meshIndex = indexValue(*meshValue, "node.mesh");
            if (meshIndex >= meshes_.size()) invalid("node.mesh index is out of range");
            const auto& mesh = meshes_[meshIndex];
            if (mesh.primitives.size() > kMaxSceneMeshInstances - sceneMeshInstances)
                invalid("selected scene mesh instance count exceeds 100000");
            sceneMeshInstances += mesh.primitives.size();
            for (std::size_t primitive = 0; primitive < mesh.primitives.size(); ++primitive) {
                auto object = Mesh::create(mesh.primitives[primitive].geometry);
                object->name = mesh.name + "_primitive_" + std::to_string(primitive);
                group->add(object);
                asset.meshes.push_back(std::move(object));
            }
        }
        if (const auto* children = find(raw, "children"))
            for (const auto& childValue : children->array("node.children"))
                self(self, indexValue(childValue, "node child"), group, depth + 1);
        state[nodeIndex] = 2;
    };
    for (const auto& root : roots) {
        const std::size_t nodeIndex = indexValue(root, "scene root node");
        if (nodeIndex >= hasParent.size()) invalid("scene root node index is out of range");
        if (hasParent[nodeIndex]) invalid("scene root node is also a child in the node graph");
        visit(visit, nodeIndex, asset.scene, 0);
    }
    if (asset.meshes.empty()) invalid("selected scene contains no mesh primitives");
    asset.scene->updateMatrixWorld(true);
    return asset;
}

} // namespace gltf_detail
} // namespace chrononmotion
