#include "RenderPlanEmissionInternal.hpp"

#include <chronon3d/render_plan/color_utils.hpp>

#include <cmath>
#include <optional>
#include <string>

namespace chrononmotion::motion::detail {
using chronon3d::render_plan::FitMode;
using chronon3d::render_plan::LayerType;
using chronon3d::render_plan::OutputFormat;
using chronon3d::render_plan::ShapeContentTypePlan;

const char* pathCommandTypeName(chronon3d::render_plan::ShapePathCommandTypePlan type) {
    using Type = chronon3d::render_plan::ShapePathCommandTypePlan;
    switch (type) {
        case Type::MoveTo: return "move_to";
        case Type::LineTo: return "line_to";
        case Type::QuadraticTo: return "quadratic_to";
        case Type::CubicTo: return "cubic_to";
        case Type::Close: return "close";
    }
    return "unsupported";
}

const char* fieldGeneratorName(chronon3d::graphics::Field2DGenerator generator) {
    using Generator = chronon3d::graphics::Field2DGenerator;
    switch (generator) {
        case Generator::Perlin: return "perlin";
        case Generator::Simplex: return "simplex";
        case Generator::Fractal: return "fractal";
        case Generator::Cellular: return "cellular";
        case Generator::Grid: return "grid";
        case Generator::Checker: return "checker";
        case Generator::Dots: return "dots";
        case Generator::Stripes: return "stripes";
        case Generator::Rings: return "rings";
        case Generator::Linear: return "linear";
        case Generator::Radial: return "radial";
        case Generator::Conic: return "conic";
        case Generator::Burst: return "burst";
        case Generator::Value:
        case Generator::Ridged: return "unsupported";
    }
    return "unsupported";
}
        [[nodiscard]] const char* layerTypeName(LayerType type) {

            switch (type) {
                case LayerType::Image: return "image";
                case LayerType::Video: return "video";
                case LayerType::Text: return "text";
                case LayerType::Color: return "color";
                case LayerType::Shape: return "shape";
                case LayerType::Particles: return "particles";
                case LayerType::Light: return "light";
                case LayerType::Adjustment: return "adjustment";
                case LayerType::Mesh: return "mesh";
            }
            return "unknown";
        }

        [[nodiscard]] const char* shapeTypeName(ShapeContentTypePlan type) {
            switch (type) {
                case ShapeContentTypePlan::Rect: return "rect";
                case ShapeContentTypePlan::RoundedRect: return "rounded_rect";
                case ShapeContentTypePlan::Ellipse: return "ellipse";
                case ShapeContentTypePlan::Line: return "line";
                case ShapeContentTypePlan::Path: return "path";
                case ShapeContentTypePlan::Star: return "star";
                case ShapeContentTypePlan::Polygon: return "polygon";
                case ShapeContentTypePlan::Arrow: return "arrow";
                case ShapeContentTypePlan::Arc: return "arc";
                case ShapeContentTypePlan::Grid: return "grid";
                case ShapeContentTypePlan::DotGrid: return "dot_grid";
                case ShapeContentTypePlan::DeviceFrame: return "device_frame";
                case ShapeContentTypePlan::RingSegment: return "ring_segment";
                case ShapeContentTypePlan::MonotoneLine: return "monotone_line";
                case ShapeContentTypePlan::MonotoneChart: return "monotone_chart";
            }
            return "unknown";
        }

