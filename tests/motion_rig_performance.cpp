#include "chrononmotion/motion/Motion.hpp"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <sys/resource.h>

namespace {
    std::atomic<bool> measuring{false};
    std::atomic<std::size_t> allocations{0}, bytes{0};
    void* allocate(std::size_t size) {
        void* pointer = std::malloc(size ? size : 1);
        if (!pointer) throw std::bad_alloc();
        if (measuring.load(std::memory_order_relaxed)) {
            allocations.fetch_add(1, std::memory_order_relaxed);
            bytes.fetch_add(size, std::memory_order_relaxed);
        }
        return pointer;
    }
}
void* operator new(std::size_t size) { return allocate(size); }
void* operator new[](std::size_t size) { return allocate(size); }
void operator delete(void* pointer) noexcept { std::free(pointer); }
void operator delete[](void* pointer) noexcept { std::free(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { std::free(pointer); }
void operator delete[](void* pointer, std::size_t) noexcept { std::free(pointer); }

int main() {
    using namespace chrononmotion;
    using namespace chrononmotion::motion;
    using Clock = std::chrono::steady_clock;
    RigGraph graph;
    RigDriverGraph drivers;
    for (unsigned i = 0; i < 100; ++i) {
        RigNode bone; bone.id = i + 1; bone.parent = i;
        bone.rest.position = {1.f, 0.f, 0.f}; bone.bind = bone.rest;
        ConstraintSpec limit; limit.kind = ConstraintKind::LimitRotation;
        limit.minimum = {-1.f, -1.f, -1.f}; limit.maximum = {1.f, 1.f, 1.f};
        bone.constraints.push_back(limit); graph.nodes.push_back(bone);
    }
    for (unsigned i = 0; i < 20; ++i) {
        RigNode node; node.id = 101 + i; node.kind = RigNodeKind::Controller; graph.nodes.push_back(node);
        RigController controller; controller.id = node.id; drivers.controllers.push_back(controller);
        RigDriverNode driver; driver.id = i + 1; driver.kind = RigDriverKind::Controller;
        driver.controller = controller.id; drivers.nodes.push_back(driver);
        drivers.outputs.push_back({driver.id, i + 1, RigDrivenProperty::Rotation, 2});
    }
    const auto rig = graph.compile();
    const RigDriverEvaluator driverEvaluator(rig, drivers);
    DeformerStack stack;
    for (unsigned i = 0; i < 10; ++i) {
        RigDeformer op; op.strength = .01f; op.phase = static_cast<float>(i); stack.operators.push_back(op);
    }
    std::vector<Vector3> geometry(100, {.1f, .2f, 0.f});
    RigEvaluation reusable;
    rig.evaluate({}, reusable);
    allocations = 0; bytes = 0; measuring = true;
    const auto fkStart = Clock::now();
    for (unsigned frame = 0; frame < 1000; ++frame) rig.evaluate({}, reusable);
    const auto fkEnd = Clock::now();
    measuring = false;
    const auto fkAllocations = allocations.load();
    const auto fkBytes = bytes.load();
    if (fkAllocations != 0 || fkBytes != 0) {
        std::printf("FAIL: warmed FK allocates %zu times, %zu bytes\n", fkAllocations, fkBytes);
        return 1;
    }
    std::size_t passAllocations[2]{}, passBytes[2]{};
    std::vector<RigPose> frames;
    frames.reserve(1000);
    const auto evalStart = Clock::now();
    for (unsigned pass = 0; pass < 2; ++pass) {
        allocations = 0; bytes = 0; measuring = true;
        for (unsigned frame = 0; frame < 1000; ++frame) {
            const auto pose = driverEvaluator.evaluate(static_cast<float>(frame) / 30.f);
            rig.evaluate(pose, reusable);
            const auto deformed = stack.evaluate(geometry);
            if (deformed.size() != geometry.size()) return 1;
        }
        measuring = false;
        passAllocations[pass] = allocations; passBytes[pass] = bytes;
    }
    const auto evalEnd = Clock::now();
    if (passAllocations[0] != passAllocations[1] || passBytes[0] != passBytes[1]) {
        std::printf("FAIL: repeated work has growing allocation volume\n"); return 1;
    }
    for (unsigned frame = 0; frame < 1000; ++frame)
        frames.push_back(driverEvaluator.evaluate(static_cast<float>(frame) / 30.f));
    allocations = 0; bytes = 0; measuring = true;
    const auto bakeStart = Clock::now();
    const auto baked = rig.bake(frames, 0, 30.f);
    const auto bakeEnd = Clock::now();
    measuring = false;
    struct rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) != 0) return 1;
    std::printf("rig corpus: 100 bones, 100 constraints, 20 controllers, 10 deformers, 1000 frames\n"
                "FK: %.3f ms, %zu allocations, %zu bytes (warmed)\n"
                "full evaluation: %.3f ms/pass, %zu allocations/pass, %zu bytes/pass (bounded repeat)\n"
                "bake: %.3f ms, %zu allocations, %zu bytes; peak RSS: %ld KiB\n",
                std::chrono::duration<double, std::milli>(fkEnd - fkStart).count(), fkAllocations, fkBytes,
                std::chrono::duration<double, std::milli>(evalEnd - evalStart).count() / 2., passAllocations[0], passBytes[0],
                std::chrono::duration<double, std::milli>(bakeEnd - bakeStart).count(), allocations.load(), bytes.load(), usage.ru_maxrss);
    return baked.lastFrame == 999 ? 0 : 1;
}
