// Proof: GLB import -> headless render -> PPM.
//
// Modes:
//   1. Self-contained proof (default):
//        chrononmotion_render_glb [out.ppm [w h]]
//      Synthesizes a deterministic binary GLB 2.0 (24-vertex indexed cube with
//      POSITION, NORMAL, TEXCOORD_0, node rotation, and an embedded 64x64 PNG
//      checkerboard texture in the GLB BIN chunk), loads it via GLTFLoader,
//      renders it offscreen with the imported PBR material plus direct lights, verifies pixel coverage and
//      texture variation, and writes out.ppm.
//   2. External GLB file:
//        chrononmotion_render_glb <model.glb> <out.ppm> [w h]
//   3. Camera-move preview frames:
//        chrononmotion_render_glb --move <preset> <model.glb> <frame_dir> <frames> <width> [height]
//
// Optional flags, usable with any mode and ignored when absent:
//   --fit=<fraction>        frame the subject at that fraction of the view
//   --camera-pos=x,y,z      lock the camera position (no auto framing)
//   --camera-target=x,y,z   lock the look-at point
//   --camera-fov=<degrees>  lock the lens; required by the locked camera
//   --exposure=<value>      override tone-mapping exposure
// Locked-camera flags keep a per-frame render sequence registered, which the
// auto framing cannot do when each frame's bounds change.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <string>
#include <vector>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#include "stb_image.h"

#include "threepp/animation/AnimationClip.hpp"
#include "threepp/animation/AnimationMixer.hpp"
#include "threepp/cameras/PerspectiveCamera.hpp"
#include "threepp/canvas/Canvas.hpp"
#include "threepp/geometries/PlaneGeometry.hpp"
#include "threepp/lights/AmbientLight.hpp"
#include "threepp/lights/DirectionalLight.hpp"
#include "threepp/lights/SpotLight.hpp"
#include "threepp/loaders/GLTFLoader.hpp"
#include "threepp/math/Box3.hpp"
#include "threepp/materials/MeshBasicMaterial.hpp"
#include "threepp/materials/MeshStandardMaterial.hpp"
#include "threepp/objects/Group.hpp"
#include "threepp/objects/Mesh.hpp"
#include "threepp/renderers/GLRenderer.hpp"
#include "threepp/scenes/Scene.hpp"
#include "threepp/renderers/RenderTarget.hpp"
#include "threepp/postprocessing/EffectComposer.hpp"
#include "threepp/postprocessing/RenderPass.hpp"
#include "threepp/postprocessing/GTAOPass.hpp"
#include "threepp/textures/DataTexture.hpp"
#include "chrononmotion/motion/CameraRig.hpp"
#include "threepp/motion/MotionBridge.hpp"
#include "threepp/motion/CinematicSceneLook.hpp"
#include "threepp/motion/ProceduralIdlePose.hpp"

using namespace threepp;

namespace {

void appendU16(std::vector<uint8_t>& buf, uint16_t v) {
    buf.push_back(static_cast<uint8_t>(v & 0xffu));
    buf.push_back(static_cast<uint8_t>((v >> 8) & 0xffu));
}

void appendU32(std::vector<uint8_t>& buf, uint32_t v) {
    for (unsigned shift = 0; shift < 32; shift += 8) {
        buf.push_back(static_cast<uint8_t>((v >> shift) & 0xffu));
    }
}

void appendFloat(std::vector<uint8_t>& buf, float v) {
    uint32_t bits = 0;
    std::memcpy(&bits, &v, sizeof(bits));
    appendU32(buf, bits);
}

std::vector<uint8_t> encodeCheckerPng(int size, int cells) {
    std::vector<uint8_t> rgba(size * size * 4);
    const int cell = size / cells;
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const bool hi = ((x / cell) + (y / cell)) % 2 == 0;
            const size_t idx = static_cast<size_t>(y * size + x) * 4;
            rgba[idx + 0] = hi ? 240 : 40;
            rgba[idx + 1] = hi ? 210 : 95;
            rgba[idx + 2] = hi ? 120 : 215;
            rgba[idx + 3] = 255;
        }
    }
    std::vector<uint8_t> png;
    auto writeFunc = [](void* context, void* data, int len) {
        auto* out = static_cast<std::vector<uint8_t>*>(context);
        const auto* bytes = static_cast<const uint8_t*>(data);
        out->insert(out->end(), bytes, bytes + len);
    };
    stbi_write_png_to_func(writeFunc, &png, size, size, 4, rgba.data(), size * 4);
    return png;
}

