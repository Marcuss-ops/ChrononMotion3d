// https://github.com/mrdoob/three.js/blob/r129/examples/jsm/postprocessing/BokehPass.js

#ifndef THREEPP_POSTPROCESSING_BOKEHPASS_HPP
#define THREEPP_POSTPROCESSING_BOKEHPASS_HPP

#include "threepp/postprocessing/Pass.hpp"

namespace threepp {

    class Camera;
    class MeshDepthMaterial;
    class RenderTarget;
    class Scene;
    class ShaderMaterial;

    // Depth of field. Re-renders the scene's depth into a target of its own,
    // then blurs the incoming image by how far each pixel sits from the focus
    // plane.
    //
    // Needs the scene as a Scene rather than an Object3D: the depth prepass
    // works by swapping overrideMaterial, which only a Scene has.
    class BokehPass: public Pass {

    public:
        // Distance to the focus plane, in world units along the view axis.
        float focus;

        // Strength of the defocus, in UV units per world unit of focus miss:
        // blur_uv = |focus_miss_world| x aperture, with the radius scaled in
        // UV fractions of the canvas height (see shaders/BokehShader.hpp).
        // Convert from CameraRig RenderPlan-contract values with
        // CameraRig::bokehPreviewAperture / bokehPreviewMaxBlur.
        float aperture;

        // Ceiling on the blur radius, in UV units of the canvas height.
        // Without it a distant background smears across the whole frame.
        float maxblur;

        // Horizontal:vertical scale for the sample ring, so the bokeh stays
        // round on a non-square frame. Follows setSize unless assigned.
        float aspect;

        BokehPass(Scene& scene, Camera& camera,
                  float focus = 1.f,
                  float aperture = 0.025f,
                  float maxblur = 0.01f);

        void setSize(unsigned int width, unsigned int height) override;

        void render(GLRenderer& renderer,
                    RenderTarget* writeBuffer,
                    RenderTarget* readBuffer,
                    float deltaTime,
                    bool maskActive) override;

        ~BokehPass() override;

    private:
        Scene* scene_;
        Camera* camera_;

        bool aspectFollowsSize_ = true;

        std::unique_ptr<RenderTarget> renderTargetDepth_;
        std::shared_ptr<MeshDepthMaterial> depthMaterial_;
        std::shared_ptr<ShaderMaterial> bokehMaterial_;
        std::unique_ptr<FullScreenQuad> fsQuad_;
    };

}// namespace threepp

#endif//THREEPP_POSTPROCESSING_BOKEHPASS_HPP
