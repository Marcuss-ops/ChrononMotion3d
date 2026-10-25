// Chroma-keyed image puppets skinned to real joint chains and rendered headlessly.
// Modes: hand (finger flex), head (neck-attached sway), wave (connected arm wave).

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include "chrononmotion/geometries/PlaneGeometry.hpp"
#include "threepp/cameras/OrthographicCamera.hpp"
#include "threepp/canvas/Canvas.hpp"
#include "threepp/animation/AnimationMixer.hpp"
#include "threepp/cameras/PerspectiveCamera.hpp"
#include "threepp/core/BufferAttribute.hpp"
#include "threepp/geometries/PlaneGeometry.hpp"
#include "threepp/lights/AmbientLight.hpp"
#include "threepp/lights/DirectionalLight.hpp"
#include "threepp/loaders/GLTFLoader.hpp"
#include "threepp/loaders/ImageLoader.hpp"
#include "threepp/materials/MeshBasicMaterial.hpp"
#include "threepp/objects/Bone.hpp"
#include "threepp/objects/Group.hpp"
#include "threepp/objects/SkinnedMesh.hpp"
#include "threepp/objects/Skeleton.hpp"
#include "threepp/renderers/GLRenderer.hpp"
#include "threepp/renderers/RenderTarget.hpp"
#include "threepp/scenes/Scene.hpp"
#include "threepp/textures/Texture.hpp"