void writeSyntheticTexturedCubeGlb(const std::filesystem::path& path) {
    struct Face {
        float n[3];
        float v[4][3];
    };
    constexpr float h = 0.7f;
    const Face faces[6] = {
        {{0, 0, 1}, {{-h, -h, h}, {h, -h, h}, {h, h, h}, {-h, h, h}}},
        {{0, 0, -1}, {{h, -h, -h}, {-h, -h, -h}, {-h, h, -h}, {h, h, -h}}},
        {{0, 1, 0}, {{-h, h, h}, {h, h, h}, {h, h, -h}, {-h, h, -h}}},
        {{0, -1, 0}, {{-h, -h, -h}, {h, -h, -h}, {h, -h, h}, {-h, -h, h}}},
        {{1, 0, 0}, {{h, -h, h}, {h, -h, -h}, {h, h, -h}, {h, h, h}}},
        {{-1, 0, 0}, {{-h, -h, -h}, {-h, -h, h}, {-h, h, h}, {-h, h, -h}}},
    };
    const float uvs[4][2] = {{0.f, 0.f}, {1.f, 0.f}, {1.f, 1.f}, {0.f, 1.f}};

    std::vector<uint8_t> bin;
    // View 0: POSITION (24 * 3 floats = 288 bytes)
    for (const auto& f : faces) {
        for (int i = 0; i < 4; ++i) {
            appendFloat(bin, f.v[i][0]);
            appendFloat(bin, f.v[i][1]);
            appendFloat(bin, f.v[i][2]);
        }
    }
    const size_t posLen = bin.size(); // 288

    // View 1: NORMAL (24 * 3 floats = 288 bytes)
    for (const auto& f : faces) {
        for (int i = 0; i < 4; ++i) {
            appendFloat(bin, f.n[0]);
            appendFloat(bin, f.n[1]);
            appendFloat(bin, f.n[2]);
        }
    }
    const size_t normOffset = posLen;
    const size_t normLen = bin.size() - normOffset; // 288

    // View 2: TEXCOORD_0 (24 * 2 floats = 192 bytes)
    const size_t uvOffset = bin.size();
    for (int f = 0; f < 6; ++f) {
        for (int i = 0; i < 4; ++i) {
            appendFloat(bin, uvs[i][0]);
            appendFloat(bin, uvs[i][1]);
        }
    }
    const size_t uvLen = bin.size() - uvOffset; // 192

    // View 3: INDICES (36 * uint16 = 72 bytes)
    const size_t idxOffset = bin.size();
    for (uint16_t f = 0; f < 6; ++f) {
        const uint16_t base = f * 4;
        appendU16(bin, base + 0);
        appendU16(bin, base + 1);
        appendU16(bin, base + 2);
        appendU16(bin, base + 0);
        appendU16(bin, base + 2);
        appendU16(bin, base + 3);
    }
    const size_t idxLen = bin.size() - idxOffset; // 72

    // View 4: embedded PNG image
    while (bin.size() % 4 != 0) bin.push_back(0);
    const size_t imgOffset = bin.size();
    const std::vector<uint8_t> png = encodeCheckerPng(64, 8);
    bin.insert(bin.end(), png.begin(), png.end());
    const size_t imgLen = png.size();
    while (bin.size() % 4 != 0) bin.push_back(0);

    const std::string json =
        std::string(R"({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0]}],)") +
        R"("nodes":[{"name":"textured_cube","rotation":[0.1675188,0.2845147,-0.0505642,0.9425056],"mesh":0}],)" +
        R"("buffers":[{"byteLength":)" + std::to_string(bin.size()) + R"(}],)" +
        R"("bufferViews":[)" +
        R"({"buffer":0,"byteOffset":0,"byteLength":)" + std::to_string(posLen) + R"(},)" +
        R"({"buffer":0,"byteOffset":)" + std::to_string(normOffset) + R"(,"byteLength":)" + std::to_string(normLen) + R"(},)" +
        R"({"buffer":0,"byteOffset":)" + std::to_string(uvOffset) + R"(,"byteLength":)" + std::to_string(uvLen) + R"(},)" +
        R"({"buffer":0,"byteOffset":)" + std::to_string(idxOffset) + R"(,"byteLength":)" + std::to_string(idxLen) + R"(},)" +
        R"({"buffer":0,"byteOffset":)" + std::to_string(imgOffset) + R"(,"byteLength":)" + std::to_string(imgLen) + R"(}],)" +
        R"("accessors":[)" +
        R"({"bufferView":0,"componentType":5126,"count":24,"type":"VEC3","min":[-0.7,-0.7,-0.7],"max":[0.7,0.7,0.7]},)" +
        R"({"bufferView":1,"componentType":5126,"count":24,"type":"VEC3"},)" +
        R"({"bufferView":2,"componentType":5126,"count":24,"type":"VEC2"},)" +
        R"({"bufferView":3,"componentType":5123,"count":36,"type":"SCALAR"}],)" +
        R"("images":[{"bufferView":4,"mimeType":"image/png"}],)" +
        R"("samplers":[{"magFilter":9728,"minFilter":9728,"wrapS":10497,"wrapT":10497}],)" +
        R"("textures":[{"sampler":0,"source":0}],)" +
        R"("materials":[{"name":"checker_pbr","pbrMetallicRoughness":{"baseColorFactor":[1,1,1,1],"baseColorTexture":{"index":0},"metallicFactor":0.1,"roughnessFactor":0.8}}],)" +
        R"("meshes":[{"name":"cube_mesh","primitives":[{"attributes":{"POSITION":0,"NORMAL":1,"TEXCOORD_0":2},"indices":3,"material":0}]}]})";

    std::vector<uint8_t> paddedJson(json.begin(), json.end());
    while (paddedJson.size() % 4 != 0) paddedJson.push_back(static_cast<uint8_t>(' '));

    std::vector<uint8_t> glb;
    const uint32_t totalLen = static_cast<uint32_t>(12 + 8 + paddedJson.size() + 8 + bin.size());
    appendU32(glb, 0x46546C67u); // "glTF"
    appendU32(glb, 2u);
    appendU32(glb, totalLen);
    appendU32(glb, static_cast<uint32_t>(paddedJson.size()));
    appendU32(glb, 0x4E4F534Au); // "JSON"
    glb.insert(glb.end(), paddedJson.begin(), paddedJson.end());
    appendU32(glb, static_cast<uint32_t>(bin.size()));
    appendU32(glb, 0x004E4942u); // "BIN\0"
    glb.insert(glb.end(), bin.begin(), bin.end());

    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(glb.data()), static_cast<std::streamsize>(glb.size()));
}

bool endsWithGlbOrGltf(const std::string& s) {
    auto lower = s;
    for (char& c : lower) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return (lower.size() >= 4 && lower.substr(lower.size() - 4) == ".glb") ||
           (lower.size() >= 5 && lower.substr(lower.size() - 5) == ".gltf");
}

std::shared_ptr<DataTexture> makePaperTexture() {
    const std::filesystem::path sourcePath =
            std::filesystem::path(__FILE__).parent_path() / "assets/table_paper.png";
    int width = 0;
    int height = 0;
    int channels = 0;
    unsigned char* decoded = stbi_load(sourcePath.string().c_str(), &width, &height, &channels, 3);
    if (!decoded || width <= 0 || height <= 0) {
        std::cerr << "glb proof: failed to load paper texture " << sourcePath << "\n";
        if (decoded) stbi_image_free(decoded);
        return {};
    }
    std::vector<unsigned char> rgb(decoded, decoded + static_cast<size_t>(width) * height * 3);
    stbi_image_free(decoded);
    auto texture = DataTexture::create(std::move(rgb), width, height);
    texture->format = Format::RGB;
    texture->colorSpace = ColorSpace::sRGB;
    texture->generateMipmaps = true;
    texture->minFilter = Filter::LinearMipmapLinear;
    texture->magFilter = Filter::Linear;
    texture->needsUpdate();
    return texture;
}

std::shared_ptr<DataTexture> loadPaperOverlay(const std::filesystem::path& path) {
    int width = 0;
    int height = 0;
    int channels = 0;
    unsigned char* decoded = stbi_load(path.string().c_str(), &width, &height, &channels, 4);
    if (!decoded || width <= 0 || height <= 0) {
        std::cerr << "glb proof: failed to load paper overlay " << path << "\n";
        if (decoded) stbi_image_free(decoded);
        return {};
    }
    std::vector<unsigned char> rgba(decoded, decoded + static_cast<size_t>(width) * height * 4);
    stbi_image_free(decoded);
    auto texture = DataTexture::create(std::move(rgba), width, height);
    texture->format = Format::RGBA;
    texture->colorSpace = ColorSpace::sRGB;
    texture->generateMipmaps = true;
    texture->minFilter = Filter::LinearMipmapLinear;
    texture->magFilter = Filter::Linear;
    texture->needsUpdate();
    return texture;
}

Vector3 sampleSmoothPath(const std::vector<Vector3>& points, float progress) {
    if (points.size() < 2) return points.empty() ? Vector3() : points.front();
    const float scaled = std::clamp(progress, 0.f, 1.f) * static_cast<float>(points.size() - 1);
    const size_t segment = std::min(static_cast<size_t>(scaled), points.size() - 2);
    const float t = scaled - static_cast<float>(segment);
    const Vector3& p0 = points[segment == 0 ? 0 : segment - 1];
    const Vector3& p1 = points[segment];
    const Vector3& p2 = points[segment + 1];
    const Vector3& p3 = points[std::min(segment + 2, points.size() - 1)];
    const auto component = [t](float a, float b, float c, float d) {
        const float t2 = t * t;
        const float t3 = t2 * t;
        return 0.5f * (2.f * b + (-a + c) * t +
                (2.f * a - 5.f * b + 4.f * c - d) * t2 +
                (-a + 3.f * b - 3.f * c + d) * t3);
    };
    return Vector3(component(p0.x, p1.x, p2.x, p3.x),
                   component(p0.y, p1.y, p2.y, p3.y),
                   component(p0.z, p1.z, p2.z, p3.z));
}

std::string shellQuote(const std::string& value) {
    std::string quoted{"'"};
    for (const char c : value) {
        if (c == '\'') quoted += "'\\''";
        else quoted += c;
    }
    quoted += '\'';
    return quoted;
}

