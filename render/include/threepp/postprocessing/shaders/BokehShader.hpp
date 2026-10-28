// https://github.com/mrdoob/three.js/blob/r129/examples/jsm/shaders/BokehShader.js
// ported from a GLSL shader by Martins Upitis

#ifndef THREEPP_POSTPROCESSING_BOKEHSHADER_HPP
#define THREEPP_POSTPROCESSING_BOKEHSHADER_HPP

#include "threepp/core/Shader.hpp"

namespace threepp::shaders {

    // Depth-of-field: 41 taps on a ring pattern, with the ring radius driven by
    // how far each pixel's view depth is from the focus plane.
    inline Shader bokehShader() {

        return Shader{
                UniformMap{
                        {"tColor", Uniform()},
                        {"tDepth", Uniform()},
                        {"focus", Uniform(1.f)},
                        {"aspect", Uniform(1.f)},
                        {"aperture", Uniform(0.025f)},
                        {"maxblur", Uniform(0.01f)},
                        {"nearClip", Uniform(1.f)},
                        {"farClip", Uniform(1000.f)}},

                R"(
                varying vec2 vUv;

                void main() {
                    vUv = uv;
                    gl_Position = projectionMatrix * modelViewMatrix * vec4( position, 1.0 );
                })",

                R"(
                #include <common>

                varying vec2 vUv;

                uniform sampler2D tColor;
                uniform sampler2D tDepth;

                uniform float maxblur;  // max blur amount
                uniform float aperture; // bigger values for shallower depth of field

                uniform float nearClip;
                uniform float farClip;

                uniform float focus;
                uniform float aspect;

                #include <packing>

                float getDepth( const in vec2 screenPosition ) {
                    #if DEPTH_PACKING == 1
                    return unpackRGBAToDepth( texture2D( tDepth, screenPosition ) );
                    #else
                    return texture2D( tDepth, screenPosition ).x;
                    #endif
                }

                float getViewZ( const in float depth ) {
                    #if PERSPECTIVE_CAMERA == 1
                    return perspectiveDepthToViewZ( depth, nearClip, farClip );
                    #else
                    return orthographicDepthToViewZ( depth, nearClip, farClip );
                    #endif
                }

                void main() {
                    float viewZ = getViewZ( getDepth( vUv ) );

                    float factor = ( focus + viewZ ); // viewZ is <= 0, so this is a difference equation
                    // `maxblur` is expressed as a fraction of image height.
                    // Convert that same pixel radius to U and V; multiplying
                    // V by aspect stretches the blur horizontally on 16:9.
                    vec2 aspectcorrect = vec2( 1.0 / aspect, 1.0 );
                    vec2 dofblur = vec2( clamp( factor * aperture, -maxblur, maxblur ) );

                    // A dense weighted Gaussian avoids separated silhouettes
                    // and the cross-hatch left by sparse bokeh taps on text.
                    vec4 col = vec4( 0.0 );
                    float weightSum = 0.0;
                    for ( int y = -10; y <= 10; ++y ) {
                        for ( int x = -10; x <= 10; ++x ) {
                            vec2 p = vec2( float( x ), float( y ) ) / 10.0;
                            float weight = exp( -4.5 * dot( p, p ) );
                            col += texture2D( tColor, vUv.xy + ( p * aspectcorrect ) * dofblur ) * weight;
                            weightSum += weight;
                        }
                    }
                    gl_FragColor = col / weightSum;
                    gl_FragColor.a = 1.0;

                    #include <encodings_fragment>
                })"};
    }

}// namespace threepp::shaders

#endif//THREEPP_POSTPROCESSING_BOKEHSHADER_HPP
