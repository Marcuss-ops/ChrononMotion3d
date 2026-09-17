// ChrononMotion — the content boundary.
//
// The rule this file exists to keep mechanical:
//
//   Chronon owns content. ChrononMotion owns space and time.
//
// ChrononMotion never sees bytes, fonts, pixels or decoded frames. It sees an
// opaque identity plus a neutral rectangle, which is all anchoring and placement
// need. Two ContentRefs are the same asset iff their ids match.

#ifndef THREEPP_MOTION_CONTENT_HPP
#define THREEPP_MOTION_CONTENT_HPP

#include "threepp/math/Vector2.hpp"

#include <string>
#include <utility>

namespace threepp::motion {

    /// What a ContentRef stands for. Kept only so text and image semantics stay
    /// visibly shared: both go through the same anchor/transform path.
    enum class ContentKind {
        None,
        Text,
        Image,
        Video
    };

    /// The neutral, content-free measurements the motion side needs to anchor a
    /// layer: the authored box and the pivot inside it. No font, no pixels, no
    /// decoded frames — a fingerprint plus a rectangle.
    struct ContentMetrics {
        /// Authored box of the content, in content units (Chronon's units; the
        /// motion side only compares them against each other).
        Vector2 naturalSize{0.f, 0.f};

        /// Normalized pivot inside the box: (0,0) bottom-left … (1,1) top-right.
        Vector2 anchor{0.5f, 0.5f};
    };

    /// An opaque handle to content owned by Chronon.
    struct ContentRef {
        std::string id{};
        ContentKind kind{ContentKind::None};
        ContentMetrics metrics{};

        [[nodiscard]] bool empty() const { return id.empty() || kind == ContentKind::None; }

        [[nodiscard]] static ContentRef none() { return ContentRef{}; }

        [[nodiscard]] static ContentRef text(std::string id,
                                            Vector2 naturalSize,
                                            Vector2 anchor = Vector2(0.5f, 0.5f)) {

            return ContentRef{std::move(id), ContentKind::Text, ContentMetrics{naturalSize, anchor}};
        }

        [[nodiscard]] static ContentRef image(std::string id,
                                             Vector2 naturalSize,
                                             Vector2 anchor = Vector2(0.5f, 0.5f)) {

            return ContentRef{std::move(id), ContentKind::Image, ContentMetrics{naturalSize, anchor}};
        }

        [[nodiscard]] static ContentRef video(std::string id,
                                             Vector2 naturalSize,
                                             Vector2 anchor = Vector2(0.5f, 0.5f)) {

            return ContentRef{std::move(id), ContentKind::Video, ContentMetrics{naturalSize, anchor}};
        }

        [[nodiscard]] bool isText() const { return kind == ContentKind::Text; }
        [[nodiscard]] bool isImage() const { return kind == ContentKind::Image; }
        [[nodiscard]] bool isVideo() const { return kind == ContentKind::Video; }

        /// Identity only: `metrics` is a cached measurement of the same asset, not
        /// part of what makes it the same asset.
        bool operator==(const ContentRef& other) const { return id == other.id && kind == other.kind; }
        bool operator!=(const ContentRef& other) const { return !(*this == other); }
    };

}// namespace threepp::motion

#endif//THREEPP_MOTION_CONTENT_HPP
