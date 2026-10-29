#include "GltfReaderInternal.hpp"

#include <limits>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace chrononmotion {
namespace gltf_detail {

GltfMotionAsset Document::loadMotion() {
    using namespace motion;
    rejectUnknownKeys(root_, {"asset", "scene", "scenes", "nodes", "buffers", "bufferViews", "accessors", "meshes", "skins", "animations"}, "motion root");
    const auto& asset = required(root_, "asset", "root");
    rejectUnknownKeys(asset, {"version", "generator", "copyright"}, "asset");
    if (required(asset, "version", "asset").string("version") != "2.0") invalid("only glTF 2.0 motion supported");
    parseBuffers(); parseViewsAndAccessors();
    GltfMotionAsset result;
    const auto& nodes = required(root_, "nodes", "motion root").array("nodes");
    if (nodes.size() > 10000) invalid("motion node budget exceeded");
    std::vector<RigNodeId> parents(nodes.size(), 0);
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        rejectUnknownKeys(nodes[i], {"name", "children", "translation", "rotation", "scale", "mesh", "skin"}, "motion node");
        if (const auto* children = find(nodes[i], "children")) for (const auto& raw : children->array("children")) {
            const auto child = indexValue(raw, "child");
            if (child >= nodes.size() || parents[child]) invalid("invalid or multiply parented motion node");
            parents[child] = static_cast<RigNodeId>(i + 1);
        }
    }
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        RigNode node; node.id = static_cast<RigNodeId>(i + 1); node.parent = parents[i];
        if (const auto* p = find(nodes[i], "translation")) node.rest.position = vector3(*p, "translation");
        if (const auto* s = find(nodes[i], "scale")) node.rest.scale = vector3(*s, "scale");
        if (const auto* r = find(nodes[i], "rotation")) {
            const auto& q = r->array("rotation"); if (q.size() != 4) invalid("rotation requires four components");
            node.rest.orientation.set(floatValue(q[0], "rotation"), floatValue(q[1], "rotation"), floatValue(q[2], "rotation"), floatValue(q[3], "rotation"));
        }
        node.bind = node.rest; result.graph.nodes.push_back(node);
    }
    const auto rig = result.graph.compile();
    if (const auto* scenes = find(root_, "scenes")) {
        const auto& list = scenes->array("scenes");
        const auto selected = find(root_, "scene") ? indexValue(*find(root_, "scene"), "scene") : 0;
        if (selected >= list.size()) invalid("motion selected scene out of range");
        for (const auto& scene : list) {
            rejectUnknownKeys(scene, {"name", "nodes"}, "motion scene");
            std::set<std::size_t> roots;
            for (const auto& value : required(scene, "nodes", "scene").array("scene roots")) {
                const auto root = indexValue(value, "scene root");
                if (root >= nodes.size() || parents[root] || !roots.insert(root).second) invalid("invalid motion scene root");
            }
        }
    } else if (find(root_, "scene")) invalid("scene selection requires scenes");
    const auto bind = rig.bindPose();
    const auto nodeIndex = [&](const JsonValue& v) {
        const auto index = indexValue(v, "node index"); if (index >= nodes.size()) invalid("motion node reference out of range"); return index;
    };
    const auto floats = [&](const JsonValue& v, std::size_t components) {
        const auto i = indexValue(v, "accessor"); if (i >= accessors_.size()) invalid("motion accessor out of range");
        return floatAttribute(accessors_[i], components, "motion");
    };
    if (const auto* skins = find(root_, "skins")) {
        if (skins->array("skins").size() > 256) invalid("skin budget exceeded");
        for (const auto& raw : skins->array("skins")) {
            rejectUnknownKeys(raw, {"name", "joints", "skeleton", "inverseBindMatrices"}, "skin");
            GltfMotionSkin skin;
            const auto& joints = required(raw, "joints", "skin").array("joints");
            if (joints.empty() || joints.size() > 10000) invalid("invalid skin joint count");
            for (const auto& joint : joints) {
                const auto id = static_cast<RigNodeId>(nodeIndex(joint) + 1);
                if (std::find(skin.joints.begin(), skin.joints.end(), id) != skin.joints.end()) invalid("duplicate skin joint");
                skin.joints.push_back(id);
            }
            if (const auto* skeleton = find(raw, "skeleton")) skin.skeleton = static_cast<RigNodeId>(nodeIndex(*skeleton) + 1);
            skin.inverseBindMatrices.resize(joints.size());
            if (const auto* inverse = find(raw, "inverseBindMatrices")) {
                const auto accessor = indexValue(*inverse, "inverse bind accessor");
                if (accessor >= accessors_.size() || accessors_[accessor].type != "MAT4") invalid("inverse bind accessor must be MAT4");
                const auto values = floats(*inverse, 16);
                if (values.size() != joints.size() * 16) invalid("inverse bind count mismatch");
                for (std::size_t i = 0; i < joints.size(); ++i) {
                    skin.inverseBindMatrices[i].fromArray(values, i * 16);
                    const auto& m = skin.inverseBindMatrices[i].elements;
                    if (m[3] != 0.f || m[7] != 0.f || m[11] != 0.f || m[15] != 1.f || skin.inverseBindMatrices[i].determinant() == 0.f)
                        invalid("inverse bind must be nonsingular affine");
                }
            }
            result.skins.push_back(std::move(skin));
        }
    }
    const auto* rawMeshes = find(root_, "meshes");
    if (rawMeshes) {
        if (rawMeshes->array("meshes").size() > 10000) invalid("motion mesh budget exceeded");
        for (const auto& mesh : rawMeshes->array("meshes")) {
            rejectUnknownKeys(mesh, {"name", "primitives", "weights"}, "motion mesh");
            const auto& primitives = required(mesh, "primitives", "mesh").array("primitives");
            if (primitives.empty() || primitives.size() > 10000) invalid("invalid motion primitive count");
            for (const auto& primitive : primitives)
                rejectUnknownKeys(primitive, {"attributes", "indices", "mode", "targets"}, "motion primitive");
        }
    }
    std::size_t vertexBudget = 0;
    std::size_t morphVertexBudget = 0;
    std::vector<std::vector<std::size_t>> nodeGeometry(nodes.size());
    for (std::size_t node = 0; node < nodes.size(); ++node) {
        const auto* meshRef = find(nodes[node], "mesh");
        if (!meshRef) { if (find(nodes[node], "skin")) invalid("skin requires mesh"); continue; }
        if (!rawMeshes) invalid("missing meshes");
        const auto& mesh = at(rawMeshes->array("meshes"), indexValue(*meshRef, "mesh"), "mesh");
        rejectUnknownKeys(mesh, {"name", "primitives", "weights"}, "motion mesh");
        const auto* skinRef = find(nodes[node], "skin");
        const GltfMotionSkin* skin = nullptr;
        if (skinRef) { const auto i = indexValue(*skinRef, "skin"); if (i >= result.skins.size()) invalid("skin out of range"); skin = &result.skins[i]; }
        const auto& primitives = required(mesh, "primitives", "mesh").array("primitives");
        for (const auto& primitive : primitives) {
            rejectUnknownKeys(primitive, {"attributes", "indices", "mode", "targets"}, "motion primitive");
            if (find(primitive, "mode") && indexValue(*find(primitive, "mode"), "mode") != 4) invalid("motion supports triangles only");
            const auto& attrs = required(primitive, "attributes", "primitive");
            rejectUnknownKeys(attrs, {"POSITION", "NORMAL", "JOINTS_0", "WEIGHTS_0"}, "motion attributes");
            const auto data = floats(required(attrs, "POSITION", "attributes"), 3);
            const auto count = data.size() / 3;
            if (count > 1000000 - vertexBudget || result.geometry.size() >= 10000) invalid("motion geometry budget exceeded");
            vertexBudget += count;
            RigGeometryBinding geometry; geometry.id = static_cast<std::uint32_t>(result.geometry.size() + 1);
            geometry.rigidNode = static_cast<RigNodeId>(node + 1);
            for (std::size_t i = 0; i < count; ++i) geometry.source.push_back({data[i * 3], data[i * 3 + 1], data[i * 3 + 2]});
            if (const auto* normalAccessor = find(attrs, "NORMAL")) {
                const auto normals = floats(*normalAccessor, 3);
                if (normals.size() != data.size()) invalid("motion NORMAL topology mismatch");
                geometry.sourceNormals.reserve(count);
                for (std::size_t i = 0; i < count; ++i)
                    geometry.sourceNormals.push_back({normals[i * 3], normals[i * 3 + 1], normals[i * 3 + 2]});
            }
            if (const auto* indices = find(primitive, "indices")) {
                const auto index = indexValue(*indices, "indices"); if (index >= accessors_.size()) invalid("index accessor out of range");
                const auto& a = accessors_[index];
                if (a.components != 1 || a.normalized || (a.componentType != 5121 && a.componentType != 5123 && a.componentType != 5125) || a.count % 3)
                    invalid("invalid triangle index accessor");
                for (std::size_t i = 0; i < a.count; ++i) {
                    const auto* e = accessorElement(a, i);
                    const auto v = a.componentType == 5121 ? std::to_integer<unsigned>(*e) : a.componentType == 5123 ? readU16(e) : readU32(e);
                    if (v >= count) invalid("triangle index out of range");
                    geometry.indices.push_back(v);
                }
            } else if (count % 3) invalid("nonindexed triangles require multiple of three vertices");
            if (const auto* targets = find(primitive, "targets")) {
                if (targets->array("targets").size() > 256) invalid("morph budget exceeded");
                for (const auto& target : targets->array("targets")) {
                    rejectUnknownKeys(target, {"POSITION", "NORMAL"}, "morph target");
                    if (!find(target, "POSITION") && !find(target, "NORMAL"))
                        invalid("morph target has no supported POSITION or NORMAL attribute");
                    if (count > 1000000 - morphVertexBudget) invalid("morph vertex budget exceeded");
                    morphVertexBudget += count;
                    RigMorphTarget morph; morph.positions = geometry.source;
                    if (const auto* positionTarget = find(target, "POSITION")) {
                        const auto delta = floats(*positionTarget, 3);
                        if (delta.size() != data.size()) invalid("morph POSITION topology mismatch");
                        for (std::size_t i = 0; i < count; ++i)
                            morph.positions[i].add({delta[i * 3], delta[i * 3 + 1], delta[i * 3 + 2]});
                    }
                    if (const auto* normalTarget = find(target, "NORMAL")) {
                        if (geometry.sourceNormals.size() != count)
                            invalid("NORMAL morph target requires base NORMAL attributes");
                        const auto delta = floats(*normalTarget, 3);
                        if (delta.size() != data.size()) invalid("morph NORMAL topology mismatch");
                        morph.normals = geometry.sourceNormals;
                        for (std::size_t i = 0; i < count; ++i)
                            morph.normals[i].add({delta[i * 3], delta[i * 3 + 1], delta[i * 3 + 2]});
                    }
                    geometry.morphs.push_back(std::move(morph));
                }
            }
            if (const auto* weights = find(mesh, "weights")) {
                if (weights->array("weights").size() != geometry.morphs.size()) invalid("morph weight count mismatch");
                for (std::size_t i = 0; i < geometry.morphs.size(); ++i) geometry.morphs[i].weight = floatValue(weights->array("weights")[i], "weight");
            }
            if (skin) {
                const auto jointAccessor = indexValue(required(attrs, "JOINTS_0", "attributes"), "JOINTS_0");
                if (jointAccessor >= accessors_.size()) invalid("joint accessor out of range");
                const auto& j = accessors_[jointAccessor];
                if (j.components != 4 || j.normalized || (j.componentType != 5121 && j.componentType != 5123) || j.count != count)
                    invalid("invalid joint accessor");
                const auto weightIndex = indexValue(required(attrs, "WEIGHTS_0", "attributes"), "WEIGHTS_0");
                if (weightIndex >= accessors_.size()) invalid("weight accessor out of range");
                const auto& w = accessors_[weightIndex];
                std::vector<float> weights;
                if (w.componentType == 5126 && !w.normalized) weights = floats(required(attrs, "WEIGHTS_0", "attributes"), 4);
                else {
                    if (w.components != 4 || !w.normalized || (w.componentType != 5121 && w.componentType != 5123))
                        invalid("weights require float or normalized unsigned VEC4");
                    for (std::size_t vertex = 0; vertex < w.count; ++vertex) for (unsigned influence = 0; influence < 4; ++influence) {
                        const auto* e = accessorElement(w, vertex) + influence * w.componentBytes;
                        weights.push_back(w.componentType == 5121 ? static_cast<float>(std::to_integer<unsigned>(*e)) / 255.f :
                                          static_cast<float>(readU16(e)) / 65535.f);
                    }
                }
                if (weights.size() != count * 4) invalid("skin weight topology mismatch");
                geometry.rigidNode = 0; geometry.skin.resize(count);
                for (std::size_t i = 0; i < count; ++i) for (unsigned influence = 0; influence < 4; ++influence) {
                    const auto* e = accessorElement(j, i) + influence * j.componentBytes;
                    const auto joint = j.componentType == 5121 ? std::to_integer<unsigned>(*e) : readU16(e);
                    if (joint >= skin->joints.size()) invalid("vertex joint outside skin");
                    const float weight = weights[i * 4 + influence];
                    geometry.skin[i].weights[influence] = weight;
                    if (weight != 0.f) geometry.skin[i].joints[influence] = skin->joints[joint];
                }
                for (std::size_t joint = 0; joint < skin->joints.size(); ++joint)
                    geometry.inverseBinds.push_back({skin->joints[joint], skin->inverseBindMatrices[joint]});
                (void) skinRigGeometry(rig, bind, geometry.source, geometry.skin, geometry.inverseBinds);
            } else if (find(attrs, "JOINTS_0") || find(attrs, "WEIGHTS_0")) invalid("joint attributes require node skin");
            (void) mixRigMorphs(geometry.source, geometry.morphs);
            (void) mixRigMorphNormals(geometry.sourceNormals, geometry.morphs);
            nodeGeometry[node].push_back(result.geometry.size());
            result.geometry.push_back(std::move(geometry));
        }
    }
    parseMotionAnimations(nodes, nodeGeometry, result);
    return result;
}

