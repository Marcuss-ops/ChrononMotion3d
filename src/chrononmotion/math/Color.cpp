
#include "chrononmotion/math/Color.hpp"

#include "chrononmotion/math/MathUtils.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

using namespace chrononmotion;

namespace {

    // sRGB <-> linear transfer functions. Must match the GLSL sRGBToLinear /
    // LinearTosRGB in ShaderChunk/encodings_pars_fragment.glsl so a color
    // linearized on the CPU round-trips exactly through the renderer's output encode.
    float SRGBToLinear(float c) {
        return (c < 0.04045f) ? c * 0.0773993808f : std::pow(c * 0.9478672986f + 0.0521327014f, 2.4f);
    }

    float LinearToSRGB(float c) {
        return (c < 0.0031308f) ? c * 12.92f : 1.055f * std::pow(c, 0.41666f) - 0.055f;
    }

    float hue2rgb(float p, float q, float t) {

        if (t < 0) t += 1;
        if (t > 1) t -= 1;
        if (t < 1.f / 6) return p + (q - p) * 6 * t;
        if (t < 1.f / 2) return q;
        if (t < 2.f / 3) return p + (q - p) * 6 * (2.f / 3 - t);
        return p;
    }



}// namespace

bool ColorManagement::enabled = true;
ColorSpace ColorManagement::workingColorSpace = ColorSpace::Linear;

Color& ColorManagement::convert(Color& color, ColorSpace source, ColorSpace target) {

    if (!enabled || source == target ||
        source == ColorSpace::NoColorSpace || target == ColorSpace::NoColorSpace) {

        return color;
    }

    // threepp only deals with the sRGB and linear-sRGB working spaces, which share
    // the same (Rec. 709) primaries, so only the transfer function differs.
    if (source == ColorSpace::sRGB) {

        color.r = SRGBToLinear(color.r);
        color.g = SRGBToLinear(color.g);
        color.b = SRGBToLinear(color.b);
    }

    if (target == ColorSpace::sRGB) {

        color.r = LinearToSRGB(color.r);
        color.g = LinearToSRGB(color.g);
        color.b = LinearToSRGB(color.b);
    }

    return color;
}

Color& ColorManagement::colorSpaceToWorking(Color& color, ColorSpace source) {

    return convert(color, source, workingColorSpace);
}

Color& ColorManagement::workingToColorSpace(Color& color, ColorSpace target) {

    return convert(color, workingColorSpace, target);
}

Color::Color(float r, float g, float b)
    : r(r), g(g), b(b) {}

Color::Color(unsigned int hex) {
    setHex(hex);
}

float& Color::operator[](unsigned int index) {
    switch (index) {
        case 0:
            return r;
        case 1:
            return g;
        case 2:
            return b;
        default:
            throw std::runtime_error("index out of bound: " + std::to_string(index));
    }
}

Color& Color::setScalar(float scalar) {

    this->r = scalar;
    this->g = scalar;
    this->b = scalar;

    return *this;
}

Color& Color::setHex(unsigned int hex, ColorSpace colorSpace) {

    this->r = static_cast<float>(hex >> 16 & 255) / 255.f;
    this->g = static_cast<float>(hex >> 8 & 255) / 255.f;
    this->b = static_cast<float>(hex & 255) / 255.f;

    ColorManagement::colorSpaceToWorking(*this, colorSpace);

    return *this;
}

Color& Color::setRGB(float r, float g, float b, ColorSpace colorSpace) {

    this->r = r;
    this->g = g;
    this->b = b;

    ColorManagement::colorSpaceToWorking(*this, colorSpace);

    return *this;
}

Color& Color::setHSL(float h, float s, float l, ColorSpace colorSpace) {

    // h,s,l ranges are in 0.0 - 1.0
    h = math::euclideanModulo(h, 1.f);
    s = std::clamp(s, 0.f, 1.f);
    l = std::clamp(l, 0.f, 1.f);

    if (s == 0) {

        this->r = this->g = this->b = l;

    } else {

        const auto p = l <= 0.5f ? l * (1 + s) : l + s - (l * s);
        const auto q = (2 * l) - p;

        this->r = hue2rgb(q, p, h + 1.f / 3);
        this->g = hue2rgb(q, p, h);
        this->b = hue2rgb(q, p, h - 1.f / 3);
    }

    ColorManagement::colorSpaceToWorking(*this, colorSpace);

    return *this;
}

Color& Color::copy(const Color& color) {

    this->r = color.r;
    this->g = color.g;
    this->b = color.b;

    return *this;
}

Color& Color::add(const Color& color) {

    this->r += color.r;
    this->g += color.g;
    this->b += color.b;

    return *this;
}

Color& Color::addColors(const Color& color1, const Color& color2) {

    this->r = color1.r + color2.r;
    this->g = color1.g + color2.g;
    this->b = color1.b + color2.b;

    return *this;
}

Color& Color::addScalar(float s) {

    this->r += s;
    this->g += s;
    this->b += s;

    return *this;
}

