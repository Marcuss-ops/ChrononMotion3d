#include "GltfReaderInternal.hpp"

#include <limits>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace chrononmotion {
namespace gltf_detail {



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
    Document document(JsonParser(json, std::min(kMaxJsonBytes, policy.maxJsonBytes)).parse(), {}, false, policy); document.setExternalResolver(&resolver);
    return document.loadMotion();
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
