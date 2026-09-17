// ChrononMotion — the content boundary.
//
// The rule this file exists to keep mechanical:
//
//   Chronon owns content. ChrononMotion owns space and time.
//
// ChrononMotion never sees bytes, fonts, pixels or decoded frames. It sees an
// opaque identity plus a neutral rectangle, which is all anchoring and placement
// need. Two ContentRefs are the same asset iff their ids match.

#ifndef CHRONONMOTION_MOTION_CONTENT_HPP
#define CHRONONMOTION_MOTION_CONTENT_HPP

#include "chrononmotion/math/Vector2.hpp"

#include <string>
#include <utility>

namespace chrononmotion::motion {

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
    ///
    /// `fingerprint` is the identity of the *measurement*, not of the asset: the
    /// digest of the exact bytes and style Chronon measured the rectangle from.
    /// The motion side never computes it and never interprets it — it carries it
    /// so that "these numbers were taken from that content" is a fact a caller
    /// can check instead of an assumption. Without it a re-measured asset keeps
    /// the size and pivot of the old one, silently.
    struct ContentMetrics {
        /// Opaque digest of the measured content. Empty means "not measured by
        /// Chronon" (hand-authored numbers), which is a legitimate state but one
        /// that can never be checked for staleness.
        std::string fingerprint{};

        /// Authored box of the content, in content units (Chronon's units; the
        /// motion side only compares them against each other).
        Vector2 naturalSize{0.f, 0.f};

        /// Normalized pivot inside the box: (0,0) bottom-left … (1,1) top-right.
        Vector2 anchor{0.5f, 0.5f};

        /// Whether these numbers came from a measurement at all.
        [[nodiscard]] bool measured() const { return !fingerprint.empty(); }

        /// Whether these numbers are the ones taken from `currentFingerprint`.
        /// A measurement with no fingerprint is never the current one: it cannot
        /// be shown to be current, and treating "unknown" as "fine" is precisely
        /// the silent staleness this field exists to prevent.
        [[nodiscard]] bool currentAgainst(const std::string& currentFingerprint) const {

            return measured() && fingerprint == currentFingerprint;
        }
    };

    /// The verdict on a ContentRef's measurement against a freshly computed
    /// digest.
    enum class MeasurementState {
        Unknown, ///< The ref carries no fingerprint; nothing can be concluded.
        Current, ///< The carried measurement was taken from that digest.
        Stale    ///< The carried measurement was taken from different content.
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

            return ContentRef{std::move(id), ContentKind::Text, ContentMetrics{std::string{}, naturalSize, anchor}};
        }

        [[nodiscard]] static ContentRef image(std::string id,
                                             Vector2 naturalSize,
                                             Vector2 anchor = Vector2(0.5f, 0.5f)) {

            return ContentRef{std::move(id), ContentKind::Image, ContentMetrics{std::string{}, naturalSize, anchor}};
        }

        [[nodiscard]] static ContentRef video(std::string id,
                                             Vector2 naturalSize,
                                             Vector2 anchor = Vector2(0.5f, 0.5f)) {

            return ContentRef{std::move(id), ContentKind::Video, ContentMetrics{std::string{}, naturalSize, anchor}};
        }

        /// A handle whose rectangle was measured from content with this digest.
        /// This is the form a Chronon-produced ref takes; the un-fingerprinted
        /// factories above stay for hand-authored geometry and for the tests.
        [[nodiscard]] static ContentRef measured(std::string id,
                                                ContentKind kind,
                                                std::string fingerprint,
                                                Vector2 naturalSize,
                                                Vector2 anchor = Vector2(0.5f, 0.5f)) {

            return ContentRef{std::move(id), kind, ContentMetrics{std::move(fingerprint), naturalSize, anchor}};
        }

        /// Attach (or replace) the measurement fingerprint, keeping id, kind and
        /// rectangle. Fluent, so a caller can state the digest next to the ref it
        /// measured.
        ContentRef& withFingerprint(std::string value) {

            metrics.fingerprint = std::move(value);
            return *this;
        }

        /// Compare the carried measurement against a fresh digest for the same
        /// asset. `Unknown` when this ref carries no measurement to compare.
        [[nodiscard]] MeasurementState measurementState(const std::string& currentFingerprint) const {

            if (!metrics.measured()) return MeasurementState::Unknown;
            return metrics.currentAgainst(currentFingerprint) ? MeasurementState::Current : MeasurementState::Stale;
        }

        [[nodiscard]] bool isText() const { return kind == ContentKind::Text; }
        [[nodiscard]] bool isImage() const { return kind == ContentKind::Image; }
        [[nodiscard]] bool isVideo() const { return kind == ContentKind::Video; }

        /// Identity only: `metrics` is a cached measurement of the same asset, not
        /// part of what makes it the same asset — and neither is its fingerprint,
        /// which changes when the same asset is re-measured.
        bool operator==(const ContentRef& other) const { return id == other.id && kind == other.kind; }
        bool operator!=(const ContentRef& other) const { return !(*this == other); }
    };

}// namespace chrononmotion::motion

#endif//CHRONONMOTION_MOTION_CONTENT_HPP