int renderImageFocusClip(const std::filesystem::path& imagePath,
                         const std::filesystem::path& outputPath) {
    if (!std::filesystem::exists(imagePath)) {
        std::cerr << "image-focus: source image not found: " << imagePath << "\n";
        return 2;
    }
    std::filesystem::create_directories(outputPath.parent_path());
    const std::string graph =
        "[0:v]crop=1220:600:0:0,split=2[plate][card0];"
        "[plate]scale=1920:1080:force_original_aspect_ratio=increase,crop=1920:1080,"
        "gblur=sigma=24,eq=brightness=-0.24:saturation=0.72[bg];"
        "[bg]drawbox=x=0:y=0:w=iw:h=ih:color=0x08090d@0.38:t=fill,"
        "drawtext=fontfile=/usr/share/fonts/truetype/dejavu/DejaVuSerif.ttf:text='GOOD FOOD':"
        "fontcolor=0xfff8e9:fontsize=118:x=(w-text_w)/2:y=(h-text_h)/2:"
        "alpha='0.90*min(max((t-1.9)/0.8,0),1)':"
        "borderw=10:bordercolor=0xffbf00@0.62:shadowcolor=0xffbf00@0.75:shadowx=0:shadowy=0[titled];"
        "[card0]scale=w='1450*(1+0.22*min(t/2,1))':h='713*(1+0.22*min(t/2,1))':eval=frame,"
        "rotate='0.025*sin(t*1.5)':ow=rotw(iw):oh=roth(ih):c=black@0,"
        "gblur=sigma=9:enable='between(t,2.2,4.2)'[card];"
        "[titled][card]overlay=x='(W-w)/2':"
        "y='135-1000*min(max((t-2.0)/2.8,0),1)':enable='lt(t,4.8)'[out]";
    const std::string command =
        "ffmpeg -hide_banner -loglevel warning -y -loop 1 -framerate 30 -i " + shellQuote(imagePath.string()) +
        " -t 7.5 -filter_complex " + shellQuote(graph) +
        " -map '[out]' -an -c:v libx264 -preset medium -crf 18 -pix_fmt yuv420p -r 30 -movflags +faststart " +
        shellQuote(outputPath.string());
    std::cout << "image-focus: rendering 1920x1080 at 30 fps through the native FFmpeg C/C++ media backend\n";
    const int status = std::system(command.c_str());
    return status == 0 ? 0 : 1;
}

int renderWordCameraClip(const std::filesystem::path& imagePath,
                         const std::filesystem::path& outputPath) {
    if (!std::filesystem::exists(imagePath)) {
        std::cerr << "word-camera: background image not found: " << imagePath << "\n";
        return 2;
    }
    std::filesystem::create_directories(outputPath.parent_path());
    const std::string font = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf";
    std::string graph =
        "[0:v]scale=1920:1080:force_original_aspect_ratio=increase,crop=1920:1080,"
        "gblur=sigma=2.8,eq=brightness=-0.22:saturation=0.78,"
        "drawbox=x=0:y=0:w=iw:h=ih:color=0x070a10@0.34:t=fill[base];"
        "[base]drawtext=fontfile=" + font + ":text='HOMES':fontcolor=white:fontsize=150:"
        "x='490-text_w/2':y=245:alpha='min(max(t/0.4,0),1)':borderw=6:"
        "bordercolor=0xffc400@0.60:shadowcolor=0xffc400@0.90:shadowx=0:shadowy=0[a];"
        "[a]drawbox=x=250:y=415:w=480:h=12:color=0xffc400@0.95:t=fill:enable='gte(t,0.25)'[b];"
        "[b]drawtext=fontfile=" + font + ":text='CAN':fontcolor=white:fontsize=150:"
        "x='1450-text_w/2':y=245:alpha='min(max((t-2.2)/0.4,0),1)':borderw=6:"
        "bordercolor=0xffc400@0.60:shadowcolor=0xffc400@0.90:shadowx=0:shadowy=0[c];"
        "[c]drawbox=x=1325:y=415:w=250:h=12:color=0xffc400@0.95:t=fill:enable='gte(t,2.45)'[d];"
        "[d]drawtext=fontfile=" + font + ":text='CHANGE':fontcolor=white:fontsize=150:"
        "x='490-text_w/2':y=715:alpha='min(max((t-4.45)/0.4,0),1)':borderw=6:"
        "bordercolor=0xffc400@0.60:shadowcolor=0xffc400@0.90:shadowx=0:shadowy=0[e];"
        "[e]drawbox=x=205:y=885:w=570:h=12:color=0xffc400@0.95:t=fill:enable='gte(t,4.7)'[f];"
        "[f]drawtext=fontfile=" + font + ":text='LIVES':fontcolor=white:fontsize=150:"
        "x='1450-text_w/2':y=715:alpha='min(max((t-6.6)/0.4,0),1)':borderw=6:"
        "bordercolor=0xffc400@0.60:shadowcolor=0xffc400@0.90:shadowx=0:shadowy=0[g];"
        "[g]drawbox=x=1270:y=885:w=360:h=12:color=0xffc400@0.95:t=fill:enable='gte(t,6.85)'[scene];"
        "[scene]zoompan=z='if(lt(on,260),1.72,1.72-0.72*min(max((on-260)/40,0),1))':"
        "x='if(lt(on,65),0,if(lt(on,80),894*(((on-65)/15)^2)*(3-2*((on-65)/15)),"
        "if(lt(on,130),894,if(lt(on,145),894*(1-(((on-130)/15)^2)*(3-2*((on-130)/15))),"
        "if(lt(on,195),0,if(lt(on,210),894*(((on-195)/15)^2)*(3-2*((on-195)/15)),"
        "if(lt(on,260),894,894*(1-min(max((on-260)/40,0),1)))))))))':"
        "y='if(lt(on,130),0,if(lt(on,145),470*(((on-130)/15)^2)*(3-2*((on-130)/15)),"
        "if(lt(on,195),470,if(lt(on,210),470*(1-(((on-195)/15)^2)*(3-2*((on-195)/15))),"
        "if(lt(on,260),470,470*(1-min(max((on-260)/40,0),1)))))))':"
        "d=1:s=1920x1080:fps=30[out]";
    const std::string command =
        "ffmpeg -hide_banner -loglevel warning -y -loop 1 -framerate 30 -i " + shellQuote(imagePath.string()) +
        " -t 10 -filter_complex " + shellQuote(graph) +
        " -map '[out]' -an -c:v libx264 -preset medium -crf 18 -pix_fmt yuv420p -r 30 -movflags +faststart " +
        shellQuote(outputPath.string());
    std::cout << "word-camera: tracking HOMES / CAN / CHANGE / LIVES and pulling out to the full scene\n";
    const int status = std::system(command.c_str());
    return status == 0 ? 0 : 1;
}

} // namespace

