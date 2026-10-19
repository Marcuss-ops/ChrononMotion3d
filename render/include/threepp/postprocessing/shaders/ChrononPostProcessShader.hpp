#ifndef THREEPP_CHRONON_POSTPROCESS_SHADER_HPP
#define THREEPP_CHRONON_POSTPROCESS_SHADER_HPP

#include "threepp/core/Shader.hpp"

#include <cmath>

namespace threepp::shaders {

    // Full-screen single-sample finishing pass. Prepare tBackdrop with a
    // separable blur of the source, and tHighlights with a blurred bright-pass
    // (see the companion shaders below). Unused inputs can point at the source.
    inline Shader chrononPostProcessShader() {
        return Shader{
            UniformMap{
                {"tDiffuse", Uniform()}, {"tBackdrop", Uniform()}, {"tHighlights", Uniform()},
                {"uResolution", Uniform(Vector2(1920.f, 1080.f))}, {"uTime", Uniform(0.f)},
                {"uScanline", Uniform(0.f)}, {"uGrid", Uniform(0.f)}, {"uGridScale", Uniform(80.f)},
                {"uChromatic", Uniform(0.f)}, {"uVhs", Uniform(0.f)}, {"uHalation", Uniform(0.f)},
                {"uLens", Uniform(0.f)}, {"uEdgeSoftening", Uniform(0.f)}, {"uStreak", Uniform(0.f)},
                {"uGrain", Uniform(0.f)}},
            R"(
                varying vec2 vUv;
                void main() {
                    vUv = uv;
                    gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.0);
                })",
            R"(
                uniform sampler2D tDiffuse;
                uniform sampler2D tBackdrop;
                uniform sampler2D tHighlights;
                uniform vec2 uResolution;
                uniform float uTime, uScanline, uGrid, uGridScale, uChromatic, uVhs;
                uniform float uHalation, uLens, uEdgeSoftening, uStreak, uGrain;
                varying vec2 vUv;

