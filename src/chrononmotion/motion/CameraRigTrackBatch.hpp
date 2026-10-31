// Internal mutation queue shared by camera authoring appliers.
#pragma once

#include "chrononmotion/motion/CameraRig.hpp"

#include <cstddef>
#include <stdexcept>
#include <variant>
#include <vector>

namespace chrononmotion::motion {

/// Shared typed writer for camera appliers. Timing and easing remain with
/// each applier; all mutation of rig tracks goes through this queue. Events
/// preserve authored order and Track::add last-write-wins semantics.
class CameraRigTrackBatch {
public:
    enum class Channel { Position, Target, Orientation, Fov, FocalLength, Roll,
                         FocusDistance, MaxBlur, FStop };
    class TrackProxy {
    public:
        TrackProxy(CameraRigTrackBatch& owner, Channel channel) : owner_(owner), channel_(channel) {}
        TrackProxy& add(float t, const Vector3& v, const Easing& e = Easing::linear()) {
            if (channel_ == Channel::Position) owner_.position(t, v, e);
            else if (channel_ == Channel::Target) owner_.target(t, v, e);
            else throw std::logic_error("CameraRigTrackBatch: vector value used for a scalar channel");
            return *this;
        }
        TrackProxy& add(float t, const Quaternion& v, const Easing& e = Easing::linear()) {
            if (channel_ != Channel::Orientation)
                throw std::logic_error("CameraRigTrackBatch: quaternion value used for a non-orientation channel");
            owner_.orientation(t, v, e);
            return *this;
        }
        TrackProxy& add(float t, float v, const Easing& e = Easing::linear()) {
            switch (channel_) {
                case Channel::Fov: owner_.fov(t, v, e); break;
                case Channel::FocalLength: owner_.focalLength(t, v, e); break;
                case Channel::Roll: owner_.roll(t, v, e); break;
                case Channel::FocusDistance: owner_.focusDistance(t, v, e); break;
                case Channel::MaxBlur: owner_.maxBlur(t, v, e); break;
                case Channel::FStop: owner_.fStop(t, v, e); break;
                default: throw std::logic_error("CameraRigTrackBatch: scalar value used for a vector channel");
            }
            return *this;
        }
    private:
        CameraRigTrackBatch& owner_;
        Channel channel_;
    };

    CameraRigTrackBatch() = default;
    explicit CameraRigTrackBatch(CameraRig& rig) : rig_(&rig) {}

    [[nodiscard]] TrackProxy positionTrack() { return {*this, Channel::Position}; }
    [[nodiscard]] TrackProxy targetTrack() { return {*this, Channel::Target}; }
    [[nodiscard]] TrackProxy orientationTrack() { return {*this, Channel::Orientation}; }
    [[nodiscard]] TrackProxy fovTrack() { return {*this, Channel::Fov}; }
    [[nodiscard]] TrackProxy focalLengthTrack() { return {*this, Channel::FocalLength}; }
    [[nodiscard]] TrackProxy rollTrack() { return {*this, Channel::Roll}; }
    [[nodiscard]] TrackProxy focusDistanceTrack() { return {*this, Channel::FocusDistance}; }
    [[nodiscard]] TrackProxy maxBlurTrack() { return {*this, Channel::MaxBlur}; }
    [[nodiscard]] TrackProxy fStopTrack() { return {*this, Channel::FStop}; }

    void position(float t, const Vector3& v, const Easing& e) { push(Channel::Position, t, v, e); }
    void target(float t, const Vector3& v, const Easing& e) { push(Channel::Target, t, v, e); }
    void orientation(float t, const Quaternion& v, const Easing& e) { push(Channel::Orientation, t, v, e); }
    void fov(float t, float v, const Easing& e) { push(Channel::Fov, t, v, e); }
    void focalLength(float t, float v, const Easing& e) { push(Channel::FocalLength, t, v, e); }
    void roll(float t, float v, const Easing& e) { push(Channel::Roll, t, v, e); }
    void focusDistance(float t, float v, const Easing& e) { push(Channel::FocusDistance, t, v, e); }
    void maxBlur(float t, float v, const Easing& e) { push(Channel::MaxBlur, t, v, e); }
    void fStop(float t, float v, const Easing& e) { push(Channel::FStop, t, v, e); }

    [[nodiscard]] bool empty() const noexcept { return events_.empty(); }
    [[nodiscard]] std::size_t size() const noexcept { return events_.size(); }

    void apply(CameraRig& rig) const {
        for (const auto& event : events_) {
            switch (event.channel) {
                case Channel::Position: rig.positionTrack().add(event.time, std::get<Vector3>(event.value), event.easing); break;
                case Channel::Target: rig.targetTrack().add(event.time, std::get<Vector3>(event.value), event.easing); break;
                case Channel::Orientation: rig.orientationTrack().add(event.time, std::get<Quaternion>(event.value), event.easing); break;
                case Channel::Fov: rig.fovTrack().add(event.time, std::get<float>(event.value), event.easing); break;
                case Channel::FocalLength: rig.focalLengthTrack().add(event.time, std::get<float>(event.value), event.easing); break;
                case Channel::Roll: rig.rollTrack().add(event.time, std::get<float>(event.value), event.easing); break;
                case Channel::FocusDistance: rig.focusDistanceTrack().add(event.time, std::get<float>(event.value), event.easing); break;
                case Channel::MaxBlur: rig.maxBlurTrack().add(event.time, std::get<float>(event.value), event.easing); break;
                case Channel::FStop: rig.fStopTrack().add(event.time, std::get<float>(event.value), event.easing); break;
            }
        }
    }
    void applyAndClear(CameraRig& rig) {
        apply(rig);
        events_.clear();
    }

private:
    using Value = std::variant<Vector3, Quaternion, float>;
    struct Event { Channel channel; float time; Value value; Easing easing; };
    void push(Channel c, float t, const Vector3& v, const Easing& e) { events_.push_back({c, t, v, e}); maybeFlush(); }
    void push(Channel c, float t, const Quaternion& v, const Easing& e) { events_.push_back({c, t, v, e}); maybeFlush(); }
    void push(Channel c, float t, float v, const Easing& e) { events_.push_back({c, t, v, e}); maybeFlush(); }
    void maybeFlush() { if (rig_ && events_.size() >= 4096) applyAndClear(*rig_); }
    CameraRig* rig_{nullptr};
    std::vector<Event> events_;
};

} // namespace chrononmotion::motion
