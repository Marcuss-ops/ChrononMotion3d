#include "chrononmotion/preview/Diagnostics.hpp"

#include <algorithm>
#include <iomanip>
#include <limits>
#include <sstream>
#include <utility>
#include <vector>

namespace chrononmotion::preview {
    namespace {

        std::string fixed(double value) {
            std::ostringstream out;
            out << std::fixed << std::setprecision(2) << value;
            return out.str();
        }

        std::string bytesAsMb(std::uint64_t bytes) {
            return fixed(static_cast<double>(bytes) / (1024.0 * 1024.0));
        }

    }// namespace

    Diagnostics::Diagnostics(std::size_t historyCapacity)
        : historyCapacity_(std::max<std::size_t>(1, historyCapacity)) {}

    void Diagnostics::beginFrame(std::uint64_t frame) {
        current_ = FrameStats{};
        current_.frame = frame;
        frameStart_ = std::chrono::steady_clock::now();
        inFrame_ = true;
    }

    void Diagnostics::endFrame(double frameMs) {
        if (!inFrame_) return;
        if (frameMs < 0.0) {
            const auto elapsed = std::chrono::steady_clock::now() - frameStart_;
            frameMs = std::chrono::duration<double, std::milli>(elapsed).count();
        }
        current_.frameMs = frameMs;
        history_.push_back(current_);
        while (history_.size() > historyCapacity_) history_.pop_front();
        inFrame_ = false;
    }

    double Diagnostics::averageFrameMs() const {
        if (history_.empty()) return 0.0;
        double total = 0.0;
        for (const FrameStats& frame : history_) total += frame.frameMs;
        return total / static_cast<double>(history_.size());
    }

    double Diagnostics::minFrameMs() const {
        if (history_.empty()) return 0.0;
        double value = std::numeric_limits<double>::infinity();
        for (const FrameStats& frame : history_) value = std::min(value, frame.frameMs);
        return value;
    }

    double Diagnostics::maxFrameMs() const {
        if (history_.empty()) return 0.0;
        double value = 0.0;
        for (const FrameStats& frame : history_) value = std::max(value, frame.frameMs);
        return value;
    }

    double Diagnostics::fps() const {
        const double average = averageFrameMs();
        return average > 0.0 ? 1000.0 / average : 0.0;
    }

    void Diagnostics::reset() {
        history_.clear();
        current_ = FrameStats{};
        counters_.clear();
        inFrame_ = false;
    }

    std::string Diagnostics::toJson(int indentSpaces) const {
        const std::string indent(static_cast<std::size_t>(std::max(0, indentSpaces)), ' ');
        std::ostringstream out;
        out << std::fixed << std::setprecision(3);
        out << "{\n";
        out << indent << "\"frame\": " << current_.frame << ",\n";
        out << indent << "\"frame_ms\": " << current_.frameMs << ",\n";
        out << indent << "\"update_ms\": " << current_.updateMs << ",\n";
        out << indent << "\"evaluate_ms\": " << current_.evaluateMs << ",\n";
        out << indent << "\"render_ms\": " << current_.renderMs << ",\n";
        out << indent << "\"gpu_ms\": " << current_.gpuMs << ",\n";
        out << indent << "\"draw_calls\": " << current_.drawCalls << ",\n";
        out << indent << "\"triangles\": " << current_.triangles << ",\n";
        out << indent << "\"assets_loaded\": " << current_.assetsLoaded << ",\n";
        out << indent << "\"assets_loading\": " << current_.assetsLoading << ",\n";
        out << indent << "\"asset_bytes\": " << current_.assetBytes << ",\n";
        out << indent << "\"cpu_bytes\": " << current_.cpuBytes << ",\n";
        out << indent << "\"gpu_bytes\": " << current_.gpuBytes << ",\n";
        out << indent << "\"layers\": " << current_.layers << ",\n";
        out << indent << "\"lights\": " << current_.lights << ",\n";
        out << indent << "\"average_frame_ms\": " << averageFrameMs() << ",\n";
        out << indent << "\"min_frame_ms\": " << minFrameMs() << ",\n";
        out << indent << "\"max_frame_ms\": " << maxFrameMs() << ",\n";
        out << indent << "\"fps\": " << fps() << ",\n";
        out << indent << "\"history\": [";
        for (std::size_t i = 0; i < history_.size(); ++i) {
            if (i != 0) out << ", ";
            out << history_[i].frameMs;
        }
        out << "],\n";
        out << indent << "\"counters\": {";
        std::size_t index = 0;
        for (const auto& [name, value] : counters_) {
            if (index++ != 0) out << ", ";
            out << '"' << name << "\": " << value;
        }
        out << "}\n}\n";
        return out.str();
    }

    void Diagnostics::buildOverlay(DebugDraw& draw, int width, int height,
                                   std::string_view title) const {
        if (width <= 0 || height <= 0) return;
        const float scale = 8.f * 2.f / static_cast<float>(height);
        const float lineStep = 7.f * scale;
        const float x = -0.98f;
        float y = 0.97f;

        std::vector<std::string> lines;
        lines.emplace_back(title);
        lines.push_back("FPS " + fixed(fps()) + "  AVG " + fixed(averageFrameMs()) + " MS");
        lines.push_back("FRAME " + std::to_string(current_.frame) + "  " +
                        fixed(current_.frameMs) + " MS  MIN " + fixed(minFrameMs()) +
                        "  MAX " + fixed(maxFrameMs()));
        lines.push_back("UPDATE " + fixed(current_.updateMs) + "  EVAL " +
                        fixed(current_.evaluateMs) + "  RENDER " + fixed(current_.renderMs) +
                        "  GPU " + fixed(current_.gpuMs) + " MS");
        lines.push_back("DRAW " + std::to_string(current_.drawCalls) + "  TRIS " +
                        std::to_string(current_.triangles) + "  LAYERS " +
                        std::to_string(current_.layers) + "  LIGHTS " +
                        std::to_string(current_.lights));
        lines.push_back("ASSETS " + std::to_string(current_.assetsLoaded) + " READY " +
                        std::to_string(current_.assetsLoading) + " LOADING  " +
                        bytesAsMb(current_.assetBytes) + " MB");
        lines.push_back("CPU " + bytesAsMb(current_.cpuBytes) + " MB  GPU " +
                        bytesAsMb(current_.gpuBytes) + " MB");
        for (const auto& [name, value] : counters_) {
            lines.push_back(name + " " + fixed(value));
        }

        std::size_t maxLength = 0;
        for (const std::string& line : lines) maxLength = std::max(maxLength, line.size());

        const Vector3 backdropMin{x - 6.f * scale,
                                  y - static_cast<float>(lines.size()) * lineStep - 4.f * scale,
                                  0.f};
        const Vector3 backdropMax{x + static_cast<float>(maxLength) * 4.f * scale + 6.f * scale,
                                  y + 6.f * scale,
                                  0.f};
        draw.rectFilled(backdropMin, backdropMax, Color{0.01f, 0.02f, 0.025f}, 0.68f, 40);

        const Color titleColor{0.55f, 0.95f, 1.f};
        const Color bodyColor{0.45f, 1.f, 0.62f};
        draw.text({x, y, 0.f}, lines.front(), titleColor, scale);
        y -= lineStep;
        for (std::size_t i = 1; i < lines.size(); ++i) {
            draw.text({x, y, 0.f}, lines[i], bodyColor, scale);
            y -= lineStep;
        }
    }

}// namespace chrononmotion::preview
