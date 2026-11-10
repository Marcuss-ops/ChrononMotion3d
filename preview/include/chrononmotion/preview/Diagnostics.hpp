// ChrononMotion preview — runtime diagnostics.
//
// The preview equivalent of Bevy's Diagnostics + a tiny text overlay: frame
// timings, draw call/triangle counts, asset counts and memory fields, a bounded
// ring buffer, JSON for automation, and an overlay built from DebugDraw lines
// so it needs no font asset or UI toolkit.

#ifndef CHRONONMOTION_PREVIEW_DIAGNOSTICS_HPP
#define CHRONONMOTION_PREVIEW_DIAGNOSTICS_HPP

#include "chrononmotion/preview/DebugDraw.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <string>
#include <string_view>

namespace chrononmotion::preview {

    struct FrameStats {
        std::uint64_t frame{0};
        double frameMs{0.0};
        double updateMs{0.0};
        double evaluateMs{0.0};
        double renderMs{0.0};
        double gpuMs{0.0};
        std::uint64_t drawCalls{0};
        std::uint64_t triangles{0};
        std::uint64_t assetsLoaded{0};
        std::uint64_t assetsLoading{0};
        std::uint64_t assetBytes{0};
        std::uint64_t cpuBytes{0};
        std::uint64_t gpuBytes{0};
        std::size_t layers{0};
        std::size_t lights{0};
    };

    class Diagnostics {
    public:
        explicit Diagnostics(std::size_t historyCapacity = 180);

        void beginFrame(std::uint64_t frame = 0);
        /// Close the current frame. A negative `frameMs` measures the time
        /// since beginFrame(); a non-negative value lets an external loop
        /// supply its own frame duration.
        void endFrame(double frameMs = -1.0);

        void recordUpdate(double milliseconds) { current_.updateMs += milliseconds; }
        void recordEvaluate(double milliseconds) { current_.evaluateMs += milliseconds; }
        void recordRender(double milliseconds) { current_.renderMs += milliseconds; }
        void recordGpu(double milliseconds) { current_.gpuMs += milliseconds; }

        void setDrawCalls(std::uint64_t value) { current_.drawCalls = value; }
        void setTriangles(std::uint64_t value) { current_.triangles = value; }
        void setAssetsLoaded(std::uint64_t value) { current_.assetsLoaded = value; }
        void setAssetsLoading(std::uint64_t value) { current_.assetsLoading = value; }
        void setAssetBytes(std::uint64_t value) { current_.assetBytes = value; }
        void setCpuBytes(std::uint64_t value) { current_.cpuBytes = value; }
        void setGpuBytes(std::uint64_t value) { current_.gpuBytes = value; }
        void setLayers(std::size_t value) { current_.layers = value; }
        void setLights(std::size_t value) { current_.lights = value; }
        void addCounter(std::string name, double value) { counters_[std::move(name)] = value; }

        [[nodiscard]] const FrameStats& current() const { return current_; }
        [[nodiscard]] const std::deque<FrameStats>& history() const { return history_; }
        [[nodiscard]] const std::map<std::string, double>& counters() const { return counters_; }

        [[nodiscard]] double averageFrameMs() const;
        [[nodiscard]] double minFrameMs() const;
        [[nodiscard]] double maxFrameMs() const;
        [[nodiscard]] double fps() const;

        void reset();

        [[nodiscard]] std::string toJson(int indentSpaces = 2) const;

        /// Build the overlay into `draw` in NDC space. The backdrop is a
        /// low-alpha filled rectangle, the labels are the built-in 3x5 font.
        void buildOverlay(DebugDraw& draw, int width, int height,
                          std::string_view title = "CHRONONMOTION PREVIEW") const;

    private:
        std::size_t historyCapacity_;
        std::deque<FrameStats> history_{};
        FrameStats current_{};
        std::map<std::string, double> counters_{};
        std::chrono::steady_clock::time_point frameStart_{};
        bool inFrame_{false};
    };

}// namespace chrononmotion::preview

#endif//CHRONONMOTION_PREVIEW_DIAGNOSTICS_HPP