int main(int argc, char** argv) {
    // Opt-in framing control: --fit=<fraction> makes the subject occupy that
    // fraction of the frame. Without it the legacy proof framing is unchanged.
    std::vector<char*> filteredArgs{argv[0]};
    float fitFraction = 0.f;
    bool hasLockedCamera = false;
    float lockedCamera[6] = {0.f, 0.f, 0.f, 0.f, 0.f, 0.f};
    float lockedFov = 0.f;
    float exposureOverride = 0.f;
    std::uint32_t backgroundOverride = 0;
    bool hasBackgroundOverride = false;
    std::string animClipName;
    std::string animSequence;
    float animBlend = 0.2f;
    float animFps = 30.f;
    bool animLoop = false;
    bool listAnims = false;
    bool skipProof = false;
    bool printBounds = false;
    const auto parseTriple = [](const std::string& text, float* out) {
        std::stringstream stream(text);
        std::string item;
        int count = 0;
        while (std::getline(stream, item, ',') && count < 3) out[count++] = std::stof(item);
        return count == 3;
    };
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg.rfind("--fit=", 0) == 0) {
            fitFraction = std::stof(arg.substr(6));
            continue;
        }
        if (arg.rfind("--camera-pos=", 0) == 0 && parseTriple(arg.substr(13), lockedCamera)) {
            hasLockedCamera = true;
            continue;
        }
        if (arg.rfind("--camera-target=", 0) == 0 && parseTriple(arg.substr(16), lockedCamera + 3)) {
            hasLockedCamera = true;
            continue;
        }
        if (arg.rfind("--camera-fov=", 0) == 0) {
            lockedFov = std::stof(arg.substr(13));
            continue;
        }
        if (arg.rfind("--exposure=", 0) == 0) {
            exposureOverride = std::stof(arg.substr(11));
            continue;
        }
        if (arg.rfind("--background=", 0) == 0) {
            backgroundOverride = static_cast<std::uint32_t>(
                    std::stoul(arg.substr(13), nullptr, 16));
            hasBackgroundOverride = true;
            continue;
        }
        if (arg.rfind("--anim=", 0) == 0) {
            animClipName = arg.substr(7);
            continue;
        }
        if (arg.rfind("--anim-sequence=", 0) == 0) {
            animSequence = arg.substr(16);
            animClipName = animSequence.substr(0, animSequence.find(','));
            continue;
        }
        if (arg.rfind("--anim-blend=", 0) == 0) {
            animBlend = std::stof(arg.substr(13));
            continue;
        }
        if (arg.rfind("--anim-fps=", 0) == 0) {
            animFps = std::stof(arg.substr(11));
            continue;
        }
        if (arg == "--anim-loop") {
            animLoop = true;
            continue;
        }
        if (arg == "--list-anims") {
            listAnims = true;
            continue;
        }
        if (arg == "--no-proof") {
            skipProof = true;
            continue;
        }
        if (arg == "--print-bounds") {
            printBounds = true;
            continue;
        }
        filteredArgs.push_back(argv[i]);
    }
    if (hasLockedCamera) hasLockedCamera = lockedFov > 0.f;
    argc = static_cast<int>(filteredArgs.size());
    argv = filteredArgs.data();
    fitFraction = std::clamp(fitFraction, 0.f, 0.98f);

    if (argc == 4 && std::string(argv[1]) == "--word-camera") {
        return renderWordCameraClip(argv[2], argv[3]);
    }
    if (argc == 4 && std::string(argv[1]) == "--image-focus") {
        return renderImageFocusClip(argv[2], argv[3]);
    }
    std::filesystem::path glbPath;
    std::string outPpm = "glb.ppm";
    std::string cameraMove;
    int width = 256;
    int height = 256;
    int orbitFrames = 0;
    std::filesystem::path orbitDirectory;
    const bool pushInMode = argc >= 2 && std::string(argv[1]) == "--pushin";
    const bool sceneOrbitMode = argc >= 2 && std::string(argv[1]) == "--scene-orbit";
    const bool namedMoveMode = argc >= 2 && std::string(argv[1]) == "--move";
    const std::filesystem::path assetsDirectory = std::filesystem::path(__FILE__).parent_path() / "assets";
    bool cleanupGlb = false;

    if (namedMoveMode && argc >= 7) {
        cameraMove = argv[2];
        glbPath = argv[3];
        orbitDirectory = argv[4];
        orbitFrames = std::atoi(argv[5]);
        width = std::atoi(argv[6]);
        height = argc > 7 ? std::atoi(argv[7]) : width * 9 / 16;
        const std::vector<std::string> supportedMoves{
                "product_push", "orbit", "arc_left", "arc_right", "rise_reveal",
                "top_down", "lateral_slide", "dolly_arc", "crane_down", "slow_drift"};
        if (std::find(supportedMoves.begin(), supportedMoves.end(), cameraMove) == supportedMoves.end() ||
            orbitFrames < 2 || width < 1 || height < 1) {
            std::cerr << "usage: chrononmotion_render_glb --move <preset> model.glb frame_dir frames width [height]\n";
            return 2;
        }
        std::filesystem::create_directories(orbitDirectory);
    } else if (argc >= 6 && (std::string(argv[1]) == "--orbit" || pushInMode || sceneOrbitMode)) {
        glbPath = argv[2];
        orbitDirectory = argv[3];
        orbitFrames = std::atoi(argv[4]);
        width = std::atoi(argv[5]);
        height = argc > 6 ? std::atoi(argv[6]) : width * 9 / 16;
        if (orbitFrames < 2 || width < 1 || height < 1) {
            std::cerr << "usage: chrononmotion_render_glb --orbit|--pushin|--scene-orbit model.glb frame_dir frames width [height]\n";
            return 2;
        }
        std::filesystem::create_directories(orbitDirectory);
    } else if (!animClipName.empty() && argc >= 5) {
        // --anim=<clip> model.glb frame_dir frames width [height]
        glbPath = argv[1];
        orbitDirectory = argv[2];
        orbitFrames = std::atoi(argv[3]);
        width = std::atoi(argv[4]);
        height = argc > 5 ? std::atoi(argv[5]) : width * 9 / 16;
        if (orbitFrames < 1 || width < 1 || height < 1) {
            std::cerr << "usage: chrononmotion_render_glb --anim=<clip> model.glb frame_dir frames width [height]\n";
            return 2;
        }
        std::filesystem::create_directories(orbitDirectory);
    } else if (listAnims && argc >= 2 && endsWithGlbOrGltf(argv[1])) {
        glbPath = argv[1];
        outPpm.clear();
    } else if (argc >= 3 && endsWithGlbOrGltf(argv[1])) {
        glbPath = argv[1];
        outPpm = argv[2];
        if (argc > 3) width = std::atoi(argv[3]);
        if (argc > 4) height = std::atoi(argv[4]);
    } else {
        if (argc > 1) outPpm = argv[1];
        if (argc > 2) width = std::atoi(argv[2]);
        if (argc > 3) height = std::atoi(argv[3]);
        glbPath = std::filesystem::path(outPpm + ".synthetic.glb");
        writeSyntheticTexturedCubeGlb(glbPath);
        cleanupGlb = true;
    }

    GLTFLoader loader;
    auto result = loader.load(glbPath);
    if (cleanupGlb) {
        std::error_code ec;
        std::filesystem::remove(glbPath, ec);
    }
    if (!result || !result->scene) {
        std::cerr << "glb proof: failed to load " << glbPath << "\n";
        return 1;
    }
    std::cout << "glb proof: loaded scene with " << result->scene->children.size() << " root children\n";
    if (listAnims) {
        for (std::size_t i = 0; i < result->animations.size(); ++i) {
            const auto& clip = result->animations[i];
            std::cout << i << ": " << clip->name() << " (" << clip->getDuration() << "s)\n";
        }
        return result->animations.empty() ? 1 : 0;
    }

    Canvas canvas(Canvas::Parameters().size(width, height).headless(true));
    GLRenderer renderer(canvas);
    renderer.setClearColor(Color(pushInMode ? 0x202020 : 0x393633));
    if (hasBackgroundOverride) renderer.setClearColor(Color(backgroundOverride));
    auto modelScene = result->scene;

    // Deterministic offline playback: one mixer anchored at clip time zero, so
    // any frame can be rendered without depending on the previous one.
    std::unique_ptr<AnimationMixer> clipMixer;
    std::shared_ptr<AnimationClip> activeClip;

    // A sequence chains clips back to back, overlapping each boundary by
    // animBlend seconds. Every action is set to an absolute local time and an
    // explicit weight, then composited with update(0), so any frame can be
    // rendered on its own and the result never depends on frame order.
    struct SequenceStep {
        AnimationAction* action = nullptr;
        float start = 0.f;
        float duration = 0.f;
    };
    std::vector<SequenceStep> sequenceSteps;
    float sequenceDuration = 0.f;

    if (!animSequence.empty()) {
        std::vector<std::shared_ptr<AnimationClip>> sequenceClips;
        std::stringstream names(animSequence);
        std::string name;
        while (std::getline(names, name, ',')) {
            if (name.empty()) continue;
            const auto clip = AnimationClip::findByName(result->animations, name);
            if (!clip) {
                std::cerr << "glb anim: clip '" << name << "' not found in sequence; available:";
                for (const auto& candidate : result->animations) std::cerr << " " << candidate->name();
                std::cerr << "\n";
                return 1;
            }
            sequenceClips.push_back(clip);
        }
        if (sequenceClips.empty()) {
            std::cerr << "glb anim: empty --anim-sequence\n";
            return 1;
        }
        std::shared_ptr<AnimationClip> shortest = *std::min_element(sequenceClips.begin(), sequenceClips.end(),
                [](const auto& a, const auto& b) { return a->getDuration() < b->getDuration(); });
        float blend = std::max(animBlend, 0.f);
        if (sequenceClips.size() > 1)
            blend = std::min(blend, std::max(shortest->getDuration() * 0.5f, 0.f));
        else
            blend = 0.f;
        clipMixer = std::make_unique<AnimationMixer>(*modelScene);
        float cursor = 0.f;
        for (std::size_t i = 0; i < sequenceClips.size(); ++i) {
            SequenceStep step;
            step.action = clipMixer->clipAction(sequenceClips[i]);
            step.action->play().setEffectiveWeight(i == 0 ? 1.f : 0.f).setLocalTime(0.f);
            step.start = cursor;
            step.duration = std::max(sequenceClips[i]->getDuration(), 0.f);
            sequenceSteps.push_back(step);
            cursor += step.duration - (i + 1 < sequenceClips.size() ? blend : 0.f);
        }
        sequenceDuration = std::max(cursor, 0.f);
        activeClip = sequenceClips.front();
        clipMixer->update(0.f);
        std::cout << "glb anim: sequence=";
        for (std::size_t i = 0; i < sequenceSteps.size(); ++i)
            std::cout << (i ? " -> " : "") << sequenceClips[i]->name()
                      << "(" << sequenceSteps[i].duration << "s)";
        std::cout << " duration=" << sequenceDuration << "s blend=" << blend << "s\n";
    }

    if (!animClipName.empty() && animSequence.empty()) {
        activeClip = AnimationClip::findByName(result->animations, animClipName);
        if (!activeClip) {
            std::cerr << "glb anim: clip '" << animClipName << "' not found; available:";
            for (const auto& clip : result->animations) std::cerr << " " << clip->name();
            std::cerr << "\n";
            return 1;
        }
        clipMixer = std::make_unique<AnimationMixer>(*modelScene);
        clipMixer->clipAction(activeClip)->play();
        clipMixer->setTime(0.f);
        std::cout << "glb anim: clip=" << activeClip->name()
                  << " duration=" << activeClip->getDuration() << "s fps=" << animFps << "\n";
    }

    // Compose the proof around the imported object's actual bounds rather than
    // assuming that its origin is its visual center. This keeps ordinary GLBs
    // inside frame and gives the preview an elevated three-quarter angle.
    modelScene->updateMatrixWorld(true);
    const Box3 bounds = Box3().setFromObject(*modelScene, true);
    const Vector3 objectCenter = bounds.getCenter();
    const Vector3 size = bounds.getSize();
    const float span = std::max({size.x, size.y, size.z});

    const float centeredTop = bounds.max().y - objectCenter.y;
    const float centeredBottom = bounds.min().y - objectCenter.y;
    if (printBounds) {
        std::cout << "glb bounds: center=" << objectCenter.x << "," << objectCenter.y << ","
                  << objectCenter.z << " size=" << size.x << "," << size.y << "," << size.z
                  << " span=" << span << std::endl;
    }
    const Vector3 pivot(objectCenter.x, 0.f, objectCenter.z);
    auto scene = Scene::create();
    if (pushInMode) modelScene->position.y -= objectCenter.y;
    scene->add(modelScene);
    std::shared_ptr<MeshBasicMaterial> paperMaterial;
    std::shared_ptr<Mesh> paperMesh;
    std::array<std::shared_ptr<MeshBasicMaterial>, 5> animatedMastheadMaterials{};
    if (pushInMode) {
        auto floor = Mesh::create(PlaneGeometry::create(80.f, 80.f),
                MeshBasicMaterial::create(MeshBasicMaterial::Params().color(Color(0x303030))));
        floor->material()->toneMapped = false;
        floor->rotation.x = -1.57079632679f;
        floor->position.set(pivot.x, centeredBottom - 0.025f, pivot.z);
        scene->add(floor);
        auto wall = Mesh::create(PlaneGeometry::create(80.f, 28.f),
                MeshBasicMaterial::create(MeshBasicMaterial::Params().color(Color(0x242424))));
        wall->material()->toneMapped = false;
        wall->position.set(pivot.x, centeredTop + 4.f, pivot.z - span * 0.95f);
        scene->add(wall);

        const float wallZ = pivot.z - span * 0.95f;
        auto windowFrame = Mesh::create(PlaneGeometry::create(3.15f, 2.55f),
                MeshBasicMaterial::create(MeshBasicMaterial::Params().color(Color(0x3a3a3a))));
        windowFrame->material()->toneMapped = false;
        windowFrame->position.set(pivot.x, centeredTop + 2.5f, wallZ + 0.025f);
        scene->add(windowFrame);
        auto windowGlass = Mesh::create(PlaneGeometry::create(2.85f, 2.25f),
                MeshBasicMaterial::create(MeshBasicMaterial::Params().color(Color(0x707070))));
        windowGlass->material()->toneMapped = false;
        windowGlass->position.set(pivot.x, centeredTop + 2.5f, wallZ + 0.045f);
        scene->add(windowGlass);
        for (const auto& [x, y, w, h] : std::array<std::array<float, 4>, 4>{
                     std::array<float, 4>{0.f, 0.f, 0.07f, 2.2f},
                     std::array<float, 4>{0.f, 0.f, 2.8f, 0.07f},
                     std::array<float, 4>{-1.4f, 0.f, 0.06f, 2.2f},
                     std::array<float, 4>{1.4f, 0.f, 0.06f, 2.2f}}) {
            auto bar = Mesh::create(PlaneGeometry::create(w, h),
                    MeshBasicMaterial::create(MeshBasicMaterial::Params().color(Color(0x292929))));
            bar->material()->toneMapped = false;
            bar->position.set(pivot.x + x, centeredTop + 2.5f + y, wallZ + 0.065f);
            scene->add(bar);
        }

        paperMaterial = MeshBasicMaterial::create(
                MeshBasicMaterial::Params().map(makePaperTexture()).color(Color(0xffffff)));
        paperMaterial->transparent = true;
        paperMaterial->side = Side::Double;
        paperMaterial->depthWrite = false;
        paperMaterial->opacity = 0.f;
        paperMesh = Mesh::create(PlaneGeometry::create(1.85f, 1.35f), paperMaterial);
        paperMesh->rotation.x = -1.57079632679f;
        paperMesh->position.set(pivot.x, centeredTop + 0.08f, pivot.z);
        paperMesh->visible = true;
        scene->add(paperMesh);

        const std::array<std::string, 5> mastheadAssets{
                "sony_letter_S.png", "sony_letter_O.png", "sony_letter_N.png",
                "sony_letter_Y.png", "sony_masthead_info.png"};
        for (size_t i = 0; i < mastheadAssets.size(); ++i) {
            auto map = loadPaperOverlay(assetsDirectory / mastheadAssets[i]);
            if (!map) return 1;
            auto material = MeshBasicMaterial::create(MeshBasicMaterial::Params()
                    .map(map).color(Color(0xffffff)));
            material->transparent = true;
            material->depthWrite = false;
            material->toneMapped = false;
            material->opacity = 0.f;
            material->side = Side::Double;
            auto lettering = Mesh::create(PlaneGeometry::create(1.85f, 1.35f), material);
            lettering->rotation.x = -1.57079632679f;
            lettering->position.set(pivot.x, centeredTop + 0.083f, pivot.z);
            scene->add(lettering);
            animatedMastheadMaterials[i] = material;
        }
    }

    // Preserve GLTFLoader's imported materials and textures. This proof is meant
    // to exercise the actual imported PBR path rather than replacing it with a
    // simpler material for a brighter screenshot.
    int meshCount = 0;
    int standardMaterials = 0;
    int textured = 0;
    modelScene->traverse([&](Object3D& node) {
        if (auto* mesh = dynamic_cast<Mesh*>(&node)) {
            ++meshCount;
            if (auto stdMat = mesh->materialAs<MeshStandardMaterial>()) {
                ++standardMaterials;
                if (stdMat->map) ++textured;
                if (pushInMode) {
                    stdMat->color.multiplyScalar(0.82f);
                    stdMat->roughness = 0.95f;
                    stdMat->metalness = 0.f;
                }
            }
        }
    });
    std::cout << "glb proof: meshes=" << meshCount
              << " standard_materials=" << standardMaterials
              << " textured_standard_materials=" << textured << "\n";
    if (meshCount == 0) {
        std::cerr << "glb proof: scene contained no meshes\n";
        return 1;
    }

    if (sceneOrbitMode) {
        motion::CinematicSceneLookOptions look;
        look.ambientIntensity = 2.0f;
        look.hemisphereIntensity = 1.55f;
        look.keyIntensity = 58.f;
        look.keyAngleRadians = 0.78f;
        look.enableRimFill = true;
        look.rimFillIntensity = 2.5f;
        look.enableDepthFog = true;
        look.fogDensityPerEnvironmentRadius = 0.018f;
        motion::applyCinematicSceneLook(*scene, renderer,
                Vector3(objectCenter.x, 1.0f, objectCenter.z), 1.9f, span, look);
    } else {
        scene->add(AmbientLight::create(Color(0xffffff), pushInMode ? 2.1f : 8.0f));
        auto light = DirectionalLight::create(pushInMode ? Color(0xe0e0e0) : Color(0xffe4c4), pushInMode ? 5.0f : 24.0f);
        light->position.set(objectCenter.x - span * 1.4f,
                            pushInMode ? centeredTop + span * 1.7f : objectCenter.y + span * 1.8f,
                            pushInMode ? objectCenter.z - span * 1.2f : objectCenter.z + span * 1.1f);
        auto lightTarget = Group::create();
        lightTarget->position.set(pivot.x, pushInMode ? centeredTop : objectCenter.y, pivot.z);
        scene->add(lightTarget);
        light->setTarget(*lightTarget);
        scene->add(light);
    }

    // The push-in proof keeps its specialized tabletop key. Production
    // documentary scenes use the reusable scene-look lighting above.
    if (pushInMode) {
        auto overhead = SpotLight::create(Color(0xfff1dc), 320.f, span * 6.f, 0.95f, 0.72f, 2.f);
        overhead->position.set(pivot.x - span * 0.08f, centeredTop + span * 1.45f,
                               pivot.z + span * 0.12f);
        overhead->radius = span * 0.12f;
        auto overheadTarget = Group::create();
        overheadTarget->position.set(pivot.x, centeredTop, pivot.z);
        scene->add(overheadTarget);
        overhead->setTarget(*overheadTarget);
        scene->add(overhead);
    }

    const float aspect = float(width) / float(height);
    const float fovDegrees = sceneOrbitMode ? 48.f : (cameraMove.empty() ? 30.f : 50.f);
    float distance = cameraMove.empty()
            ? std::max(span * (sceneOrbitMode ? 0.31f : 2.7f), 1.f)
            : std::max(size.x, size.y * aspect) /
                      (2.f * std::tan(50.f * 0.5f * 3.14159265359f / 180.f) * aspect * 0.48f);
    if (fitFraction > 0.f) {
        const float tanHalfFov = std::tan(fovDegrees * 0.5f * 3.14159265359f / 180.f);
        const float requiredHeight = std::max(size.y, size.x / std::max(aspect, 1e-4f));
        distance = requiredHeight / (2.f * tanHalfFov * fitFraction);
    }
    // A fitted camera can sit very close to a small model, so the fixed 0.1 near
    // plane would clip it away entirely. Scale the planes with the distance.
    const float nearPlane = fitFraction > 0.f ? std::max(0.0005f, distance * 0.02f) : 0.1f;
    const float farPlane = fitFraction > 0.f ? std::max(10.f, distance * 100.f) : 2000.f;
    auto camera = PerspectiveCamera::create(fovDegrees, aspect, nearPlane, farPlane);
    std::vector<Vector3> cameraPath;
    std::vector<Vector3> targetPath;
    chrononmotion::motion::Easing cameraTiming = chrononmotion::motion::Easing::easeInOut();
    chrononmotion::motion::CameraPose pose;
    const Vector3 orbitTarget(objectCenter.x, sceneOrbitMode ? 1.0f : objectCenter.y, objectCenter.z);
    std::unique_ptr<motion::ProceduralIdlePose> idlePose;
    if (sceneOrbitMode) {
        if (auto* placement = modelScene->getObjectByName("MannequinPlacement")) {
            idlePose = std::make_unique<motion::ProceduralIdlePose>(*placement,
                    motion::ProceduralIdlePose::Options{
                            .cycleSeconds = 8.5f,
                            .yawRadians = 0.018f,
                            .leanRadians = 0.009f,
                            .bobAmplitude = 0.0035f,
                            .phaseRadians = 0.35f});
            std::cout << "glb proof: procedural idle enabled on MannequinPlacement\n";
        } else {
            std::cerr << "glb proof: scene has no MannequinPlacement; idle motion skipped\n";
        }
    }
    pose.position = sceneOrbitMode
            ? chrononmotion::Vector3(orbitTarget.x + distance * 0.30f,
                                     orbitTarget.y + distance * 0.20f,
                                     orbitTarget.z + distance * 0.97f)
            : cameraMove.empty()
            ? chrononmotion::Vector3(objectCenter.x + distance * 0.58f,
                                     objectCenter.y + distance * 0.48f,
                                     objectCenter.z + distance * 0.66f)
            : chrononmotion::Vector3(objectCenter.x, objectCenter.y + distance * 0.47f,
                                     objectCenter.z + distance * 0.883f);
    // CameraPose defaults to a 60 degree lens, which would otherwise override the
    // camera's own fov and shrink an explicitly fitted subject.
    if (fitFraction > 0.f) pose.fov = fovDegrees;
    if (hasLockedCamera) {
        pose.position = chrononmotion::Vector3(lockedCamera[0], lockedCamera[1], lockedCamera[2]);
        pose.target = chrononmotion::Vector3(lockedCamera[3], lockedCamera[4], lockedCamera[5]);
        pose.hasTarget = true;
        pose.fov = lockedFov;
    }
    chrononmotion::motion::CameraRig::lookAtPose(pose, chrononmotion::Vector3(orbitTarget.x, orbitTarget.y, orbitTarget.z));
    if (hasLockedCamera) chrononmotion::motion::CameraRig::lookAtPose(pose, pose.target);
    if (printBounds) {
        std::cout << "glb camera: pos=" << pose.position.x << "," << pose.position.y << ","
                  << pose.position.z << " target=" << orbitTarget.x << "," << orbitTarget.y
                  << "," << orbitTarget.z << " fov=" << pose.fov
                  << " distance=" << distance << std::endl;
    }
    renderer.toneMapping = ToneMapping::ACESFilmic;
    renderer.toneMappingExposure = (pushInMode || sceneOrbitMode) ? 1.0f : 2.0f;
    if (exposureOverride > 0.f) renderer.toneMappingExposure = exposureOverride;

    std::unique_ptr<EffectComposer> composer;
    std::unique_ptr<RenderTarget> target;
    if (sceneOrbitMode) {
        composer = std::make_unique<EffectComposer>(renderer);
        composer->renderToScreen = false;
        composer->setSize(static_cast<unsigned int>(width), static_cast<unsigned int>(height));
        composer->addPass(std::make_shared<RenderPass>(*scene, *camera));
        auto gtao = std::make_shared<GTAOPass>(*scene, *camera,
                static_cast<unsigned int>(width), static_cast<unsigned int>(height),
                GTAOParameters{.radius = 0.16f, .scale = 1.15f, .samples = 8},
                PoissonDenoiseParameters{.radius = 6.f, .samples = 8});
        composer->addPass(gtao);
    } else {
        target = RenderTarget::create(width, height, RenderTarget::Options{});
        renderer.setRenderTarget(target.get());
    }
    if (pushInMode) {
        cameraPath = {
                Vector3(pivot.x + 8.f, 3.2f, pivot.z + 12.f),
                Vector3(pivot.x + 7.f, 4.8f, pivot.z + 10.5f),
                Vector3(pivot.x + 4.f, 7.2f, pivot.z + 8.f),
                Vector3(pivot.x + 1.4f, 9.1f, pivot.z + 5.2f),
                Vector3(pivot.x + 0.35f, centeredTop + 3.4f, pivot.z + 2.8f),
                Vector3(pivot.x, centeredTop + 2.35f, pivot.z + 2.5f)};
        targetPath = {
                Vector3(pivot.x, 0.1f, pivot.z),
                Vector3(pivot.x, 0.25f, pivot.z),
                Vector3(pivot.x, 0.75f, pivot.z),
                Vector3(pivot.x, centeredTop, pivot.z),
                Vector3(pivot.x, centeredTop, pivot.z),
                Vector3(pivot.x, centeredTop, pivot.z)};
    }
    const int renderCount = orbitFrames > 0 ? orbitFrames : 1;
    for (int frame = 0; frame < renderCount; ++frame) {
        if (orbitFrames > 0 && !clipMixer) {
            const float linearProgress = float(frame) / float(orbitFrames - 1);
            const float frameTime = linearProgress * (sceneOrbitMode ? 12.f : 6.f);
            chrononmotion::motion::CameraPose framePose;
            if (!cameraMove.empty()) {
                const auto gentleOut = chrononmotion::motion::Easing::easeOut();
                const float eased = (cameraMove == "rise_reveal" || cameraMove == "crane_down")
                        ? gentleOut(linearProgress) : cameraTiming(linearProgress);
                framePose = pose;
                const chrononmotion::Vector3 pivot(objectCenter.x, objectCenter.y, objectCenter.z);
                const float frameWidth = distance * 2.f * std::tan(50.f * 0.5f * 3.14159265359f / 180.f) * aspect;
                const float frameHeight = frameWidth / aspect;
                const auto degrees = [](float value) { return value * 3.14159265359f / 180.f; };
                if (cameraMove == "product_push") {
                    chrononmotion::motion::CameraRig::dolly(framePose, distance * (-0.14f + 0.26f * eased));
                } else if (cameraMove == "orbit") {
                    chrononmotion::motion::CameraRig::orbitAround(framePose, pivot,
                            degrees(-24.f + 48.f * eased), 0.f);
                } else if (cameraMove == "arc_left") {
                    chrononmotion::motion::CameraRig::orbitAround(framePose, pivot,
                            degrees(8.f - 30.f * eased), 0.f);
                    chrononmotion::motion::CameraRig::dolly(framePose, distance * (-0.04f + 0.04f * eased));
                } else if (cameraMove == "arc_right") {
                    chrononmotion::motion::CameraRig::orbitAround(framePose, pivot,
                            degrees(-8.f + 30.f * eased), 0.f);
                    chrononmotion::motion::CameraRig::dolly(framePose, distance * (-0.04f + 0.04f * eased));
                } else if (cameraMove == "rise_reveal") {
                    chrononmotion::motion::CameraRig::orbitAround(framePose, pivot, 0.f,
                            degrees(-12.f + 15.f * eased));
                    chrononmotion::motion::CameraRig::pan(framePose,
                            chrononmotion::Vector2(0.f, -frameHeight * (0.10f - 0.125f * eased)));
                } else if (cameraMove == "top_down") {
                    // The base showcase view is already elevated by 28 degrees.
                    // Add 10–54 degrees to finish at the preset's 82 degree top view.
                    chrononmotion::motion::CameraRig::orbitAround(framePose, pivot, 0.f,
                            degrees(10.f + 44.f * eased));
                    chrononmotion::motion::CameraRig::dolly(framePose, distance * (-0.08f - 0.10f * eased));
                } else if (cameraMove == "lateral_slide") {
                    const float lateral = frameWidth * (-0.12f + 0.24f * eased);
                    chrononmotion::motion::CameraRig::pan(framePose, chrononmotion::Vector2(lateral, 0.f));
                    chrononmotion::motion::CameraRig::orbitAround(framePose, framePose.target,
                            degrees(5.f - 10.f * eased), 0.f);
                } else if (cameraMove == "dolly_arc") {
                    chrononmotion::motion::CameraRig::dolly(framePose, distance * (-0.22f + 0.40f * eased));
                    chrononmotion::motion::CameraRig::orbitAround(framePose, pivot,
                            degrees(-4.f + 8.f * eased), 0.f);
                } else if (cameraMove == "crane_down") {
                    const float vertical = frameHeight * (0.16f - 0.20f * eased);
                    chrononmotion::motion::CameraRig::pan(framePose, chrononmotion::Vector2(0.f, vertical));
                    chrononmotion::motion::CameraRig::orbitAround(framePose, pivot,
                            0.f, degrees(16.f - 22.f * eased));
                } else if (cameraMove == "slow_drift") {
                    chrononmotion::motion::CameraRig::pan(framePose,
                            chrononmotion::Vector2(frameWidth * (-0.045f + 0.09f * eased), 0.f));
                    chrononmotion::motion::CameraRig::orbitAround(framePose, pivot,
                            degrees(-5.f + 10.f * eased), 0.f);
                    chrononmotion::motion::CameraRig::roll(framePose, degrees(-1.5f + 3.f * eased));
                }
            } else if (pushInMode) {
                const float linearProgress = std::clamp(frameTime / 6.f, 0.f, 1.f);
                const float progress = cameraTiming(linearProgress);
                const Vector3 cameraPoint = sampleSmoothPath(cameraPath, progress);
                const Vector3 targetPoint = sampleSmoothPath(targetPath, progress);
                framePose.position = chrononmotion::Vector3(cameraPoint.x, cameraPoint.y, cameraPoint.z);
                framePose.target = chrononmotion::Vector3(targetPoint.x, targetPoint.y, targetPoint.z);
                framePose.hasTarget = true;
                framePose.fov = 46.f + (24.f - 46.f) * progress;
                chrononmotion::motion::CameraRig::lookAtPose(framePose, framePose.target);
                paperMesh->visible = true;
                paperMaterial->opacity = std::clamp((frameTime - 3.1f) / 0.55f, 0.f, 1.f);
                for (size_t i = 0; i < 4; ++i) {
                    const float letterStart = 4.05f + static_cast<float>(i) * 0.24f;
                    animatedMastheadMaterials[i]->opacity =
                            std::clamp((frameTime - letterStart) / 0.34f, 0.f, 1.f);
                }
                animatedMastheadMaterials[4]->opacity =
                        std::clamp((frameTime - 5.05f) / 0.48f, 0.f, 1.f);
                pose = framePose;
            } else {
                const float yaw = sceneOrbitMode
                        ? (-48.f + (float(frame) / float(orbitFrames - 1)) * 96.f) * 3.14159265359f / 180.f
                        : (float(frame) / float(orbitFrames - 1)) * 6.28318530718f;
                framePose = pose;
                chrononmotion::motion::CameraRig::orbitAround(framePose,
                        chrononmotion::Vector3(orbitTarget.x, orbitTarget.y, orbitTarget.z), yaw, 0.f);
            }
            threepp::motion::applyCameraPose(framePose, *camera, nullptr, nearPlane, farPlane);
            if (idlePose) idlePose->apply(frameTime);
        } else {
            threepp::motion::applyCameraPose(pose, *camera, nullptr, nearPlane, farPlane);
            if (idlePose) idlePose->apply(0.f);
        }
        if (clipMixer && !sequenceSteps.empty()) {
            float sequenceTime = float(frame) / std::max(animFps, 1e-3f);
            sequenceTime = (animLoop && sequenceDuration > 0.f)
                    ? std::fmod(sequenceTime, sequenceDuration)
                    : std::min(sequenceTime, sequenceDuration);
            float blend = std::max(animBlend, 0.f);
            if (sequenceSteps.size() > 1) {
                float shortest = sequenceSteps.front().duration;
                for (const auto& step : sequenceSteps) shortest = std::min(shortest, step.duration);
                blend = std::min(blend, std::max(shortest * 0.5f, 1e-4f));
            } else {
                blend = 0.f;
            }
            std::vector<float> weights(sequenceSteps.size(), 0.f);
            float total = 0.f;
            for (std::size_t i = 0; i < sequenceSteps.size(); ++i) {
                const auto& step = sequenceSteps[i];
                const float local = sequenceTime - step.start;
                if (local < -1e-4f || local > step.duration + 1e-4f) {
                    // Outside its window the action must contribute nothing, not
                    // keep the weight it was last given.
                    step.action->setEffectiveWeight(0.f);
                    continue;
                }
                float weight = 1.f;
                if (blend > 0.f) {
                    if (i > 0) weight = std::min(weight, local / blend);
                    weight = std::min(weight, (step.duration - local) / blend);
                }
                weight = std::clamp(weight, 0.f, 1.f);
                if (weight <= 0.f) {
                    step.action->setEffectiveWeight(0.f);
                    continue;
                }
                step.action->setEffectiveWeight(weight);
                step.action->setLocalTime(std::clamp(local, 0.f, step.duration));
                weights[i] = weight;
                total += weight;
            }
            if (total > 0.f && std::abs(total - 1.f) > 1e-4f) {
                for (std::size_t i = 0; i < sequenceSteps.size(); ++i) {
                    if (weights[i] > 0.f)
                        sequenceSteps[i].action->setEffectiveWeight(weights[i] / total);
                }
            }
            clipMixer->update(0.f);
        } else if (clipMixer) {
            const float duration = std::max(activeClip->getDuration(), 0.f);
            float clipTime = float(frame) / std::max(animFps, 1e-3f);
            clipTime = (animLoop && duration > 0.f)
                    ? std::fmod(clipTime, duration)
                    : std::min(clipTime, duration);
            clipMixer->setTime(clipTime);
        }
        scene->updateMatrixWorld(true);
        std::vector<unsigned char> pixels;
        if (composer) {
            composer->render(1.f / 30.f);
            pixels = composer->readRGBPixels();
        } else {
            renderer.render(*scene, *camera);
            pixels = renderer.readRGBPixels();
        }
        if (int(pixels.size()) != width * height * 3) {
            std::cerr << "glb proof: readback size " << pixels.size() << "\n";
            return 1;
        }

    const auto pixelAt = [&](int x, int y) -> uint32_t {
        const size_t i = static_cast<size_t>(y * width + x) * 3;
        return (uint32_t(pixels[i]) << 16) | (uint32_t(pixels[i + 1]) << 8) | uint32_t(pixels[i + 2]);
    };
    const uint32_t corner = pixelAt(2, 2);
    const uint32_t centerPixel = pixelAt(width / 2, height / 2);
    std::set<uint32_t> centerPatchColors;
    const int patchStep = pushInMode ? 4 : 1;
    const int xBegin = pushInMode ? 0 : width / 3;
    const int xEnd = pushInMode ? width : (2 * width) / 3;
    const int yBegin = pushInMode ? 0 : height / 3;
    const int yEnd = pushInMode ? height : (2 * height) / 3;
    for (int y = yBegin; y < yEnd; y += patchStep) {
        for (int x = xBegin; x < xEnd; x += patchStep) {
            const uint32_t c = pixelAt(x, y);
            if (c != corner) centerPatchColors.insert(c);
        }
    }
    std::cout << "glb proof: center=0x" << std::hex << centerPixel << " corner=0x" << corner << std::dec
              << " distinct_lit_colors=" << centerPatchColors.size() << "\n";
    const bool weakCoverage = pushInMode
            ? centerPatchColors.size() < 8
            : (cameraMove.empty() && !sceneOrbitMode
                    ? centerPixel == corner || centerPatchColors.size() < 2
                    : centerPatchColors.size() < 8);
    if (weakCoverage) {
        std::cerr << "glb proof: expected non-background textured pixels in center region\n";
        if (!skipProof) return 1;
        std::cerr << "glb proof: coverage assertion waived by --no-proof\n";
    }

    const auto output = orbitFrames > 0
            ? orbitDirectory / ("frame_" + std::string(6 - std::min(6, int(std::to_string(frame).size())), '0') + std::to_string(frame) + ".ppm")
            : std::filesystem::path(outPpm);
    std::ofstream ppm(output, std::ios::binary);
    ppm << "P6\n" << width << " " << height << "\n255\n";
    for (int y = height - 1; y >= 0; --y) {
        ppm.write(reinterpret_cast<const char*>(&pixels[y * width * 3]), width * 3);
    }
    std::cout << "glb proof: wrote " << output << "\n";
    }

    renderer.setRenderTarget(nullptr);
    renderer.dispose();
    if (target) renderer.setRenderTarget(nullptr);
    return 0;
}
