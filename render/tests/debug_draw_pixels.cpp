#include "threepp/canvas/Canvas.hpp"
#include "threepp/debug/DebugDrawRenderer.hpp"
#include "threepp/renderers/GLRenderer.hpp"

#include "chrononmotion/preview/DebugDraw.hpp"

#include <array>
#include <cstddef>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
    constexpr int kWidth = 64;
    constexpr int kHeight = 64;

    void require(bool condition, const char* message) {
        if (!condition) throw std::runtime_error(message);
    }

    std::size_t redPixels(const std::vector<unsigned char>& rgb) {
        std::size_t count = 0;
        for (std::size_t i = 0; i + 2 < rgb.size(); i += 3) {
            count += rgb[i] > 200 && rgb[i + 1] < 60 && rgb[i + 2] < 60;
        }
        return count;
    }
}

int main() try {
    using namespace threepp;

    Canvas canvas(Canvas::Parameters().size(kWidth, kHeight).headless(true));
    GLRenderer renderer(canvas);
    renderer.setViewport(0, 0, kWidth, kHeight);
    renderer.setClearColor(Color(0x000000), 1.f);
    renderer.clear(true, true, false);

    chrononmotion::preview::DebugDraw draw;
    draw.line({-1.f, 0.f, 0.f}, {1.f, 0.f, 0.f}, chrononmotion::Color{1.f, 0.f, 0.f});

    DebugDrawRenderer debug;
    require(debug.valid(), "debug renderer did not initialise");
    const std::array<float, 16> identity{
            1.f, 0.f, 0.f, 0.f,
            0.f, 1.f, 0.f, 0.f,
            0.f, 0.f, 1.f, 0.f,
            0.f, 0.f, 0.f, 1.f};
    debug.render(draw, identity, false);

    const auto pixels = renderer.readRGBPixels();
    const std::size_t red = redPixels(pixels);
    require(red > 0, "debug line produced no red pixels");
    renderer.dispose();
    std::cout << "DebugDrawRenderer drew " << red << " red pixels\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "DebugDrawRenderer test failed: " << error.what() << '\n';
    return 1;
}
