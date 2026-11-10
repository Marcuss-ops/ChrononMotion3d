#include "chrononmotion/preview/PreviewHost.hpp"

#include <chrono>
#include <stdexcept>
#include <utility>

namespace chrononmotion::preview {

    PreviewHost::PreviewHost(PreviewOptions options)
        : options_(std::move(options)),
          tasks_(options_.ioThreads, options_.computeThreads, options_.asyncThreads),
          assets_(tasks_.io),
          clock_(options_.fps, options_.firstFrame, options_.frameCount),
          diagnostics_(options_.diagnosticsHistory),
          debug_() {
        if (options_.viewportWidth <= 0 || options_.viewportHeight <= 0)
            throw std::invalid_argument("PreviewHost: viewport must be positive");
        if (options_.startPlaying) clock_.play();
        if (options_.watchAssets) assets_.startWatching();
    }

    void PreviewHost::setViewport(int width, int height) {
        if (width <= 0 || height <= 0)
            throw std::invalid_argument("PreviewHost::setViewport: size must be positive");
        options_.viewportWidth = width;
        options_.viewportHeight = height;
    }

    void PreviewHost::update(double wallSeconds) {
        const auto start = std::chrono::steady_clock::now();
        diagnostics_.beginFrame(frameCounter_++);

        if (options_.watchAssets) assets_.pollOnce();
        lastEvents_ = assets_.pollEvents();

        lastPosition_ = clock_.advanceSeconds(wallSeconds);
        if (frameHandler_) frameHandler_(lastPosition_);

        diagnostics_.setAssetsLoaded(assets_.count(AssetState::Ready));
        diagnostics_.setAssetsLoading(assets_.count(AssetState::Loading));
        diagnostics_.setAssetBytes(assets_.totalBytes());

        debug_.clear();
        if (debugBuilder_) debugBuilder_(debug_);
        diagnostics_.buildOverlay(debug_, options_.viewportWidth, options_.viewportHeight);

        const auto elapsed = std::chrono::steady_clock::now() - start;
        diagnostics_.recordUpdate(std::chrono::duration<double, std::milli>(elapsed).count());
        diagnostics_.endFrame();
    }

}// namespace chrononmotion::preview