        [[nodiscard]] const char* blendModeName(chronon3d::BlendMode mode) {

            switch (mode) {
                case chronon3d::BlendMode::Normal: return "normal";
                case chronon3d::BlendMode::Add: return "add";
                case chronon3d::BlendMode::Multiply: return "multiply";
                case chronon3d::BlendMode::Screen: return "screen";
                case chronon3d::BlendMode::Overlay: return "overlay";
                case chronon3d::BlendMode::Darken: return "darken";
                case chronon3d::BlendMode::Lighten: return "lighten";
                case chronon3d::BlendMode::Difference: return "difference";
                case chronon3d::BlendMode::Exclusion: return "exclusion";
                case chronon3d::BlendMode::SoftLight: return "soft_light";
                case chronon3d::BlendMode::HardLight: return "hard_light";
                case chronon3d::BlendMode::ColorDodge: return "color_dodge";
                case chronon3d::BlendMode::ColorBurn: return "color_burn";
                case chronon3d::BlendMode::Subtract: return "subtract";
                case chronon3d::BlendMode::Divide: return "divide";
                case chronon3d::BlendMode::VividLight: return "vivid_light";
                case chronon3d::BlendMode::PinLight: return "pin_light";
                case chronon3d::BlendMode::LinearLight: return "linear_light";
                case chronon3d::BlendMode::Hue: return "hue";
                case chronon3d::BlendMode::Saturation: return "saturation";
                case chronon3d::BlendMode::Color: return "color";
                case chronon3d::BlendMode::Luminosity: return "luminosity";
            }
            return "normal";
        }

        [[nodiscard]] const char* fitName(FitMode fit) {

            switch (fit) {
                case FitMode::Cover: return "cover";
                case FitMode::Contain: return "contain";
                case FitMode::Stretch: return "stretch";
                case FitMode::None: return "none";
            }
            return "none";
        }

        [[nodiscard]] const char* outputFormatName(OutputFormat format) {

            switch (format) {
                case OutputFormat::Png: return "png";
                case OutputFormat::PngSequence: return "png_sequence";
                case OutputFormat::Gif: return "gif";
                case OutputFormat::Mp4: return "mp4";
                case OutputFormat::Mkv: return "mkv";
                case OutputFormat::WebM: return "webm";
            }
            return "png";
        }


[[nodiscard]] std::optional<std::string> textSpanColorHex(const chronon3d::Color& color) {
            if (!std::isfinite(color.r) || !std::isfinite(color.g) || !std::isfinite(color.b) ||
                !std::isfinite(color.a) || color.r < 0.f || color.r > 1.f ||
                color.g < 0.f || color.g > 1.f || color.b < 0.f || color.b > 1.f ||
                color.a < 0.f || color.a > 1.f)
                return std::nullopt;
            const auto quantize = [](float linear) {
                return static_cast<unsigned int>(std::lround(
                    chronon3d::render_plan::linear_to_srgb(linear) * 255.f));
            };
            const unsigned int channels[] = {quantize(color.r), quantize(color.g),
                quantize(color.b), static_cast<unsigned int>(std::lround(color.a * 255.f))};
            constexpr char digits[] = "0123456789ABCDEF";
            std::string result{"#"};
            for (const unsigned int channel : channels) {
                result.push_back(digits[(channel >> 4U) & 0x0fU]);
                result.push_back(digits[channel & 0x0fU]);
            }
            const auto parseChannel = [](char high, char low) {
                const auto nibble = [](char value) -> unsigned int {
                    if (value >= '0' && value <= '9') return static_cast<unsigned int>(value - '0');
                    if (value >= 'A' && value <= 'F') return static_cast<unsigned int>(value - 'A' + 10);
                    return static_cast<unsigned int>(value - 'a' + 10);
                };
                return static_cast<float>((nibble(high) * 16U + nibble(low)) / 255.0);
            };
            const float decoded[] = {
                chronon3d::render_plan::srgb_to_linear(parseChannel(result[1], result[2])),
                chronon3d::render_plan::srgb_to_linear(parseChannel(result[3], result[4])),
                chronon3d::render_plan::srgb_to_linear(parseChannel(result[5], result[6])),
                parseChannel(result[7], result[8])};
            if (std::abs(decoded[0] - color.r) > 1.0e-6f ||
                std::abs(decoded[1] - color.g) > 1.0e-6f ||
                std::abs(decoded[2] - color.b) > 1.0e-6f ||
                std::abs(decoded[3] - color.a) > 1.0e-6f)
                return std::nullopt;
            return result;
        }
}