namespace {
using namespace threepp;
constexpr float kPi = 3.14159265358979323846f;

struct Joint { float x, y; };
struct Segment { Joint a, b; float radius; int boneA, boneB; };

float smooth(float a, float b, float x) {
    const float t = std::clamp((x - a) / (b - a), 0.f, 1.f);
    return t * t * (3.f - 2.f * t);
}

float segmentDistance(Joint p, Joint a, Joint b, float& u) {
    const float dx = b.x - a.x, dy = b.y - a.y;
    const float den = dx * dx + dy * dy;
    u = den > 1e-6f ? std::clamp(((p.x - a.x) * dx + (p.y - a.y) * dy) / den, 0.f, 1.f) : 0.f;
    return std::hypot(p.x - (a.x + u * dx), p.y - (a.y + u * dy));
}

bool insidePolygon(float x, float y, const std::vector<Joint>& poly) {
    bool inside=false;
    for (std::size_t i=0,j=poly.size()-1;i<poly.size();j=i++) {
        const auto& a=poly[i]; const auto& b=poly[j];
        if (((a.y>y)!=(b.y>y)) && x<(b.x-a.x)*(y-a.y)/(b.y-a.y+1e-7f)+a.x) inside=!inside;
    }
    return inside;
}

std::shared_ptr<Bone> addBone(const std::shared_ptr<Bone>& parent, const std::string& name,
                              float x, float y, float unit, std::vector<std::shared_ptr<Bone>>& bones) {
    auto bone = Bone::create();
    bone->name = name;
    bone->position.set(x * unit, y * unit, 0.f);
    if (parent) parent->add(bone);
    bones.push_back(bone);
    return bone;
}

struct Rig {
    std::shared_ptr<threepp::PlaneGeometry> geometry;
    std::shared_ptr<SkinnedMesh> mesh;
    std::shared_ptr<Skeleton> skeleton;
    std::unordered_map<std::string, std::shared_ptr<Bone>> bone;
    std::vector<std::shared_ptr<Bone>> bones;
    float unit{0.f};
    int imageW{0}, imageH{0};
};

void weightsForHand(float px, float py, std::vector<float>& indices, std::vector<float>& weights,
                    int vertex, const std::unordered_map<std::string, std::shared_ptr<Bone>>& bone,
                    const std::vector<std::shared_ptr<Bone>>& bones) {
    (void)bone;
    (void)bones;
    // Index zero is the fixed palm. Each digit has three child joints in the
    // order recorded below. The nearest digit tube receives smooth joint blends.
    struct Finger { const char* name; Joint j[4]; float radius; };
    static const Finger fingers[] = {
        {"pinky", {{242,575},{211,479},{178,390},{138,330}}, 82.f},
        {"ring", {{376,561},{361,444},{346,309},{337,163}}, 82.f},
        {"middle", {{511,560},{509,423},{501,302},{486,91}}, 84.f},
        {"index", {{656,564},{665,463},{667,355},{673,171}}, 80.f},
        {"thumb", {{744,777},{820,694},{895,625},{980,600}}, 90.f},
    };
    const int stride = 3;
    float best = 1e9f, bestT = 0.f;
    int bestFinger = -1;
    for (int f = 0; f < 5; ++f) {
        float cursor = 0.f;
        const float lens[3] = {
            std::hypot(fingers[f].j[1].x-fingers[f].j[0].x, fingers[f].j[1].y-fingers[f].j[0].y),
            std::hypot(fingers[f].j[2].x-fingers[f].j[1].x, fingers[f].j[2].y-fingers[f].j[1].y),
            std::hypot(fingers[f].j[3].x-fingers[f].j[2].x, fingers[f].j[3].y-fingers[f].j[2].y)};
        const float total = lens[0] + lens[1] + lens[2];
        for (int s = 0; s < 3; ++s) {
            float u = 0.f;
            const float d = segmentDistance({px,py}, fingers[f].j[s], fingers[f].j[s+1], u);
            if (d < best) {
                best = d;
                bestFinger = f;
                bestT = (cursor + u * lens[s]) / total;
            }
            cursor += lens[s];
        }
    }
    const int base = 1 + bestFinger * stride;
    auto set = [&](int slot, int bi, float w) {
        indices[vertex*4+slot] = static_cast<float>(bi);
        weights[vertex*4+slot] = w;
    };
    if (bestFinger < 0 || best > 66.f) { set(0,0,1.f); return; }
    const float t = bestT;
    const float rootBlend = 1.f - smooth(0.04f, 0.16f, t);
    const float b1 = smooth(0.25f,0.39f,t);
    const float b2 = smooth(0.60f,0.74f,t);
    const int seg = t < 0.33f ? 0 : (t < 0.67f ? 1 : 2);
    const int current = base + seg;
    if (rootBlend > 0.001f) {
        set(0,0,rootBlend); set(1,base,1.f-rootBlend);
    } else if (std::abs(t-0.33f) < 0.075f) {
        const float q = smooth(0.255f,0.405f,t);
        set(0,base,1.f-q); set(1,base+1,q);
    } else if (std::abs(t-0.67f) < 0.075f) {
        const float q = smooth(0.595f,0.745f,t);
        set(0,base+1,1.f-q); set(1,base+2,q);
    } else {
        set(0,current,1.f);
    }
    (void)b1; (void)b2;
}

Rig makeImageRig(const std::string& imagePath, const std::string& mode,
                 int width, int height) {
    ImageLoader loader;
    auto image = loader.load(imagePath, 4, true);
    if (!image || image->width() == 0) throw std::runtime_error("could not load keyed image: " + imagePath);
    Rig rig;
    rig.imageW = image->width(); rig.imageH = image->height();
    const float planeH = mode == "hand" ? 2.05f : 2.0f;
    const float planeW = planeH * static_cast<float>(rig.imageW) / rig.imageH;
    rig.unit = planeH / rig.imageH;
    const int sx = mode == "hand" ? 160 : 220;
    const int sy = mode == "hand" ? 250 : 125;
    rig.geometry = threepp::PlaneGeometry::create(planeW, planeH, sx, sy);
    rig.mesh = SkinnedMesh::create(rig.geometry, nullptr);

    auto root = addBone(nullptr, "torso", 0.f, 0.f, rig.unit, rig.bones);
    if (mode=="hand") root->position.y=(rig.imageH*0.5f-980.f)*rig.unit;
    rig.bone[root->name] = root;
    if (mode == "hand") {
        auto palm = root;
        const std::array<std::pair<const char*, std::array<Joint,4>>,5> fingers{{
            {"pinky", {{{242,575},{211,479},{178,390},{138,330}}}},
            {"ring", {{{376,561},{361,444},{346,309},{337,163}}}},
            {"middle", {{{511,560},{509,423},{501,302},{486,91}}}},
            {"index", {{{656,564},{665,463},{667,355},{673,171}}}},
            {"thumb", {{{744,777},{820,694},{895,625},{980,600}}}},
        }};
        for (const auto& f : fingers) {
            const std::string name = f.first;
            const auto& j = f.second;
            auto proximal = addBone(palm, name+"_01", j[0].x - 512.f, 980.f-j[0].y, rig.unit, rig.bones);
            auto middle = addBone(proximal, name+"_02", j[1].x-j[0].x, j[0].y-j[1].y, rig.unit, rig.bones);
            auto distal = addBone(middle, name+"_03", j[2].x-j[1].x, j[1].y-j[2].y, rig.unit, rig.bones);
            rig.bone[proximal->name]=proximal; rig.bone[middle->name]=middle; rig.bone[distal->name]=distal;
        }
    } else if (mode != "torso") {
        const float neckX = mode == "head" ? 835.f : 710.f;
        const float neckY = mode == "head" ? 750.f : 690.f;
        auto neck = addBone(root, "neck", neckX, rig.imageH * 0.5f - neckY, rig.unit, rig.bones);
        auto head = addBone(neck, "head", 0.f, 0.f, rig.unit, rig.bones);
        rig.bone[neck->name]=neck; rig.bone[head->name]=head;
        if (mode == "wave") {
            auto shoulder = addBone(root,"shoulder_R",1000.f-rig.imageW*0.5f,rig.imageH*0.5f-610.f,rig.unit,rig.bones);
            auto upper = addBone(shoulder,"upperarm_R",0.f,0.f,rig.unit,rig.bones);
            auto fore = addBone(upper, "forearm_R",1545.f-1000.f,610.f-822.f,rig.unit,rig.bones);
            auto hand = addBone(fore, "hand_R",1390.f-1545.f,822.f-430.f,rig.unit,rig.bones);
            rig.bone[shoulder->name]=shoulder; rig.bone[upper->name]=upper;
            rig.bone[fore->name]=fore; rig.bone[hand->name]=hand;
        }
    }

    auto positions = rig.geometry->getAttribute<float>("position");
    std::vector<float> skinIndex(static_cast<std::size_t>(positions->count())*4, 0.f);
    std::vector<float> skinWeight(static_cast<std::size_t>(positions->count())*4, 0.f);
    auto addWeight = [&](int v, int slot, int boneIndex, float weight) {
        skinIndex[v*4+slot] = static_cast<float>(boneIndex);
        skinWeight[v*4+slot] = weight;
    };
    for (int v=0; v<positions->count(); ++v) {
        const float x=positions->getX(v), y=positions->getY(v);
        const float px=(x/planeW+0.5f)*rig.imageW;
        const float py=(0.5f-y/planeH)*rig.imageH;
        addWeight(v,0,0,1.f);
        if (mode=="torso") continue;
        if (mode == "hand") {
            weightsForHand(px,py,skinIndex,skinWeight,v,rig.bone,rig.bones);
            continue;
        }
        const bool isWave = mode == "wave";
        // Rigid skull influence, then a broad transition through the neck into
        // the fixed torso. This is vertex skinning, not a cutout/mask overlay.
        const float headCenterX = isWave ? 700.f : 825.f;
        const std::vector<Joint> headContour = isWave
            ? std::vector<Joint>{{700,14},{790,18},{870,60},{930,130},{957,230},{956,340},{935,440},{900,520},{870,570},{840,610},{810,660},{780,710},{700,730},{630,710},{570,660},{520,610},{480,540},{455,450},{445,340},{460,230},{500,130},{560,55},{640,20}}
            : std::vector<Joint>{{825,14},{915,18},{995,60},{1055,130},{1082,230},{1081,340},{1060,440},{1025,520},{995,570},{965,610},{935,660},{905,710},{825,730},{755,710},{695,660},{645,610},{605,540},{580,450},{570,340},{585,230},{625,130},{685,55},{765,20}};
        const bool headRegion = insidePolygon(px,py,headContour);
        if (headRegion) {
            const float neckBlend = smooth(620.f,735.f,py);
            const int neckId=1, headId=2;
            skinIndex[v*4]=static_cast<float>(headId); skinWeight[v*4]=1.f-neckBlend;
            if (neckBlend>0.001f) { skinIndex[v*4+1]=static_cast<float>(neckId); skinWeight[v*4+1]=neckBlend; }
        }
        if (isWave) {
            struct ArmLine { Joint a,b; float radius; int bone; };
            const ArmLine lines[] = {
                {{1000,610},{1545,822},92.f,4},
                {{1545,822},{1390,430},78.f,5},
                {{1390,430},{1270,245},205.f,6},
            };
            float best=1e9f, bestU=0.f; int bestBone=0;
            for (const auto& line:lines) {
                float u=0.f; const float d=segmentDistance({px,py},line.a,line.b,u);
                if (d<best && d<line.radius) { best=d; bestU=u; bestBone=line.bone; }
            }
            if (bestBone) {
                const float jointBlend=1.f-smooth(0.f,28.f,best);
                skinIndex[v*4]=static_cast<float>(bestBone); skinWeight[v*4]=1.f;
                if (jointBlend>0.001f) {
                    const int parent=bestBone==4?3:(bestBone==5?4:5);
                    skinIndex[v*4+1]=static_cast<float>(parent);
                    skinWeight[v*4]=1.f-0.30f*jointBlend;
                    skinWeight[v*4+1]=0.30f*jointBlend;
                }
                (void)bestU;
            }
        }
    }
    rig.geometry->setAttribute("skinIndex", FloatBufferAttribute::create(std::move(skinIndex),4));
    rig.geometry->setAttribute("skinWeight", FloatBufferAttribute::create(std::move(skinWeight),4));
    rig.mesh->add(rig.bones.front());
    rig.skeleton=Skeleton::create(rig.bones);
    rig.mesh->updateMatrixWorld(true);
    rig.skeleton->calculateInverses();
    rig.skeleton->computeBoneTexture();
    rig.mesh->bind(rig.skeleton);
    rig.mesh->normalizeSkinWeights();

    auto texture=Texture::create(std::move(*image));
    texture->needsUpdate();
    auto material=MeshBasicMaterial::create(MeshBasicMaterial::Params().map(texture));
    material->transparent=true; material->depthWrite=false; material->side=Side::Double;
    rig.mesh->setMaterial(material);
    return rig;
}

void animate(Rig& rig, const std::string& mode, float t) {
    for (auto& b:rig.bones) b->quaternion.set(0.f,0.f,0.f,1.f);
    if (mode=="hand") {
        // Open -> close -> hold -> open. Each finger follows its own parent-child
        // chain, flexing at MCP, PIP and DIP while the palm remains still.
        const float close = smooth(0.25f,1.05f,t) * (1.f-smooth(1.65f,2.65f,t));
        const std::array<std::pair<const char*,float>,5> dirs{{
            {"pinky",-1.f},{"ring",-1.f},{"middle",-0.45f},{"index",1.f},{"thumb",0.9f}}};
        for (const auto& [name,dir]:dirs) {
            const float middleDir=std::string(name)=="middle"?-1.f:dir;
            rig.bone.at(std::string(name)+"_01")->quaternion.setFromAxisAngle(Vector3(0,0,1),dir*0.82f*close);
            rig.bone.at(std::string(name)+"_02")->quaternion.setFromAxisAngle(Vector3(0,0,1),middleDir*1.02f*close);
            rig.bone.at(std::string(name)+"_03")->quaternion.setFromAxisAngle(Vector3(0,0,1),middleDir*0.78f*close);
        }
    } else if (mode=="head") {
        const float sway=std::sin(t*2.f*kPi/4.f);
        // Slide the complete head/neck chain laterally. Keep all joint heights
        // fixed so the head does not bob vertically while it sways.
        rig.bone.at("neck")->quaternion.identity();
        rig.bone.at("head")->quaternion.identity();
        rig.bone.at("neck")->position.x=835.f*rig.unit+0.045f*sway;
        rig.bone.at("head")->position.x=0.025f*sway;
    } else if (mode=="wave") {
        // The arm starts in a raised greeting pose. A shoulder/elbow follow
        // motion and a stronger wrist arc produce a readable hello.
        const float wave=std::sin((t-0.35f)*2.f*kPi*1.1f);
        rig.bone.at("neck")->quaternion.setFromAxisAngle(Vector3(0,0,1),0.022f*std::sin(t*2.f*kPi/4.f));
        rig.bone.at("head")->quaternion.setFromAxisAngle(Vector3(0,0,1),0.040f*std::sin(t*2.f*kPi/4.f));
        rig.bone.at("upperarm_R")->quaternion.setFromAxisAngle(Vector3(0,0,1),0.045f*wave);
        rig.bone.at("forearm_R")->quaternion.setFromAxisAngle(Vector3(0,0,1),0.085f*wave);
        rig.bone.at("hand_R")->quaternion.setFromAxisAngle(Vector3(0,0,1),0.30f*wave);
    }
    rig.mesh->updateMatrixWorld(true);
    rig.skeleton->update();
}

int renderGlbHand(const std::string& outPath, int width, int height, int frames) {
    GLTFLoader loader;
    const auto asset = std::filesystem::path("/mnt/c/Users/pater/dev/projects/models/output_mrbeast/ual1_seated_gestures.glb");
    auto loaded = loader.load(asset);
    if (!loaded || !loaded->scene) throw std::runtime_error("failed to load articulated hand rig: " + asset.string());
    std::array<Bone*, 15> joints{};
    const std::array<std::string, 5> digits{"thumb", "index", "middle", "ring", "pinky"};
    for (std::size_t d=0; d<digits.size(); ++d) {
        for (int j=1; j<=3; ++j) {
            auto* node=loaded->scene->getObjectByName(digits[d]+"_0"+std::to_string(j)+"_r");
            joints[d*3+j-1]=dynamic_cast<Bone*>(node);
            if (!joints[d*3+j-1]) throw std::runtime_error("missing finger bone: "+digits[d]+"_0"+std::to_string(j)+"_r");
        }
    }
    std::array<Quaternion,15> fingerRest{};
    for(std::size_t i=0;i<joints.size();++i) fingerRest[i]=joints[i]->quaternion.clone();
    Canvas canvas(Canvas::Parameters().size(width,height).headless(true));
    GLRenderer renderer(canvas);
    renderer.setClearColor(Color(0x00ff00));
    auto target=RenderTarget::create(width,height,RenderTarget::Options{});
    renderer.setRenderTarget(target.get());
    auto scene=Scene::create();
    scene->add(loaded->scene);
    auto ambient=AmbientLight::create(Color(0xffffff),2.0f); scene->add(ambient);
    auto key=DirectionalLight::create(Color(0xffffff),2.8f); key->position.set(-2.f,3.f,4.f); scene->add(key);
    auto camera=OrthographicCamera::create(-0.48f*float(width)/height,0.48f*float(width)/height,0.48f,-0.48f,0.001f,20.f);
    camera->position.set(-1.20f,1.45f,1.2f); camera->lookAt(Vector3(-1.20f,1.45f,0.f));
    const float fps=30.f;
    auto renderFrame=[&](int frame) {
        const float t=float(frame)/fps;
        const float close=smooth(0.30f,1.10f,t)*(1.f-smooth(1.95f,2.75f,t));
        for (std::size_t d=0; d<digits.size(); ++d) {
            const float curl=d==0 ? -0.72f : 1.f;
            for (int j=0;j<3;++j) {
                const float amount=(j==0?0.62f:(j==1?1.10f:0.82f))*curl*close;
                Quaternion delta; delta.setFromAxisAngle(Vector3(0,0,1),amount);
                joints[d*3+j]->quaternion.copy(fingerRest[d*3+j]).multiply(delta);
            }
        }
        scene->updateMatrixWorld(true);
        renderer.render(*scene,*camera);
        return renderer.readRGBPixels();
    };
    const auto first=renderFrame(0), middle=renderFrame(frames/2);
    std::size_t changed=0;
    for(std::size_t i=0;i<first.size();i+=3)
        if(std::abs(int(first[i])-int(middle[i]))>8 || std::abs(int(first[i+1])-int(middle[i+1]))>8 || std::abs(int(first[i+2])-int(middle[i+2]))>8) ++changed;
    if(changed<2500) throw std::runtime_error("finger pose change too small in preview");
    const std::string cmd="ffmpeg -y -loglevel error -f rawvideo -pixel_format rgb24 -video_size "+std::to_string(width)+"x"+std::to_string(height)+" -framerate 30 -i - -vf vflip -c:v libx264 -crf 16 -pix_fmt yuv420p \""+outPath+"\"";
    FILE* pipe=popen(cmd.c_str(),"w"); if(!pipe) throw std::runtime_error("ffmpeg pipe failed");
    for(int f=0;f<frames;++f){const auto pixels=renderFrame(f);std::fwrite(pixels.data(),1,pixels.size(),pipe);}
    const int rc=pclose(pipe); if(rc!=0) throw std::runtime_error("ffmpeg encoder failed");
    std::cout<<"skeletal_rig: hand GLB finger joints changed_pixels="<<changed<<" wrote "<<outPath<<"\n";
    std::cout.flush(); std::_Exit(0);
}

int renderGlbWave(const std::string& outPath, int width, int height, int frames) {
    GLTFLoader loader;
    const auto asset=std::filesystem::path("/mnt/c/Users/pater/dev/projects/models/output_mrbeast/rig/mrbeast_rigged.glb");
    auto loaded=loader.load(asset);
    if(!loaded || !loaded->scene) throw std::runtime_error("failed to load MrBeast rig: "+asset.string());
    auto bone=[&](const std::string& name)->Bone* {
        auto* result=dynamic_cast<Bone*>(loaded->scene->getObjectByName(name));
        if(!result) throw std::runtime_error("missing MrBeast bone: "+name);
        return result;
    };
    Bone* neck=bone("neck"); Bone* head=bone("head");
    Bone* upper=bone("upperarm.R"); Bone* fore=bone("lowerarm.R"); Bone* hand=bone("hand.R");
    const Quaternion neckRest=neck->quaternion.clone(), headRest=head->quaternion.clone();
    const Quaternion upperRest=upper->quaternion.clone(), foreRest=fore->quaternion.clone(), handRest=hand->quaternion.clone();
    Canvas canvas(Canvas::Parameters().size(width,height).headless(true));
    GLRenderer renderer(canvas); renderer.setClearColor(Color(0x00ff00));
    auto target=RenderTarget::create(width,height,RenderTarget::Options{}); renderer.setRenderTarget(target.get());
    auto scene=Scene::create(); scene->add(loaded->scene);
    auto ambient=AmbientLight::create(Color(0xffffff),2.2f); scene->add(ambient);
    auto key=DirectionalLight::create(Color(0xffffff),2.4f); key->position.set(-2.f,3.f,4.f); scene->add(key);
    auto camera=OrthographicCamera::create(-0.72f*float(width)/height,0.72f*float(width)/height,0.72f,-0.72f,0.001f,20.f);
    camera->position.set(0.f,0.64f,-2.8f); camera->lookAt(Vector3(0.f,0.64f,0.f));
    auto renderFrame=[&](int frame) {
        const float t=float(frame)/30.f;
        const float wave=std::sin((t-0.25f)*2.f*kPi*1.15f);
        Quaternion delta;
        delta.setFromAxisAngle(Vector3(0,0,1),-0.32f+0.08f*wave); upper->quaternion.copy(upperRest).multiply(delta);
        delta.setFromAxisAngle(Vector3(0,0,1),0.62f+0.25f*wave); fore->quaternion.copy(foreRest).multiply(delta);
        delta.setFromAxisAngle(Vector3(0,0,1),0.45f*wave); hand->quaternion.copy(handRest).multiply(delta);
        delta.setFromAxisAngle(Vector3(0,0,1),0.04f*std::sin(t*2.f*kPi/4.f)); neck->quaternion.copy(neckRest).multiply(delta);
        delta.setFromAxisAngle(Vector3(0,0,1),0.06f*std::sin(t*2.f*kPi/4.f)); head->quaternion.copy(headRest).multiply(delta);
        scene->updateMatrixWorld(true); renderer.render(*scene,*camera); return renderer.readRGBPixels();
    };
    const auto first=renderFrame(0), middle=renderFrame(frames/2);
    std::size_t changed=0;
    for(std::size_t i=0;i<first.size();i+=3)
        if(std::abs(int(first[i])-int(middle[i]))>8 || std::abs(int(first[i+1])-int(middle[i+1]))>8 || std::abs(int(first[i+2])-int(middle[i+2]))>8) ++changed;
    if(changed<4000) throw std::runtime_error("arm wave motion too small in preview");
    const std::string cmd="ffmpeg -y -loglevel error -f rawvideo -pixel_format rgb24 -video_size "+std::to_string(width)+"x"+std::to_string(height)+" -framerate 30 -i - -vf vflip -c:v libx264 -crf 16 -pix_fmt yuv420p \""+outPath+"\"";
    FILE* pipe=popen(cmd.c_str(),"w"); if(!pipe) throw std::runtime_error("ffmpeg pipe failed");
    for(int f=0;f<frames;++f){const auto pixels=renderFrame(f);std::fwrite(pixels.data(),1,pixels.size(),pipe);}
    const int rc=pclose(pipe); if(rc!=0) throw std::runtime_error("ffmpeg encoder failed");
    std::cout<<"skeletal_rig: MrBeast GLB arm wave changed_pixels="<<changed<<" wrote "<<outPath<<"\n";
    std::cout.flush(); std::_Exit(0);
}

} // namespace