Color& Color::sub(const Color& color) {

    this->r = std::max(0.f, this->r - color.r);
    this->g = std::max(0.f, this->g - color.g);
    this->b = std::max(0.f, this->b - color.b);

    return *this;
}

Color& Color::multiply(const Color& color) {

    this->r *= color.r;
    this->g *= color.g;
    this->b *= color.b;

    return *this;
}

Color& Color::operator*=(const Color& color) {

    return multiply(color);
}

Color& Color::multiplyScalar(float s) {

    this->r *= s;
    this->g *= s;
    this->b *= s;

    return *this;
}

Color& Color::operator*=(float s) {

    return multiplyScalar(s);
}

Color& Color::lerp(const Color& color, float alpha) {

    this->r += (color.r - this->r) * alpha;
    this->g += (color.g - this->g) * alpha;
    this->b += (color.b - this->b) * alpha;

    return *this;
}

Color& Color::lerpColors(const Color& color1, const Color& color2, float alpha) {

    this->r = color1.r + (color2.r - color1.r) * alpha;
    this->g = color1.g + (color2.g - color1.g) * alpha;
    this->b = color1.b + (color2.b - color1.b) * alpha;

    return *this;
}

Color& Color::randomize() {

    this->r = math::randFloat();
    this->g = math::randFloat();
    this->b = math::randFloat();

    return *this;
}

bool Color::equals(const Color& c) const {

    return (c.r == this->r) && (c.g == this->g) && (c.b == this->b);
}

bool Color::operator==(const Color& c) const {

    return equals(c);
}

bool Color::operator!=(const Color& c) const {

    return !equals(c);
}

unsigned int Color::getHex(ColorSpace colorSpace) const {

    Color c;
    c.copy(*this);
    ColorManagement::workingToColorSpace(c, colorSpace);

    const auto r = static_cast<unsigned int>(std::round(std::clamp(c.r * 255.f, 0.f, 255.f)));
    const auto g = static_cast<unsigned int>(std::round(std::clamp(c.g * 255.f, 0.f, 255.f)));
    const auto b = static_cast<unsigned int>(std::round(std::clamp(c.b * 255.f, 0.f, 255.f)));

    return (r << 16) | (g << 8) | b;
}

HSL& Color::getHSL(HSL& target, ColorSpace colorSpace) const {
    // h,s,l ranges are in 0.0 - 1.0

    Color c;
    c.copy(*this);
    ColorManagement::workingToColorSpace(c, colorSpace);

    const float r = c.r, g = c.g, b = c.b;

    const auto max = std::max(r, std::max(g, b));
    const auto min = std::min(r, std::min(g, b));

    float hue = 0, saturation;
    const auto lightness = (min + max) / 2.0f;

    if (min == max) {

        hue = 0;
        saturation = 0;

    } else {

        const auto delta = max - min;

        saturation = lightness <= 0.5f ? delta / (max + min) : delta / (2 - max - min);

        if (max == r) {
            hue = (g - b) / delta + (g < b ? 6.f : 0.f);
        } else if (max == g) {
            hue = (b - r) / delta + 2;
        } else if (max == b) {
            hue = (r - g) / delta + 4;
        }

        hue /= 6.f;
    }

    target.h = hue;
    target.s = saturation;
    target.l = lightness;

    return target;
}

std::string Color::getHexString(ColorSpace colorSpace) const {

    std::stringstream ss;
    ss << std::setfill('0') << std::setw(6) << std::hex << getHex(colorSpace);

    return ss.str();
}

std::string Color::getStyle(ColorSpace colorSpace) const {

    Color c;
    c.copy(*this);
    ColorManagement::workingToColorSpace(c, colorSpace);

    std::stringstream ss;
    ss << "rgb(" << static_cast<int>(std::round(c.r * 255)) << ","
       << static_cast<int>(std::round(c.g * 255)) << ","
       << static_cast<int>(std::round(c.b * 255)) << ")";

    return ss.str();
}

Color& Color::copySRGBToLinear(const Color& color) {

    this->r = SRGBToLinear(color.r);
    this->g = SRGBToLinear(color.g);
    this->b = SRGBToLinear(color.b);

    return *this;
}

Color& Color::copyLinearToSRGB(const Color& color) {

    this->r = LinearToSRGB(color.r);
    this->g = LinearToSRGB(color.g);
    this->b = LinearToSRGB(color.b);

    return *this;
}

Color& Color::convertSRGBToLinear() {

    return copySRGBToLinear(*this);
}

Color& Color::convertLinearToSRGB() {

    return copyLinearToSRGB(*this);
}

Color& Color::lerpHSL(const Color& color, float alpha) {

    static HSL _hslA;
    static HSL _hslB;

    this->getHSL(_hslA);
    color.getHSL(_hslB);

    const auto h = math::lerp(_hslA.h, _hslB.h, alpha);
    const auto s = math::lerp(_hslA.s, _hslB.s, alpha);
    const auto l = math::lerp(_hslA.l, _hslB.l, alpha);

    this->setHSL(h, s, l);

    return *this;
}
