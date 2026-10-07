// https://github.com/mrdoob/three.js/blob/r129/src/core/BufferGeometry.js
//
// ChrononMotion: upstream's BufferGeometry also carried the data paths a
// renderer reads but which nothing in this tree can ever fill — the
// morph-target attribute set (morph attributes, `morphTargetsRelative`, and
// their handling in bounds/normal/transform/copy) and `drawRange` (a draw
// limiter whose only consumer was Mesh's deleted raycast). `groups` survives:
// "which index range uses which material" is geometry semantics, not renderer
// state, and a multi-material mesh needs it.

#ifndef CHRONONMOTION_BUFFERGEOMETRY_HPP
#define CHRONONMOTION_BUFFERGEOMETRY_HPP

#include "chrononmotion/math/Box3.hpp"
#include "chrononmotion/math/Sphere.hpp"

#include "chrononmotion/core/EventDispatcher.hpp"

#include "chrononmotion/core/BufferAttribute.hpp"

#include <atomic>
#include <optional>
#include <unordered_map>

namespace chrononmotion {

    class BufferGeometry: public EventDispatcher {

    public:
        // Atomic, and 1-based as before (fetch_add returns the OLD value, so the
        // +1 preserves the original `++_id` numbering). See Object3D::id for why:
        // loaders build geometries on a detached worker thread.
        const unsigned int id{_id.fetch_add(1, std::memory_order_relaxed) + 1};

        // Automatically assigned; only serialization round-trips (ObjectLoader)
        // have a reason to overwrite it.
        std::string uuid;

        std::string name;

        std::vector<GeometryGroup> groups;

        std::optional<Box3> boundingBox;
        std::optional<Sphere> boundingSphere;

        BufferGeometry();

        BufferGeometry(const BufferGeometry&) = delete;
        BufferGeometry& operator=(const BufferGeometry&) = delete;
        BufferGeometry(BufferGeometry&&) = delete;
        BufferGeometry& operator=(BufferGeometry&&) = delete;

        [[nodiscard]] virtual std::string type() const;

        [[nodiscard]] bool hasIndex() const;

        IntBufferAttribute* getIndex();

        [[nodiscard]] const IntBufferAttribute* getIndex() const;

        template<class ArrayLike>
        BufferGeometry& setIndex(const ArrayLike& index) {

            checkMutable();
            this->index_ = IntBufferAttribute::create(index, 1);
            ++attributesVersion_;

            return *this;
        }

        // Move overload: takes ownership of the index array without copying it.
        // Preferred by overload resolution for rvalue std::vector<unsigned int>
        // arguments (an exact non-template match beats the ArrayLike template),
        // so `setIndex(std::move(indices))` moves rather than copies.
        BufferGeometry& setIndex(std::vector<unsigned int>&& index) {

            checkMutable();
            this->index_ = IntBufferAttribute::create(std::move(index), 1);
            ++attributesVersion_;

            return *this;
        }

        // Internal STRUCTURAL version of the attribute set: bumped whenever an
        // attribute (or the index) is added, replaced or removed — NOT when
        // attribute *contents* mutate (that is BufferAttribute::version).
        // Consumers use it to cache attribute lookups safely across frames: an
        // unchanged value guarantees the attribute map still holds the same
        // objects, so cached pointers cannot dangle. Plain polling — no user
        // action ever required.
        [[nodiscard]] unsigned int attributesVersion() const { return attributesVersion_; }

        BufferAttribute* getAttribute(const std::string& name);

        [[nodiscard]] const BufferAttribute* getAttribute(const std::string& name) const;

        template<class T>
        TypedBufferAttribute<T>* getAttribute(const std::string& name) {

            if (!hasAttribute(name)) return nullptr;

            return dynamic_cast<TypedBufferAttribute<T>*>(attributes_.at(name).get());
        }

        template<class T>
        const TypedBufferAttribute<T>* getAttribute(const std::string& name) const {

            if (!hasAttribute(name)) return nullptr;

            return dynamic_cast<TypedBufferAttribute<T>*>(attributes_.at(name).get());
        }

        [[nodiscard]] const std::unordered_map<std::string, std::shared_ptr<BufferAttribute>>& getAttributes() const;

        void setAttribute(const std::string& name, std::shared_ptr<BufferAttribute> attribute);
        void freeze() { readOnly_ = true; }
        [[nodiscard]] bool readOnly() const { return readOnly_; }
        void checkMutable() const {
            if (readOnly_) throw std::logic_error("BufferGeometry: geometry is read-only");
        }

        void deleteAttribute(const std::string& name);

        [[nodiscard]] bool hasAttribute(const std::string& name) const;

        void addGroup(int start, int count, unsigned int materialIndex = 0);

        void clearGroups();

        BufferGeometry& applyMatrix4(const Matrix4& matrix);

        BufferGeometry& applyQuaternion(const Quaternion& q);

        BufferGeometry& rotateX(float angle);

        BufferGeometry& rotateY(float angle);

        BufferGeometry& rotateZ(float angle);

        BufferGeometry& translate(float x, float y, float z);

        BufferGeometry& scale(float x, float y, float z);

        BufferGeometry& center();

        BufferGeometry& setFromPoints(const std::vector<Vector2>& points);

        BufferGeometry& setFromPoints(const std::vector<Vector3>& points);

        void computeBoundingBox();

        void computeBoundingSphere();

        void normalizeNormals();

        [[nodiscard]] std::shared_ptr<BufferGeometry> toNonIndexed() const;

        void computeVertexNormals();

        void dispose();

        void copy(const BufferGeometry& source);

        [[nodiscard]] std::shared_ptr<BufferGeometry> clone() const;

        ~BufferGeometry() override;

        static std::shared_ptr<BufferGeometry> create();

    private:
        bool disposed_ = false;
        bool readOnly_ = false;
        std::unique_ptr<IntBufferAttribute> index_;
        std::unordered_map<std::string, std::shared_ptr<BufferAttribute>> attributes_;
        unsigned int attributesVersion_ = 0;// see attributesVersion()

        inline static std::atomic<unsigned int> _id{0};
    };

}// namespace chrononmotion

#endif//CHRONONMOTION_BUFFERGEOMETRY_HPP