int main(int argc,char** argv) {
    const std::string mode=argc>1?argv[1]:"hand";
    std::string imagePath;
    std::string outPath="green_screen_"+mode+"_rig.mp4";
    int width=1920,height=1080,frames=mode=="hand"?91:121;
    if (mode=="hand") imagePath="/mnt/c/Users/pater/dev/projects/ChrononTemplate/assets/social_rig_proof_v1/processed/hand_keyed.png";
    else if (mode=="wave") imagePath="/mnt/c/Users/pater/dev/projects/ChrononTemplate/assets/social_rig_proof_v1/processed/keanu_wave_keyed.png";
    else if (mode=="head") { imagePath="/mnt/c/Users/pater/dev/projects/ChrononTemplate/assets/social_rig_keanu_v1/processed/keanu_keyed.png"; frames=121; }
    else if (mode=="handglb") imagePath.clear();
    else if (mode=="waveglb") imagePath.clear();
    else { std::cerr<<"mode must be hand, head or wave\n"; return 2; }
    if (argc>2) outPath=argv[2];
    if (argc>3) width=std::atoi(argv[3]);
    if (argc>4) height=std::atoi(argv[4]);
    if (argc>5) frames=std::max(4,std::atoi(argv[5]));
    if (mode=="handglb") {
        try { return renderGlbHand(outPath,width,height,frames); }
        catch (const std::exception& e) { std::cerr<<"skeletal_rig: "<<e.what()<<"\n"; return 1; }
    }
    if (mode=="waveglb") {
        try { return renderGlbWave(outPath,width,height,frames); }
        catch (const std::exception& e) { std::cerr<<"skeletal_rig: "<<e.what()<<"\n"; return 1; }
    }
    const float fps=30.f, duration=static_cast<float>(frames-1)/fps;

    Canvas canvas(Canvas::Parameters().size(width,height).headless(true));
    GLRenderer renderer(canvas);
    renderer.setClearColor(Color(0x00ff00));
    auto target=RenderTarget::create(width,height,RenderTarget::Options{});
    renderer.setRenderTarget(target.get());
    auto scene=Scene::create();
    Rig rig=makeImageRig(imagePath,mode,width,height);
    scene->add(rig.mesh);
    const float aspect=static_cast<float>(width)/height;
    const float viewH=mode=="hand"?2.30f:2.22f;
    auto camera=OrthographicCamera::create(-viewH*aspect*0.5f,viewH*aspect*0.5f,viewH*0.5f,-viewH*0.5f,0.01f,10.f);
    camera->position.set(0,0,3.f); camera->lookAt(Vector3(0,0,0));
    auto renderFrame=[&](int frame) {
        animate(rig,mode,static_cast<float>(frame)/fps);
        renderer.render(*scene,*camera);
        return renderer.readRGBPixels();
    };
    const auto f0=renderFrame(0);
    const auto fmid=renderFrame(mode=="head"?frames/4:frames/2);
    const auto fend=renderFrame(frames-1);
    std::size_t delta=0;
    for (std::size_t i=0;i<f0.size();i+=3)
        if (std::abs(int(f0[i])-int(fmid[i]))>8 || std::abs(int(f0[i+1])-int(fmid[i+1]))>8 || std::abs(int(f0[i+2])-int(fmid[i+2]))>8) ++delta;
    std::cout<<"skeletal_rig: mode="<<mode<<" changed_pixels(open->mid)="<<delta
             <<" end_rest_matches="<<(mode=="hand"?"no":"yes")<<"\n";
    if (delta<4000) { std::cerr<<"skeletal_rig: expected visible articulated motion\n"; return 1; }
    const std::string cmd="ffmpeg -y -loglevel error -f rawvideo -pixel_format rgb24 -video_size "+
        std::to_string(width)+"x"+std::to_string(height)+" -framerate 30 -i - -vf vflip -c:v libx264 -crf 16 -pix_fmt yuv420p \""+outPath+"\"";
    FILE* pipe=popen(cmd.c_str(),"w");
    if(!pipe){std::cerr<<"skeletal_rig: ffmpeg pipe failed\n";return 1;}
    for(int f=0;f<frames;++f){auto pixels=renderFrame(f);std::fwrite(pixels.data(),1,pixels.size(),pipe);}
    const int rc=pclose(pipe);
    if(rc!=0){std::cerr<<"skeletal_rig: ffmpeg failed\n";return 1;}
    std::cout<<"skeletal_rig: wrote "<<frames<<" frames to "<<outPath<<"\n";
    std::cout.flush();
    std::_Exit(0);
}