std::uint32_t littleU32(std::span<const std::byte> bytes, std::size_t offset) {
    if (offset > bytes.size() || bytes.size() - offset < 4) invalid("truncated GLB header or chunk header");
    return readU32(bytes.data() + offset);
}

GltfAsset loadDocument(std::string_view json, std::vector<std::vector<std::byte>> buffers,
                       const GltfExternalBufferResolver& resolver, bool hasGlbBinChunk = false,
                       const GltfReadPolicy& policy = {}) {
    JsonValue root = motion::detail::JsonParser(json, std::min(kMaxJsonBytes, policy.maxJsonBytes)).parse();
    Document document(std::move(root), std::move(buffers), hasGlbBinChunk, policy);
    document.setExternalResolver(&resolver);
    return document.load();
}

} // namespace gltf_detail
} // namespace chrononmotion

namespace chrononmotion {

GltfMotionAsset loadGltfMotion(std::string_view json, const GltfExternalBufferResolver& resolver,
                               const GltfReadPolicy& policy) {
    using gltf_detail::Document;
    using motion::detail::kMaxJsonBytes;
    using motion::detail::JsonParser;
    if (!policy.enabled || !policy.allowMotionGltfImport) gltf_detail::invalid("motion import is disabled by authoring policy");
    if (policy.audit) policy.audit(true, "gltf-json-motion");
    try {
        Document document(JsonParser(json, std::min(kMaxJsonBytes, policy.maxJsonBytes)).parse(),
                          {}, false, policy);
        document.setExternalResolver(&resolver);
        return document.loadMotion();
    } catch (const std::invalid_argument& error) {
        constexpr std::string_view diagnosticPrefix{"glTF: "};
        const std::string_view message{error.what()};
        if (message.starts_with(diagnosticPrefix)) throw;
        gltf_detail::invalid(std::string(message));
    }
}

GltfAsset loadGltf(std::string_view json, const GltfExternalBufferResolver& resolveExternalBuffer,
                   const GltfReadPolicy& policy) {
    if (!policy.enabled || !policy.allowStaticGltfImport) gltf_detail::invalid("static import is disabled by authoring policy");
    if (policy.audit) policy.audit(false, "gltf-json-static");
    return gltf_detail::loadDocument(json, {}, resolveExternalBuffer, false, policy);
}

GltfAsset loadGlb(std::span<const std::byte> bytes, const GltfExternalBufferResolver& resolveExternalBuffer,
                  const GltfReadPolicy& policy) {

    using motion::detail::kMaxJsonBytes;
    if (!policy.enabled || !policy.allowStaticGltfImport) gltf_detail::invalid("static import is disabled by authoring policy");
    if (policy.audit) policy.audit(false, "glb-static");
    if (bytes.size() < 12 || bytes.size() > policy.maxTotalBufferBytes + std::min(kMaxJsonBytes, policy.maxJsonBytes) + 1024) gltf_detail::invalid("GLB size is invalid");
    if (gltf_detail::littleU32(bytes, 0) != 0x46546c67u) gltf_detail::invalid("invalid GLB magic");
    if (gltf_detail::littleU32(bytes, 4) != 2) gltf_detail::invalid("only GLB version 2 is supported");
    if (gltf_detail::littleU32(bytes, 8) != bytes.size()) gltf_detail::invalid("GLB declared length does not match input size");
    std::size_t offset = 12;
    std::string_view json;
    std::vector<std::byte> binary;
    bool gotJson = false;
    bool gotBin = false;
    while (offset < bytes.size()) {
        if (bytes.size() - offset < 8) gltf_detail::invalid("truncated GLB chunk header");
        const std::size_t length = gltf_detail::littleU32(bytes, offset);
        const std::uint32_t type = gltf_detail::littleU32(bytes, offset + 4);
        offset += 8;
        if (length % 4 != 0 || length > bytes.size() - offset) gltf_detail::invalid("GLB chunk length or alignment is invalid");
        if (type == 0x4e4f534au) {
            if (gotJson || gotBin || offset != 20) gltf_detail::invalid("GLB must contain exactly one first JSON chunk");
            json = std::string_view(reinterpret_cast<const char*>(bytes.data() + offset), length);
            while (!json.empty() && json.back() == ' ') json.remove_suffix(1);
            if (json.find('\0') != std::string_view::npos)
                gltf_detail::invalid("GLB JSON chunk padding must use spaces, not NUL bytes");
            gotJson = true;
        } else if (type == 0x004e4942u) {
            if (!gotJson || gotBin) gltf_detail::invalid("GLB BIN chunk must follow JSON and occur at most once");
            binary.assign(bytes.begin() + static_cast<std::ptrdiff_t>(offset),
                          bytes.begin() + static_cast<std::ptrdiff_t>(offset + length));
            gotBin = true;
        } else gltf_detail::invalid("unknown GLB chunk type");
        offset += length;
    }
    if (!gotJson) gltf_detail::invalid("GLB has no JSON chunk");
    std::vector<std::vector<std::byte>> buffers;
    if (gotBin) buffers.push_back(std::move(binary));
    return gltf_detail::loadDocument(json, std::move(buffers), resolveExternalBuffer, gotBin, policy);
}

} // namespace chrononmotion
