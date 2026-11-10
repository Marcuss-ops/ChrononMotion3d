#include "chrononmotion/preview/AssetServer.hpp"
#include "chrononmotion/preview/DebugDraw.hpp"
#include "chrononmotion/preview/Diagnostics.hpp"
#include "chrononmotion/preview/PlaybackClock.hpp"
#include "chrononmotion/preview/PreviewHost.hpp"
#include "chrononmotion/preview/TaskPool.hpp"

#include "chrononmotion/motion/CameraRig.hpp"
#include "chrononmotion/motion/LightRig.hpp"
#include "chrononmotion/motion/MotionScene.hpp"
#include "chrononmotion/motion/RigGraph.hpp"

#include "motion_check.hpp"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

    namespace check = chrononmotion_test;
    using namespace chrononmotion;
    using namespace chrononmotion::preview;

    std::filesystem::path makeTempDirectory() {
        static std::atomic<unsigned long long> counter{0};
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        for (int attempt = 0; attempt < 100; ++attempt) {
            const auto path = std::filesystem::temp_directory_path() /
                              ("chrononmotion_preview_" + std::to_string(stamp) + "_" +
                               std::to_string(counter.fetch_add(1)));
            std::error_code error;
            if (std::filesystem::create_directory(path, error)) return path;
        }
        throw std::runtime_error("could not create a temporary directory");
    }

    class TempDir {
    public:
        TempDir() : path_(makeTempDirectory()) {}
        ~TempDir() {
            std::error_code error;
            std::filesystem::remove_all(path_, error);
        }

        [[nodiscard]] std::string file(std::string_view name) const {
            return (path_ / std::string(name)).string();
        }

        void write(std::string_view name, std::string_view content) const {
            std::ofstream out(file(name), std::ios::binary | std::ios::trunc);
            if (!out) throw std::runtime_error("could not write test asset");
            out.write(content.data(), static_cast<std::streamsize>(content.size()));
        }

        void remove(std::string_view name) const {
            std::error_code error;
            std::filesystem::remove(file(name), error);
        }

    private:
        std::filesystem::path path_;
    };

    struct TestAsset {
        std::string value;
        int size{0};
    };

    template<class T>
    bool hasEvent(const std::vector<AssetEvent>& events, AssetEventKind kind,
                  AssetHandle<T> handle) {
        for (const AssetEvent& event : events) {
            if (event.kind == kind && event.type == std::type_index(typeid(T)) &&
                event.index == handle.index && event.generation == handle.generation) {
                return true;
            }
        }
        return false;
    }

    bool near(double a, double b, double epsilon = 1e-9) {
        return std::fabs(a - b) <= epsilon;
    }

    void testTaskPools() {
        check::section("TaskPool");

        TaskPool pool(4, "test");
        check::check(pool.workerCount() == 4, "worker count is stable");

        std::vector<std::future<int>> futures;
        for (int i = 0; i < 16; ++i) {
            futures.push_back(pool.submit([i] { return i * i; }));
        }
        for (int i = 0; i < 16; ++i) {
            check::check(futures[static_cast<std::size_t>(i)].get() == i * i,
                         "submitted future returns the task value");
        }

        const auto mapped = pool.parallelMap<int>(1000, [](std::size_t i) {
            return static_cast<int>(i * 2);
        });
        check::check(mapped.size() == 1000, "parallelMap preserves count");
        bool ordered = true;
        for (std::size_t i = 0; i < mapped.size(); ++i) {
            ordered = ordered && mapped[i] == static_cast<int>(i * 2);
        }
        check::check(ordered, "parallelMap preserves index order");

        const long long reduced = pool.parallelReduce<long long>(
                10'000, 0LL,
                [](std::size_t i) { return static_cast<long long>(i + 1); },
                [](long long a, long long b) { return a + b; });
        check::check(reduced == 50'005'000LL, "parallelReduce is a deterministic left fold");

        std::atomic<int> calls{0};
        pool.parallelFor(257, [&calls](std::size_t) { calls.fetch_add(1); });
        check::check(calls.load() == 257, "parallelFor visits every index once");

        auto failing = pool.submit([]() -> int { throw std::runtime_error("expected"); });
        bool caught = false;
        try {
            (void)failing.get();
        } catch (const std::runtime_error&) {
            caught = true;
        }
        check::check(caught, "task exceptions reach the future");

        pool.waitIdle();
        TaskPools lanes(1, 2, 1);
        check::check(lanes.io.workerCount() == 1, "IO lane is configurable");
        check::check(lanes.compute.workerCount() == 2, "compute lane is configurable");
        check::check(lanes.async.workerCount() == 1, "async lane is configurable");
        lanes.waitIdle();
    }

    void testPlaybackClock() {
        check::section("PlaybackClock");

        PlaybackClock clock(FrameRate{30, 1}, 0, 300);
        check::check(clock.position() == FramePosition{0, 0, 0.0},
                     "clock starts at the first frame");

        clock.play();
        const FramePosition oneSecond = clock.advanceSeconds(1.0);
        check::check(oneSecond.frame == 30, "one second at 30 fps is frame 30");
        check::check(oneSecond.subFrameUnits == 0, "one second lands on a frame boundary");
        check::check(near(oneSecond.seconds, 1.0), "frame time projects back to one second");

        clock.setRate(2.0);
        const FramePosition doubled = clock.advanceSeconds(0.5);
        check::check(doubled.frame == 60, "rate 2 advances two frames per frame");

        clock.pause();
        const FramePosition paused = clock.advanceSeconds(1.0);
        check::check(paused.frame == 60, "paused clock does not advance");

        clock.seek(10, 500'000);
        const FramePosition halfway = clock.position();
        check::check(halfway.frame == 10 && halfway.subFrameUnits == 500'000,
                     "seek preserves the sub-frame");
        check::check(near(halfway.seconds, 10.5 / 30.0), "sub-frame seconds are derived exactly");

        PlaybackClock looping(FrameRate{30, 1}, 0, 30);
        looping.setLoop(true);
        looping.seek(29, 900'000);
        looping.play();
        const FramePosition wrapped = looping.advanceSeconds(0.1);
        check::check(wrapped.frame == 2 && wrapped.subFrameUnits == 900'000,
                     "loop wraps the playhead into the range");

        PlaybackClock seeking(FrameRate{30, 1}, 0, 300);
        seeking.seekSeconds(1.0);
        check::check(seeking.position().frame == 30, "seekSeconds uses the frame calendar");

        PlaybackClock fixed(FrameRate{30, 1}, 0, 300);
        fixed.play();
        fixed.setFixedStep(0.1);
        int steps = 0;
        double consumed = 0.0;
        const std::size_t count = fixed.advanceFixed(0.31, [&](double step) {
            ++steps;
            consumed += step;
        });
        check::check(count == 3 && steps == 3, "fixed-step accumulator consumes complete steps");
        check::check(near(consumed, 0.3), "fixed-step callback receives the step duration");
        fixed.pause();
        check::check(fixed.advanceFixed(1.0, [](double) {}) == 0,
                     "paused clock does not consume fixed steps");

        check::check(frameRateFromDouble(30.0).numerator == 30,
                     "frameRateFromDouble keeps an integer rate");
        check::check(!clock.describe().empty(), "clock has a diagnostic description");
    }

    void testAssetServer() {
        check::section("AssetServer");

        TempDir dir;
        TaskPool io(2, "asset-test");
        AssetServer server(io);
        server.registerLoader<TestAsset>([](AssetLoadContext& context) {
            auto asset = std::make_shared<TestAsset>();
            asset->value.assign(reinterpret_cast<const char*>(context.bytes.data()),
                                context.bytes.size());
            asset->size = static_cast<int>(context.bytes.size());
            return asset;
        });

        const std::string path = dir.file("asset.txt");
        dir.write("asset.txt", "alpha");
        const AssetHandle<TestAsset> handle = server.load<TestAsset>(path);
        check::check(handle.valid(), "load returns a valid handle");
        check::check(server.wait(handle), "load completes");
        check::check(server.state(handle) == AssetState::Ready, "loaded asset is Ready");
        check::check(server.get(handle) != nullptr && server.get(handle)->value == "alpha",
                     "loaded bytes reach the asset");
        auto events = server.pollEvents();
        check::check(hasEvent(events, AssetEventKind::Added, handle),
                     "initial load emits Added");

        dir.write("asset.txt", "beta-longer");
        server.pollOnce();
        check::check(server.wait(handle), "modified load completes");
        check::check(server.get(handle)->value == "beta-longer",
                     "hot reload replaces the asset data");
        events = server.pollEvents();
        check::check(hasEvent(events, AssetEventKind::Modified, handle),
                     "file change emits Modified");

        dir.write("child.txt", "child-one");
        const auto child = server.load<TextAsset>(dir.file("child.txt"));
        check::check(server.wait(child), "child asset loads");

        dir.write("parent.txt", "parent");
        const auto parent = server.load<TextAsset>(dir.file("parent.txt"));
        check::check(server.wait(parent), "parent asset loads");
        server.addDependency(parent, child);
        (void)server.pollEvents();

        dir.write("child.txt", "child-two-longer");
        server.pollOnce();
        check::check(server.wait(child), "dependency reload completes");
        check::check(server.wait(parent), "dependent reload completes");
        events = server.pollEvents();
        check::check(hasEvent(events, AssetEventKind::Modified, child),
                     "dependency emits Modified");
        check::check(hasEvent(events, AssetEventKind::Modified, parent),
                     "dependent receives Modified");

        bool cycleRejected = false;
        try {
            server.addDependency(child, parent);
        } catch (const std::invalid_argument&) {
            cycleRejected = true;
        }
        check::check(cycleRejected, "dependency cycles are rejected");

        {
            auto lease = server.acquire(parent);
            check::check(lease.valid() && lease.get()->text == "parent",
                         "lease keeps the asset readable");
        }
        server.collectGarbage();
        check::check(server.state(parent) == AssetState::Unknown,
                     "released unleased asset is collected");
        events = server.pollEvents();
        check::check(hasEvent(events, AssetEventKind::Removed, parent),
                     "garbage collection emits Removed");

        dir.write("unload.txt", "unload");
        const auto unloadHandle = server.load<TextAsset>(dir.file("unload.txt"));
        check::check(server.wait(unloadHandle), "unload fixture loads");
        server.unload(unloadHandle);
        check::check(server.state(unloadHandle) == AssetState::Unknown,
                     "explicit unload invalidates the handle");
        events = server.pollEvents();
        check::check(hasEvent(events, AssetEventKind::Removed, unloadHandle),
                     "explicit unload emits Removed");

        const auto missing = server.load<TextAsset>(dir.file("missing.txt"));
        check::check(server.wait(missing), "missing asset settles");
        check::check(server.state(missing) == AssetState::Failed,
                     "missing asset is Failed");
        events = server.pollEvents();
        check::check(hasEvent(events, AssetEventKind::Failed, missing),
                     "missing asset emits Failed");

        dir.write("removed.txt", "removed");
        const auto removed = server.load<TextAsset>(dir.file("removed.txt"));
        check::check(server.wait(removed), "removal fixture loads");
        (void)server.pollEvents();
        dir.remove("removed.txt");
        server.pollOnce();
        check::check(server.state(removed) == AssetState::Unknown,
                     "deleted file invalidates the handle");
        events = server.pollEvents();
        check::check(hasEvent(events, AssetEventKind::Removed, removed),
                     "deleted file emits Removed");

        chrononmotion::motion::MotionScene scene("preview-scene", 30.f);
        scene.addNull(1, "root");
        dir.write("scene.json", scene.toJson());
        const auto sceneHandle = server.load<MotionSceneAsset>(dir.file("scene.json"));
        check::check(server.wait(sceneHandle), "MotionSceneAsset loads");
        check::check(server.get(sceneHandle) != nullptr &&
                             server.get(sceneHandle)->scene != nullptr &&
                             server.get(sceneHandle)->scene->name() == "preview-scene",
                     "MotionSceneAsset parses the project file");

        server.startWatching(std::chrono::milliseconds(30));
        check::check(server.watching(), "watcher starts");
        server.stopWatching();
        check::check(!server.watching(), "watcher stops");
    }

    void testDebugDraw() {
        check::section("DebugDraw");

        using namespace chrononmotion;
        DebugDraw draw;
        const Color color{0.2f, 0.8f, 1.f};
        check::check(draw.empty(), "debug draw starts empty");
        draw.line({0, 0, 0}, {1, 0, 0}, color);
        check::check(draw.lineCount() == 1, "line emits one line");
        check::check(draw.vertices().size() == 2, "line emits two vertices");

        draw.clear();
        draw.box(Box3({0, 0, 0}, {2, 3, 4}), color);
        check::check(draw.lineCount() == 12, "box emits twelve edges");
        const Box3 bounds = draw.bounds();
        check::check(bounds.min().x == 0.f && bounds.max().z == 4.f, "box bounds span the corners");

        draw.clear();
        draw.grid(2, 1.f, color);
        check::check(draw.lineCount() == 10, "grid emits two directions per line");

        draw.clear();
        const std::array<Vector3, 8> corners{
                Vector3{-1, -1, -1}, Vector3{1, -1, -1}, Vector3{1, 1, -1}, Vector3{-1, 1, -1},
                Vector3{-1, -1, 1}, Vector3{1, -1, 1}, Vector3{1, 1, 1}, Vector3{-1, 1, 1}};
        draw.frustum(corners, color);
        check::check(draw.lineCount() == 12, "frustum emits twelve edges");

        draw.clear();
        draw.text({0, 0, 0}, "CM3D 60", color, 0.05f);
        check::check(draw.lineCount() > 0, "text emits glyph lines");
        check::check(draw.lineCount() < 500, "text stays compact");

        draw.clear();
        draw.rectFilled({-1, -1, 0}, {1, 1, 0}, color, 0.5f, 4);
        check::check(draw.lineCount() == 4, "filled rect emits one line per row");

        draw.clear();
        motion::CameraRig rig(10, 50.f, 1.5f, 0.1f, 50.f);
        rig.setPosition({0.f, 0.f, 5.f});
        rig.setTarget({0.f, 0.f, 0.f});
        draw.cameraRig(rig, 0.f, color);
        check::check(draw.lineCount() > 12, "camera helper draws frustum and target");

        draw.clear();
        motion::LightRig lightRig;
        lightRig.id = "rig";
        lightRig.name = "rig";
        motion::LightRigSource source;
        source.id = 1;
        source.name = "spot";
        source.kind = motion::LightKind::Spot;
        source.local.position = {0.f, 2.f, 0.f};
        source.direction = {0.f, -1.f, 0.f};
        source.range = 5.f;
        source.angle = 0.5f;
        lightRig.sources.push_back(source);
        draw.lightRig(lightRig, 0.f, color);
        check::check(draw.lineCount() > 0, "light helper draws a source");
        check::check(draw.bounds().containsPoint({0.f, 2.f, 0.f}),
                     "light helper uses the sampled world position");

        draw.clear();
        motion::RigGraph graph;
        motion::RigNode root;
        root.id = 1;
        root.kind = motion::RigNodeKind::Bone;
        root.length = 1.f;
        root.primaryAxis = {1.f, 0.f, 0.f};
        motion::RigNode child;
        child.id = 2;
        child.parent = 1;
        child.kind = motion::RigNodeKind::Bone;
        child.length = 1.f;
        child.primaryAxis = {1.f, 0.f, 0.f};
        graph.nodes.push_back(root);
        graph.nodes.push_back(child);
        draw.rigGraph(graph, color);
        check::check(draw.lineCount() > 0, "rig helper draws bones and joints");

        draw.clear();
        motion::MotionScene scene("debug", 30.f);
        auto& first = scene.addNull(1, "first");
        first.tracks.position.add(0.f, {0.f, 0.f, 0.f});
        first.tracks.position.add(1.f, {1.f, 0.f, 0.f});
        scene.addNull(2, "second", 1);
        scene.evaluate(0);
        draw.motionScene(scene, 0, color, true);
        check::check(draw.lineCount() > 0, "scene helper draws layers and paths");
    }

    void testDiagnostics() {
        check::section("Diagnostics");

        Diagnostics diagnostics(3);
        for (int i = 0; i < 5; ++i) {
            diagnostics.beginFrame(static_cast<std::uint64_t>(i));
            diagnostics.setDrawCalls(static_cast<std::uint64_t>(i * 10));
            diagnostics.setTriangles(static_cast<std::uint64_t>(i * 100));
            diagnostics.addCounter("custom", 42.0);
            diagnostics.endFrame(10.0 + i);
        }
        check::check(diagnostics.history().size() == 3, "history respects its capacity");
        check::check(diagnostics.current().frame == 4, "current frame is the last frame");
        check::check(near(diagnostics.averageFrameMs(), 13.0), "average uses the retained history");
        check::check(near(diagnostics.minFrameMs(), 12.0), "minimum frame time");
        check::check(near(diagnostics.maxFrameMs(), 14.0), "maximum frame time");
        check::check(near(diagnostics.fps(), 1000.0 / 13.0), "fps derives from the average");

        const std::string json = diagnostics.toJson();
        check::check(json.find("\"frame_ms\"") != std::string::npos, "JSON has frame timing");
        check::check(json.find("\"history\"") != std::string::npos, "JSON has history");
        check::check(json.find("\"custom\": 42") != std::string::npos, "JSON has counters");

        DebugDraw draw;
        diagnostics.buildOverlay(draw, 640, 360);
        check::check(draw.lineCount() > 0, "overlay emits backdrop and text");
        diagnostics.reset();
        check::check(diagnostics.history().empty() && near(diagnostics.averageFrameMs(), 0.0),
                     "reset clears history");
    }

    void testPreviewHost() {
        check::section("PreviewHost");

        PreviewOptions options;
        options.fps = FrameRate{30, 1};
        options.frameCount = 300;
        options.watchAssets = false;
        options.viewportWidth = 640;
        options.viewportHeight = 360;
        PreviewHost host(options);

        int frames = 0;
        host.setFrameHandler([&frames](const FramePosition&) { ++frames; });
        host.setDebugBuilder([](DebugDraw& draw) {
            draw.line({-1.f, -1.f, 0.f}, {1.f, 1.f, 0.f}, Color{1.f, 0.3f, 0.1f});
        });

        host.update(1.0 / 30.0);
        check::check(frames == 1, "frame handler runs once per update");
        check::check(host.lastPosition().frame == 1, "host advances the playback clock");
        check::check(host.diagnostics().history().size() == 1, "host records diagnostics");
        check::check(host.debugDraw().lineCount() > 1, "host builds debug draw and overlay");
        check::check(host.tasks().io.workerCount() >= 1, "host owns the task lanes");

        host.clock().pause();
        host.update(1.0);
        check::check(frames == 2, "paused host still runs the frame handler");
        check::check(host.lastPosition().frame == 1, "paused host keeps the playhead");
        check::check(host.diagnostics().history().size() == 2, "host history grows");
        host.setViewport(800, 600);
    }

}// namespace

int main() {
    try {
        testTaskPools();
        testPlaybackClock();
        testAssetServer();
        testDebugDraw();
        testDiagnostics();
        testPreviewHost();
    } catch (const std::exception& error) {
        std::printf("uncaught test exception: %s\n", error.what());
        return 1;
    }
    return check::report();
}
