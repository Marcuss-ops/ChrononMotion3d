// threepp — the GL consumer of ChrononMotion's renderer-neutral DebugDraw.
//
// The preview layer owns the line list; this pass owns only one program, one
// VAO/VBO and the state save/restore needed to sit after a normal scene render.
// It is deliberately not part of the core library.

#ifndef THREEPP_DEBUGDRAWRENDERER_HPP
#define THREEPP_DEBUGDRAWRENDERER_HPP

#include "chrononmotion/preview/DebugDraw.hpp"

#include <array>
#include <cstddef>

namespace threepp {

    class DebugDrawRenderer {
    public:
        DebugDrawRenderer();
        ~DebugDrawRenderer();

        DebugDrawRenderer(const DebugDrawRenderer&) = delete;
        DebugDrawRenderer& operator=(const DebugDrawRenderer&) = delete;
        DebugDrawRenderer(DebugDrawRenderer&&) = delete;
        DebugDrawRenderer& operator=(DebugDrawRenderer&&) = delete;

        /// Draw world-space lines with an explicit column-major view-projection
        /// matrix. Depth testing is opt-in so a debug overlay can pass false.
        void render(const chrononmotion::preview::DebugDraw& draw,
                    const std::array<float, 16>& viewProjection,
                    bool depthTest = false);

        /// Draw NDC-space lines (the diagnostics overlay). Identical to render()
        /// with an identity matrix; `width`/`height` are accepted for callers
        /// that want to assert the viewport they prepared.
        void renderScreenSpace(const chrononmotion::preview::DebugDraw& draw,
                               int width, int height, bool depthTest = false);

        void dispose();
        [[nodiscard]] bool valid() const { return program_ != 0; }

    private:
        unsigned int program_{0};
        unsigned int vao_{0};
        unsigned int vbo_{0};
    };

}// namespace threepp

#endif//THREEPP_DEBUGDRAWRENDERER_HPP
