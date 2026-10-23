#include "chrononmotion/math/Color.hpp"

#include <algorithm>
#include <iostream>
#include <regex>
#include <unordered_map>

using namespace chrononmotion;

namespace {

    // ChrononMotion: upstream parsed the numeric captures of a CSS colour with
    // threepp's own `utils::parseInt`/`parseFloat` (a helper header this fork
    // does not carry). These read the same captures and answer 0 for anything
    // unparseable, which is what the upstream helpers did.
    int parseStyleInt(const std::string& text) {

        try {
            return std::stoi(text);
        } catch (...) {
            return 0;
        }
    }

    float parseStyleFloat(const std::string& text) {

        try {
            return std::stof(text);
        } catch (...) {
            return 0.f;
        }
    }

    //clang-format off
    std::unordered_map<std::string, int> colorKeywords{
            {"aliceblue", Color::aliceblue},
            {"antiquewhite", Color::antiquewhite},
            {"aqua", Color::aqua},
            {"aquamarine", Color::aquamarine},
            {"azure", Color::azure},

            {"beige", Color::beige},
            {"bisque", Color::bisque},
            {"black", Color::black},
            {"blanchedalmond", Color::blanchedalmond},
            {"blue", Color::blue},
            {"blueviolet", Color::blueviolet},

            {"brown", Color::brown},
            {"burlywood", Color::burlywood},
            {"cadetblue", Color::cadetblue},
            {"chartreuse", Color::chartreuse},
            {"chocolate", Color::chocolate},
            {"coral", Color::coral},

            {"cornflowerblue", Color::cornflowerblue},
            {"cornsilk", Color::cornsilk},
            {"crimson", Color::crimson},
            {"cyan", Color::cyan},
            {"darkblue", Color::darkblue},
            {"darkcyan", Color::darkcyan},

            {"darkgoldenrod", Color::darkgoldenrod},
            {"darkgray", Color::darkgray},
            {"darkgreen", Color::darkgreen},
            {"darkgrey", Color::darkgrey},
            {"darkkhaki", Color::darkkhaki},
            {"darkmagenta", Color::darkmagenta},

            {"darkolivegreen", Color::darkolivegreen},
            {"darkorange", Color::darkorange},
            {"darkorchid", Color::darkorchid},
            {"darkred", Color::darkred},
            {"darksalmon", Color::darksalmon},
            {"darkseagreen", Color::darkseagreen},

            {"darkslateblue", Color::darkslateblue},
            {"darkslategray", Color::darkslategray},
            {"darkslategrey", Color::darkslategrey},
            {"darkturquoise", Color::darkturquoise},
            {"darkviolet", Color::darkviolet},

            {"deeppink", Color::deeppink},
            {"deepskyblue", Color::deepskyblue},
            {"dimgray", Color::dimgray},
            {"dimgrey", Color::dimgrey},
            {"dodgerblue", Color::dodgerblue},
            {"firebrick", Color::firebrick},

            {"floralwhite", Color::floralwhite},
            {"forestgreen", Color::forestgreen},
            {"fuchsia", Color::fuchsia},
            {"gainsboro", Color::gainsboro},
            {"ghostwhite", Color::ghostwhite},
            {"gold", Color::gold},

            {"goldenrod", Color::goldenrod},
            {"gray", Color::gray},
            {"green", Color::green},
            {"greenyellow", Color::greenyellow},
            {"grey", Color::grey},
            {"honeydew", Color::honeydew},
            {"hotpink", Color::hotpink},

            {"indianred", Color::indianred},
            {"indigo", Color::indigo},
            {"ivory", Color::ivory},
            {"khaki", Color::khaki},
            {"lavender", Color::lavender},
            {"lavenderblush", Color::lavenderblush},
            {"lawngreen", Color::lawngreen},

            {"lemonchiffon", Color::lemonchiffon},
            {"lightblue", Color::lightblue},
            {"lightcoral", Color::lightcoral},
            {"lightcyan", Color::lightcyan},
            {"lightgoldenrodyellow", Color::lightgoldenrodyellow},
            {"lightgray", Color::lightgray},

            {"lightgreen", Color::lightgreen},
            {"lightgrey", Color::lightgrey},
            {"lightpink", Color::lightpink},
            {"lightsalmon", Color::lightsalmon},
            {"lightseagreen", Color::lightseagreen},
            {"lightskyblue", Color::lightskyblue},

            {"lightslategray", Color::lightslategray},
            {"lightslategrey", Color::lightslategrey},
            {"lightsteelblue", Color::lightsteelblue},
            {"lightyellow", Color::lightyellow},
            {"lime", Color::lime},
            {"limegreen", Color::limegreen},

            {"linen", Color::linen},
            {"magenta", Color::magenta},
            {"maroon", Color::maroon},
            {"mediumaquamarine", Color::mediumaquamarine},
            {"mediumblue", Color::mediumblue},
            {"mediumorchid", Color::mediumorchid},

            {"mediumpurple", Color::mediumpurple},
            {"mediumseagreen", Color::mediumseagreen},
            {"mediumslateblue", Color::mediumslateblue},
            {"mediumspringgreen", Color::mediumspringgreen},
            {"mediumturquoise", Color::mediumturquoise},

            {"mediumvioletred", Color::mediumvioletred},
            {"midnightblue", Color::midnightblue},
            {"mintcream", Color::mintcream},
            {"mistyrose", Color::mistyrose},
            {"moccasin", Color::moccasin},
            {"navajowhite", Color::navajowhite},

            {"navy", Color::navy},
            {"oldlace", Color::oldlace},
            {"olive", Color::olive},
            {"olivedrab", Color::olivedrab},
            {"orange", Color::orange},
            {"orangered", Color::orangered},
            {"orchid", Color::orchid},

            {"palegoldenrod", Color::palegoldenrod},
            {"palegreen", Color::palegreen},
            {"paleturquoise", Color::paleturquoise},
            {"palevioletred", Color::palevioletred},
            {"papayawhip", Color::papayawhip},
            {"peachpuff", Color::peachpuff},

            {"peru", Color::peru},
            {"pink", Color::pink},
            {"plum", Color::plum},
            {"powderblue", Color::powderblue},
            {"purple", Color::purple},
            {"rebeccapurple", Color::rebeccapurple},
            {"red", Color::red},
            {"rosybrown", Color::rosybrown},

            {"royalblue", Color::royalblue},
            {"saddlebrown", Color::saddlebrown},
            {"salmon", Color::salmon},
            {"sandybrown", Color::sandybrown},
            {"seagreen", Color::seagreen},
            {"seashell", Color::seashell},

            {"sienna", Color::sienna},
            {"silver", Color::silver},
            {"skyblue", Color::skyblue},
            {"slateblue", Color::slateblue},
            {"slategray", Color::slategray},
            {"slategrey", Color::slategrey},
            {"snow", Color::snow},

            {"springgreen", Color::springgreen},
            {"steelblue", Color::steelblue},
            {"tan", Color::tan},
            {"teal", Color::teal},
            {"thistle", Color::thistle},
            {"tomato", Color::tomato},
            {"turquoise", Color::turquoise},

            {"violet", Color::violet},
            {"wheat", Color::wheat},
            {"white", Color::white},
            {"whitesmoke", Color::whitesmoke},
            {"yellow", Color::yellow},
            {"yellowgreen", Color::yellowgreen}

    };
    //clang-format on

} // namespace