                float hash12(vec2 p) {
                    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
                    p3 += dot(p3, p3.yzx + 33.33);
                    return fract((p3.x + p3.y) * p3.z);
                }
                void main() {
                    vec2 centered = vUv - 0.5;
                    float r2 = dot(centered, centered);
                    vec2 lensUv = centered * (1.0 + uLens * r2) + 0.5;
                    float row = floor(gl_FragCoord.y);
                    float jitter = uVhs * step(0.985, hash12(vec2(floor(uTime * 20.0), row))) * 0.003;
                    vec2 uv = lensUv + vec2(jitter, 0.0);
                    vec2 dir = centered * (r2 * uChromatic);
                    vec3 color;
                    color.r = texture2D(tDiffuse, uv - dir).r;
                    color.g = texture2D(tDiffuse, uv).g;
                    color.b = texture2D(tDiffuse, uv + dir).b;

                    // Low-bandwidth chroma: preserve luma and smear only U/V horizontally.
                    if (uVhs > 0.0) {
                        vec3 yuv = mat3(0.299, -0.14713, 0.615,
                                       0.587, -0.28886, -0.51499,
                                       0.114, 0.436, -0.10001) * color;
                        vec2 px = vec2(1.0 / max(uResolution.x, 1.0), 0.0);
                        vec3 a = texture2D(tDiffuse, uv - px * 2.0).rgb;
                        vec3 b = texture2D(tDiffuse, uv - px).rgb;
                        vec3 c = texture2D(tDiffuse, uv + px).rgb;
                        vec3 d = texture2D(tDiffuse, uv + px * 2.0).rgb;
                        float u = dot((a+b+c+d)*0.25, vec3(-0.14713, -0.28886, 0.436));
                        float v = dot((a+b+c+d)*0.25, vec3(0.615, -0.51499, -0.10001));
                        yuv.yz = mix(yuv.yz, vec2(u, v), clamp(uVhs, 0.0, 1.0));
                        color = mat3(1.0, 1.0, 1.0,
                                     0.0, -0.39465, 2.03211,
                                     1.13983, -0.58060, 0.0) * yuv;
                    }

                    float edge = smoothstep(0.40, 0.90, length(centered) * 1.4142);
                    vec3 backdrop = texture2D(tBackdrop, lensUv).rgb;
                    color = mix(color, backdrop, clamp(uEdgeSoftening * edge, 0.0, 1.0));
                    vec2 cell = vUv * uGridScale;
                    vec2 fw = max(fwidth(cell), vec2(0.0001)) * 1.5;
                    vec2 line = 1.0 - smoothstep(fw, fw + vec2(0.035), min(fract(cell), 1.0-fract(cell)));
                    float grid = max(line.x, line.y);
                    color = mix(color, backdrop + vec3(0.15), clamp(grid * uGrid, 0.0, 1.0));

                    float scan = 0.5 + 0.5 * sin(gl_FragCoord.y * 3.14159265);
                    color *= mix(1.0, 0.92 + 0.08 * scan, clamp(uScanline, 0.0, 1.0));
                    vec3 highlights = texture2D(tHighlights, lensUv).rgb;
                    color += highlights * vec3(1.0, 0.25, 0.05) * uHalation;
                    color += highlights * uStreak;

                    float luma = dot(color, vec3(0.2126, 0.7152, 0.0722));
                    float midtones = clamp(1.0 - abs(luma - 0.5) * 2.0, 0.0, 1.0);
                    float grain = hash12(gl_FragCoord.xy + fract(uTime * 43.0)) - 0.5;
                    color += grain * uGrain * midtones;
                    gl_FragColor = vec4(max(color, vec3(0.0)), texture2D(tDiffuse, uv).a);
                    #include <encodings_fragment>
                })"};
    }

    // Bright-pass input for halation and horizontal streak bloom. Blur it at
    // reduced resolution with the separable shader and appropriate direction.
    inline Shader chrononHighlightExtractShader() {
        return Shader{
            UniformMap{{"tDiffuse", Uniform()}, {"uThreshold", Uniform(0.85f)}, {"uKnee", Uniform(0.15f)}},
            R"(varying vec2 vUv; void main(){ vUv=uv; gl_Position=projectionMatrix*modelViewMatrix*vec4(position,1.0); })",
            R"(uniform sampler2D tDiffuse; uniform float uThreshold, uKnee; varying vec2 vUv;
               void main(){ vec3 c=texture2D(tDiffuse,vUv).rgb; float l=dot(c,vec3(0.2126,0.7152,0.0722));
               float soft=clamp((l-uThreshold+uKnee)/(2.0*uKnee+1e-5),0.0,1.0); soft=soft*soft*uKnee;
               float contribution=max(l-uThreshold,0.0)+soft;
               gl_FragColor=vec4(c*(contribution/max(l,1e-5)),1.0);
               #include <encodings_fragment>
            })"};
    }

    // Separable Gaussian blur. Set uDirection to (radiusPx,0) then (0,radiusPx)
    // on alternating ping-pong targets; downsample first for broad blur radii.
    inline Shader chrononGaussianBlurShader() {
        return Shader{
            UniformMap{{"tDiffuse", Uniform()}, {"uTexel", Uniform(Vector2(1.f/1920.f, 1.f/1080.f))},
                       {"uDirection", Uniform(Vector2(1.f, 0.f))}},
            R"(varying vec2 vUv; void main(){ vUv=uv; gl_Position=projectionMatrix*modelViewMatrix*vec4(position,1.0); })",
            R"(uniform sampler2D tDiffuse; uniform vec2 uTexel, uDirection; varying vec2 vUv;
               void main(){ vec2 d=uTexel*uDirection; vec4 c=texture2D(tDiffuse,vUv)*0.227027;
               c+=(texture2D(tDiffuse,vUv+d*1.384615)+texture2D(tDiffuse,vUv-d*1.384615))*0.316216;
               c+=(texture2D(tDiffuse,vUv+d*3.230769)+texture2D(tDiffuse,vUv-d*3.230769))*0.070270;
               gl_FragColor=c;
               #include <encodings_fragment>
            })"};
    }

    // Quantize scene evaluation time, not only the final image, so motion
    // itself steps while the output encoder can keep its normal frame rate.
    inline float chrononSteppedTime(float currentTime, float targetFps = 15.f) {
        if (!std::isfinite(currentTime) || !std::isfinite(targetFps) || targetFps <= 0.f) return currentTime;
        return std::floor(currentTime * targetFps) / targetFps;
    }

}// namespace threepp::shaders

#endif
