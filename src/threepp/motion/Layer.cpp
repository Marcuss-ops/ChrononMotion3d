#include "threepp/motion/Layer.hpp"

namespace threepp::motion {

    bool LayerTracks::empty() const {

        return position.empty() &&
               anchor.empty() &&
               scale.empty() &&
               rotation.empty() &&
               orientation.empty() &&
               opacity.empty();
    }

    float LayerTracks::startTime() const {

        float start = 0.f;
        bool first = true;

        const auto consider = [&](float time) {
            if (first || time < start) {
                start = time;
                first = false;
            }
        };

        if (!position.empty()) consider(position.startTime());
        if (!anchor.empty()) consider(anchor.startTime());
        if (!scale.empty()) consider(scale.startTime());
        if (!rotation.empty()) consider(rotation.startTime());
        if (!orientation.empty()) consider(orientation.startTime());
        if (!opacity.empty()) consider(opacity.startTime());

        return start;
    }

    float LayerTracks::endTime() const {

        float end = 0.f;

        end = std::max(end, position.endTime());
        end = std::max(end, anchor.endTime());
        end = std::max(end, scale.endTime());
        end = std::max(end, rotation.endTime());
        end = std::max(end, orientation.endTime());
        end = std::max(end, opacity.endTime());

        return end;
    }

    Layer::Layer(Id id, std::string name)
        : id(id), name(std::move(name)) {}

    void Layer::sample(float seconds, Transform& out) const {

        out = transform;

        tracks.position.sample(seconds, out.position);
        tracks.anchor.sample(seconds, out.anchor);
        tracks.scale.sample(seconds, out.scale);
        tracks.rotation.sample(seconds, out.rotation);
        tracks.orientation.sample(seconds, out.orientation);
        tracks.opacity.sample(seconds, out.opacity);
    }

    Transform Layer::sample(float seconds) const {

        Transform out;
        sample(seconds, out);
        return out;
    }

    float Layer::endTime() const {

        return tracks.endTime();
    }

    Layer& Layer::named(std::string value) {

        name = std::move(value);
        return *this;
    }

    Layer& Layer::childOf(Id parent) {

        parentId = parent;
        return *this;
    }

    Layer& Layer::positionedAt(const Vector3& value) {

        transform.position.copy(value);
        return *this;
    }

    Layer& Layer::anchoredAt(const Vector3& value) {

        transform.anchor.copy(value);
        return *this;
    }

    Layer& Layer::scaled(const Vector3& value) {

        transform.scale.copy(value);
        return *this;
    }

    Layer& Layer::scaled(float uniform) {

        transform.scale.set(uniform, uniform, uniform);
        return *this;
    }

    Layer& Layer::rotated(const Vector3& eulerRadians) {

        transform.rotation.copy(eulerRadians);
        return *this;
    }

    Layer& Layer::oriented(const Quaternion& value) {

        transform.orientation.copy(value);
        return *this;
    }

    Layer& Layer::opacityAt(float value) {

        transform.opacity = value;
        return *this;
    }

    Layer& Layer::alive(FrameRange range) {

        lifetime = range;
        return *this;
    }

    Layer& Layer::alive(int inFrame, int outFrame) {

        lifetime = FrameRange::between(inFrame, outFrame);
        return *this;
    }

    Layer& Layer::contentOf(ContentRef value) {

        content = std::move(value);
        return *this;
    }

    Layer& Layer::flat() {

        is3D = false;
        return *this;
    }

}// namespace threepp::motion