Color& Color::setStyle(const std::string& style, ColorSpace colorSpace) {

    // Each terminal setter (setRGB/setHSL/setHex/setColorName) applies the
    // colorSpace -> working conversion, so the parsing here just routes to them.

    static std::regex r1(R"(((?:rgb|hsl)a?)\(([^\)]*)\))", std::regex::icase);
    static std::regex r2("\\#([A-Fa-f\\d]+)$", std::regex::icase);

    std::smatch match;
    if (std::regex_match(style, match, r1)) {

        // rgb / hsl

        std::string name = match[1];
        std::string components = match[2];

        if (name == "rgb" || name == "rgba") {

            static std::regex ra(R"(\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*(?:,\s*(\d*\.?\d+)\s*)?$)", std::regex::icase);
            static std::regex rb(R"(\s*(\d+)\%\s*,\s*(\d+)\%\s*,\s*(\d+)\%\s*(?:,\s*(\d*\.?\d+)\s*)?$)", std::regex::icase);

            if (std::regex_match(components, match, ra)) {

                // rgb(255,0,0) rgba(255,0,0,0.5)
                return this->setRGB(
                        std::min(255.f, static_cast<float>(parseStyleInt(match[1].str()))) / 255,
                        std::min(255.f, static_cast<float>(parseStyleInt(match[2].str()))) / 255,
                        std::min(255.f, static_cast<float>(parseStyleInt(match[3].str()))) / 255,
                        colorSpace);

            } else if (std::regex_match(components, match, rb)) {

                // rgb(100%,0%,0%) rgba(100%,0%,0%,0.5)
                return this->setRGB(
                        std::min(100.f, static_cast<float>(parseStyleInt(match[1].str()))) / 100,
                        std::min(100.f, static_cast<float>(parseStyleInt(match[2].str()))) / 100,
                        std::min(100.f, static_cast<float>(parseStyleInt(match[3].str()))) / 100,
                        colorSpace);
            }

        } else if (name == "hsl" || name == "hsla") {

            static std::regex r(R"(\s*(\d*\.?\d+)\s*,\s*(\d+)\%\s*,\s*(\d+)\%\s*(?:,\s*(\d*\.?\d+)\s*)?$)", std::regex::icase);

            if (std::regex_match(components, match, r)) {

                // hsl(120,50%,50%) hsla(120,50%,50%,0.5)
                const auto h = parseStyleFloat(match[1].str()) / 360;
                const auto s = static_cast<float>(parseStyleInt(match[2].str())) / 100;
                const auto l = static_cast<float>(parseStyleInt(match[3].str())) / 100;

                return this->setHSL(h, s, l, colorSpace);
            }
        }

    } else if (std::regex_match(style, match, r2)) {

        // hex color

        std::string hex = match[1];
        auto size = hex.length();

        if (size == 3) {

            // #ff0
            return this->setRGB(
                    static_cast<float>(std::stoi(std::string{hex[0]} + std::string{hex[0]}, nullptr, 16)) / 255,
                    static_cast<float>(std::stoi(std::string{hex[1]} + std::string{hex[1]}, nullptr, 16)) / 255,
                    static_cast<float>(std::stoi(std::string{hex[2]} + std::string{hex[2]}, nullptr, 16)) / 255,
                    colorSpace);

        } else if (size == 6) {

            // #ff0000
            return this->setHex(static_cast<unsigned int>(std::stoul(hex, nullptr, 16)), colorSpace);
        }
    }

    if (style.length() > 0) {

        return setColorName(style, colorSpace);
    }

    return *this;
}

Color& Color::setColorName(const std::string& style, ColorSpace colorSpace) {

    if (!colorKeywords.contains(style)) {
        std::cerr << "THREE.Color: Unknown color '" + style + "'" << std::endl;
        return *this;
    }

    return setHex(colorKeywords.at(style), colorSpace);
}
