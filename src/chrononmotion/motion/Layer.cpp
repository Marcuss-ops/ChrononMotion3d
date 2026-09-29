#include "chrononmotion/motion/Layer.hpp"

#include <cmath>

namespace chrononmotion::motion {

    float TimeRemap::mapFrame(float frame) const {
        if (!valid()) throw std::invalid_argument("TimeRemap: invalid stretch or loop range");
        if (freeze) return offsetFrames;
        float local = offsetFrames + (reverse ? -frame : frame) / stretch;
        if (loopEnd < loopStart) return local;
        const float length = static_cast<float>(loopEnd - loopStart);
        if (length <= 0.f) return static_cast<float>(loopStart);
        float phase = std::fmod(local - static_cast<float>(loopStart), length);
        if (phase < 0.f) phase += length;
        if (pingPong) {
            const float doubled = length * 2.f;
            const float pingPhase = std::fmod(local - static_cast<float>(loopStart), doubled);
            const float normalized = pingPhase < 0.f ? pingPhase + doubled : pingPhase;
            return static_cast<float>(loopStart) + (normalized <= length ? normalized : doubled - normalized);
        }
        return static_cast<float>(loopStart) + phase;
    }

    bool TimeRemap::valid() const {
        return std::isfinite(offsetFrames) && std::isfinite(stretch) && stretch > 0.f &&
               (loopEnd < loopStart || loopStart >= 0);
    }

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
        : Object3D(), id(id) {
        this->name = std::move(name);
    }

    Layer::Layer(const Layer& source)
        : Object3D(),
          id(source.id),
          parentId(source.parentId),
          content(source.content),
          mesh(source.mesh),
          precomp(source.precomp),
          lifetime(source.lifetime),
          timeRemap(source.timeRemap),
          timeRemapTrack(source.timeRemapTrack),
          is3D(source.is3D),
          castsShadow(source.castsShadow),
          acceptsShadow(source.acceptsShadow),
          shadowOpacity(source.shadowOpacity),
          shadowBlur(source.shadowBlur),
          transform(source.transform),
          tracks(source.tracks),
          constraints(source.constraints),
          drivers(source.drivers) {

        this->name = source.name;
    }

    Layer& Layer::replaceAuthoring(Layer&& source) {
        if (this == &source) return *this;
        Object3D::name = std::move(source.Object3D::name);
        id = source.id;
        parentId = source.parentId;
        content = std::move(source.content);
        mesh = std::move(source.mesh);
        precomp = std::move(source.precomp);
        lifetime = source.lifetime;
        timeRemap = source.timeRemap;
        timeRemapTrack = std::move(source.timeRemapTrack);
        is3D = source.is3D;
        castsShadow = source.castsShadow;
        acceptsShadow = source.acceptsShadow;
        shadowOpacity = source.shadowOpacity;
        shadowBlur = source.shadowBlur;
        transform = source.transform;
        tracks = std::move(source.tracks);
        constraints = std::move(source.constraints);
        drivers = std::move(source.drivers);
        return *this;
    }

    float Layer::mapFrame(float sceneFrame, float sceneSeconds, const LinkResolver& resolve) const {
        if (timeRemapTrack.empty()) return timeRemap.mapFrame(sceneFrame);
        return timeRemapTrack.sample(sceneSeconds, resolve);
    }

    void Layer::sample(float seconds, Transform& out) const {
        sample(seconds, out, {});
    }

    void Layer::sample(float seconds, Transform& out, const LinkResolver& resolve) const {

        out = transform;

        tracks.position.sample(seconds, out.position, resolve);
        tracks.anchor.sample(seconds, out.anchor, resolve);
        tracks.scale.sample(seconds, out.scale, resolve);
        tracks.rotation.sample(seconds, out.rotation, resolve);
        tracks.orientation.sample(seconds, out.orientation, resolve);
        tracks.opacity.sample(seconds, out.opacity, resolve);
    }

    Transform Layer::sample(float seconds) const {

        Transform out;
        sample(seconds, out);
        return out;
    }

    Transform Layer::sample(float seconds, const LinkResolver& resolve) const {

        Transform out;
        sample(seconds, out, resolve);
        return out;
    }

    LayerMotion Layer::keyedMotion(float seconds) const {
        return keyedMotion(seconds, {});
    }

    LayerMotion Layer::keyedMotion(float seconds, const LinkResolver& resolveRates) const {

        LayerMotion out{};

        // Mirrors `sample()` channel for channel: whatever a key drives, its
        // derivative is reported; whatever has no keys contributes nothing,
        // because a held static pose is genuinely still. The transform's
        // `rotation` Euler and `orientation` are separate channels, so both
        // contribute to angular velocity — a layer that keys only one of them
        // is not silently treated as static.
        out.positionVelocity = tracks.position.velocity(seconds, resolveRates);
        out.linearVelocity = out.positionVelocity;
        out.opacityVelocity = tracks.opacity.velocity(seconds, resolveRates);

        // Euler rate and quaternion rate are two views of the same rotation and
        // must not be summed: the layer uses `orientation * eulerToQuaternion`,
        // so summing would double-count. Orientation wins when it is keyed,
        // because the composition is what the quaternions actually express.
        if (!tracks.orientation.empty()) {
            // An orientation track is keyed in quaternions, so its rate is the
            // infinitesimal rotation between the two keys. `LayerMotion` wants an
            // axis/rate vector, not another quaternion, so convert here: the axis
            // of the delta rotation, scaled by the rotation's angle. Both factors
            // carry units (1 and rad/s) that are lost if the quaternion is copied
            // into the vector field directly.
            const Quaternion delta = tracks.orientation.velocity(seconds, resolveRates);
            const float sinHalf = std::sqrt(static_cast<float>(delta.x * delta.x +
                                                               delta.y * delta.y +
                                                               delta.z * delta.z));
            if (sinHalf > 1e-8f) {
                const float angle = 2.f * std::atan2(sinHalf, static_cast<float>(delta.w));
                Vector3 axis{static_cast<float>(delta.x / sinHalf),
                             static_cast<float>(delta.y / sinHalf),
                             static_cast<float>(delta.z / sinHalf)};
                out.angularVelocity = axis.multiplyScalar(angle);
            }
        } else if (!tracks.rotation.empty()) {
            out.angularVelocity = tracks.rotation.velocity(seconds, resolveRates);
        }

        return out;
    }

    float Layer::endTime() const {

        return std::max(tracks.endTime(), timeRemapTrack.endTime());
    }

    Layer& Layer::named(std::string value) {

        Object3D::name = std::move(value);
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

    Layer& Layer::withMesh(MeshAttachment value) {

        mesh = std::move(value);
        return *this;
    }

    Layer& Layer::flat() {

        is3D = false;
        return *this;
    }

}// namespace chrononmotion::motion
