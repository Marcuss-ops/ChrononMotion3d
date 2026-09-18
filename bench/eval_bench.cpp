// ChrononMotion — evaluate() micro-benchmark.
//
// Standalone: builds against the installed headers + the built static library,
// outside the library target, so the boundary guardrail is untouched.
//
//   g++ -std=c++20 -O2 bench/eval_bench.cpp -Iinclude -Lbuild/release -lchrononmotion -o build/eval_bench
//
// It measures MotionScene::evaluate() on a composition of the size a real promo
// compile would face — N content layers parented to a few nulls, every layer
// keyed with two presets — and verifies that evaluation stays deterministic
// (same frame in, same matrices out) while it times it.

#include "chrononmotion/chrononmotion.hpp"
#include "chrononmotion/motion/Motion.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <new>
#include <string>
#include <vector>

namespace {

    std::atomic<bool> g_measureAllocations{false};
    std::atomic<std::uint64_t> g_allocationCount{0};
    std::atomic<std::uint64_t> g_allocatedBytes{0};

    void* allocate(std::size_t size) {
        const std::size_t actual = size == 0 ? 1 : size;
        void* pointer = std::malloc(actual);
        if (!pointer) throw std::bad_alloc();
        if (g_measureAllocations.load(std::memory_order_relaxed)) {
            g_allocationCount.fetch_add(1, std::memory_order_relaxed);
            g_allocatedBytes.fetch_add(actual, std::memory_order_relaxed);
        }
        return pointer;
    }

}// namespace

void* operator new(std::size_t size) { return allocate(size); }
void* operator new[](std::size_t size) { return allocate(size); }
void operator delete(void* pointer) noexcept { std::free(pointer); }
void operator delete[](void* pointer) noexcept { std::free(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { std::free(pointer); }
void operator delete[](void* pointer, std::size_t) noexcept { std::free(pointer); }

namespace {

using namespace chrononmotion;
using namespace chrononmotion::motion;

    constexpr int kLayers = 250;
    constexpr int kNulls = 5;
    constexpr int kFrames = 600;    // 20 s @ 30 fps
    constexpr int kRepeats = 40;

    /// A checksum over every world matrix + world opacity at a frame, so a
    /// change in evaluation behaviour shows up as a changed digest.
    std::uint64_t digest(const MotionScene& scene) {

        std::uint64_t h = 1469598103934665603ull;
        const auto mix = [&h](std::uint64_t v) {
            h ^= v;
            h *= 1099511628211ull;
        };

        for (const auto& layer : scene.layers()) {
            const Matrix4& m = scene.worldMatrix(layer.id);
            for (const float e : m.elements) {
                std::uint32_t bits = 0;
                static_assert(sizeof bits == sizeof e, "float size");
                std::memcpy(&bits, &e, sizeof e);
                mix(bits);
            }
            mix(static_cast<std::uint64_t>(scene.worldOpacity(layer.id) * 1e6f));
            mix(scene.visible(layer.id) ? 1u : 0u);
        }
        return h;
    }

    MotionScene buildScene() {

        MotionScene scene("bench", 30.f);

        for (int i = 0; i < kNulls; ++i) {
            scene.addNull(100u + static_cast<Layer::Id>(i), "Null_" + std::to_string(i));
        }

        for (int i = 0; i < kLayers; ++i) {
            const auto id = static_cast<Layer::Id>(1000 + i);
            Layer& layer = scene.addContent(id, "L" + std::to_string(i),
                                            ContentRef::text("asset" + std::to_string(i), {640.f, 120.f}),
                                            100u + static_cast<Layer::Id>(i % kNulls));
            layer.transform.position.set(static_cast<float>(i % 10) * 2.f, static_cast<float>(i / 10) * 1.5f, 0.f);
            layer.alive(0, kFrames);

            presets::slideIn(layer, presets::Direction::Left, 0, 30, 6.f, 30.f);
            presets::fadeIn(layer, 0, 30, 30.f);
        }

        return scene;
    }

    double timeEvaluation(MotionScene& scene, std::uint64_t& firstDigest) {

        using clock = std::chrono::steady_clock;
        double totalNs = 0.0;
        std::uint64_t runDigest = 0;

        // Build the cached hierarchy before measuring. This separates one-time
        // authoring/cache preparation from the steady-state data plane.
        g_measureAllocations.store(false, std::memory_order_release);
        scene.evaluate(0);
        firstDigest = digest(scene);
        g_allocationCount.store(0, std::memory_order_relaxed);
        g_allocatedBytes.store(0, std::memory_order_relaxed);
        g_measureAllocations.store(true, std::memory_order_release);

        for (int repeat = 0; repeat < kRepeats; ++repeat) {
            const auto start = clock::now();
            for (int frame = 0; frame < kFrames; ++frame) {
                scene.evaluate(frame);
                if (repeat == kRepeats - 1 && frame == 0) runDigest = digest(scene);
            }
            const auto end = clock::now();
            totalNs += static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count());
        }

        g_measureAllocations.store(false, std::memory_order_release);

        if (runDigest != firstDigest) {
            std::fprintf(stderr, "NON-DETERMINISTIC: digest changed across runs!\n");
        }

        const double frameCount = static_cast<double>(kRepeats * kFrames);
        std::printf("allocations: %.3f/frame  %.1f bytes/frame\n",
                    static_cast<double>(g_allocationCount.load(std::memory_order_relaxed)) / frameCount,
                    static_cast<double>(g_allocatedBytes.load(std::memory_order_relaxed)) / frameCount);
        return totalNs / frameCount / 1000.0;
    }

}// namespace

int main() {

    MotionScene scene = buildScene();
    std::uint64_t d = 0;
    const double usPerFrame = timeEvaluation(scene, d);

    std::printf("layers=%d nulls=%d frames/run=%d repeats=%d\n", kLayers, kNulls, kFrames, kRepeats);
    std::printf("evaluate: %.2f us/frame  (%.0f frames/s single-thread)\n",
                usPerFrame, usPerFrame > 0.0 ? 1e6 / usPerFrame : 0.0);
    std::printf("digest: %016llx\n", static_cast<unsigned long long>(d));
    return 0;
}
