// ChrononMotion preview — composition root.
//
// Owns the three task lanes, the asset server, the playback clock, diagnostics
// and the debug command buffer. A windowed or headless frontend supplies the
// GL renderer; this object supplies the application state and one update path.

#ifndef CHRONONMOTION_PREVIEW_PREVIEWHOST_HPP
#define CHRONONMOTION_PREVIEW_PREVIEWHOST_HPP

#include "chrononmotion/preview/AssetServer.hpp"
#include "chrononmotion/preview/DebugDraw.hpp"
#include "chrononmotion/preview/Diagnostics.hpp"
#include "chrononmotion/preview/PlaybackClock.hpp"
#include "chrononmotion/preview/TaskPool.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace chrononmotion::preview {

    struct PreviewOptions {
        FrameRate fps{30, 1};
        int firstFrame{0};
        int frameCount{300};
        std::size_t ioThreads{1};
        std::size_t computeThreads{0};
        std::size_t asyncThreads{2};
        bool watchAssets{true};
        std::size_t diagnosticsHistory{180};
        int viewportWidth{1920};
        int viewportHeight{1080};
        bool startPlaying{true};
    };

    class PreviewHost {
    public:
        explicit PreviewHost(PreviewOptions options = {});
        ~PreviewHost() = default;

        PreviewHost(const PreviewHost&) = delete;
        PreviewHost& operator=(const PreviewHost&) = delete;
        PreviewHost(PreviewHost&&) = delete;
        PreviewHost& operator=(PreviewHost&&) = delete;

        [[nodiscard]] TaskPools& tasks() { return tasks_; }
        [[nodiscard]] AssetServer& assets() { return assets_; }
        [[nodiscard]] PlaybackClock& clock() { return clock_; }
        [[nodiscard]] Diagnostics& diagnostics() { return diagnostics_; }
        [[nodiscard]] DebugDraw& debugDraw() { return debug_; }
        [[nodiscard]] const DebugDraw& debugDraw() const { return debug_; }
        [[nodiscard]] const PreviewOptions& options() const { return options_; }

        void setDebugBuilder(std::function<void(DebugDraw&)> builder) {
            debugBuilder_ = std::move(builder);
        }
        void setFrameHandler(std::function<void(const FramePosition&)> handler) {
            frameHandler_ = std::move(handler);
        }
        void setViewport(int width, int height);

        void update(double wallSeconds);
        void update(std::chrono::nanoseconds wallDelta) {
            update(std::chrono::duration<double>(wallDelta).count());
        }

        [[nodiscard]] const std::vector<AssetEvent>& lastEvents() const { return lastEvents_; }
        [[nodiscard]] FramePosition lastPosition() const { return lastPosition_; }

    private:
        PreviewOptions options_;
        TaskPools tasks_;
        AssetServer assets_;
        PlaybackClock clock_;
        Diagnostics diagnostics_;
        DebugDraw debug_;
        std::vector<AssetEvent> lastEvents_{};
        FramePosition lastPosition_{};
        std::function<void(DebugDraw&)> debugBuilder_{};
        std::function<void(const FramePosition&)> frameHandler_{};
        std::uint64_t frameCounter_{0};
    };

}// namespace chrononmotion::preview

#endif//CHRONONMOTION_PREVIEW_PREVIEWHOST_HPP
