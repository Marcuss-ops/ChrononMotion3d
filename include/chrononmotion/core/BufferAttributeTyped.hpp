#ifndef CHRONONMOTION_BUFFER_ATTRIBUTE_TYPED_HPP
#define CHRONONMOTION_BUFFER_ATTRIBUTE_TYPED_HPP

#include "chrononmotion/core/BufferAttributeBase.hpp"

namespace chrononmotion {

    template<class T>
    class TypedBufferAttribute: public BufferAttribute {

    public:
        using value_type = T;

        static constexpr AttributeType scalarType = detail::AttributeTypeOf<T>::value;

        [[nodiscard]] int count() const override {

            return count_;
        }

        [[nodiscard]] AttributeType type() const override {

            return scalarType;
        }

        // Routed through the virtual array() so InterleavedBufferAttribute, whose
        // storage lives in the shared InterleavedBuffer rather than in array_,
        // reports the buffer it actually reads from.
        [[nodiscard]] const void* data() const override {

            return array().data();
        }

        [[nodiscard]] size_t byteLength() const override {

            return array().size() * sizeof(T);
        }

        [[nodiscard]] std::unique_ptr<BufferAttribute> cloneUntyped() const override {

            return clone();
        }

        virtual const std::vector<T>& array() const {

            return array_;
        }

        virtual std::vector<T>& array() {

            this->checkMutable();
            return array_;
        }


        // Copy one element from `attribute` into this attribute.
        // NB: this used to `return &this;` — taking the address of a prvalue,
        // which is ill-formed. It only ever compiled because nothing instantiated
        // it; the first caller would have been a hard error.
        TypedBufferAttribute<T>& copyAt(unsigned int index1, const TypedBufferAttribute<T>& attribute, unsigned int index2) {

            this->checkMutable();
            index1 *= this->itemSize_;
            index2 *= attribute.itemSize_;

            for (auto i = 0, l = this->itemSize_; i < l; i++) {

                this->array_[index1 + i] = attribute.array_[index2 + i];
            }

            return *this;
        }

        TypedBufferAttribute<T>& copyArray(const std::vector<T>& array) {

            this->checkMutable();
            this->array_ = array;

            return *this;
        }

        TypedBufferAttribute<T>& copyColorsArray(const std::vector<Color>& colors) {

            this->checkMutable();
            unsigned int offset = 0;

            for (const auto& color : colors) {

                array_[offset++] = color.r;
                array_[offset++] = color.g;
                array_[offset++] = color.b;
            }

            return *this;
        }

        TypedBufferAttribute<T>& copyVector2sArray(const std::vector<Vector2>& vectors) {

            this->checkMutable();
            unsigned int offset = 0;

            for (const auto& vector : vectors) {

                array_[offset++] = vector.x;
                array_[offset++] = vector.y;
            }

            return *this;
        }

        TypedBufferAttribute<T>& copyVector3sArray(const std::vector<Vector3>& vectors) {

            this->checkMutable();
            unsigned int offset = 0;

            for (const auto& vector : vectors) {

                array_[offset++] = vector.x;
                array_[offset++] = vector.y;
                array_[offset++] = vector.z;
            }

            return *this;
        }

        TypedBufferAttribute<T>& copyVector4sArray(std::vector<Vector4>& vectors) {

            this->checkMutable();
            unsigned int offset = 0;

            for (const auto& vector : vectors) {

                array_[offset++] = vector.x;
                array_[offset++] = vector.y;
                array_[offset++] = vector.z;
                array_[offset++] = vector.w;
            }

            return *this;
        }

        // The scratch vectors below are deliberately function-local. They used to
        // be shared `inline static` members, which made every one of these
        // transforms a data race when two threads processed different geometries
        // at once (loaders and sensor code do exactly that). A 12-byte stack
        // local is also cheaper than thread-local storage.
        TypedBufferAttribute<T>& applyMatrix3(const Matrix3& m) {

            if (this->itemSize_ == 2) {

                Vector2 v;
                for (unsigned i = 0, l = this->count_; i < l; i++) {

                    setFromBufferAttribute(v, i);
                    v.applyMatrix3(m);

                    this->setXY(i, v.x, v.y);
                }

            } else if (this->itemSize_ == 3) {

                Vector3 v;
                for (unsigned i = 0, l = this->count_; i < l; i++) {

                    setFromBufferAttribute(v, i);
                    v.applyMatrix3(m);

                    this->setXYZ(i, v.x, v.y, v.z);
                }
            }

            return *this;
        }

        TypedBufferAttribute<T>& applyMatrix4(const Matrix4& m) {

            Vector3 v;
            for (unsigned i = 0, l = this->count_; i < l; i++) {

                v.x = this->getX(i);
                v.y = this->getY(i);
                v.z = this->getZ(i);

                v.applyMatrix4(m);

                this->setXYZ(i, v.x, v.y, v.z);
            }

            return *this;
        }

        TypedBufferAttribute<T>& applyNormalMatrix(const Matrix3& m) {

            Vector3 v;
            for (unsigned i = 0, l = this->count_; i < l; i++) {

                v.x = this->getX(i);
                v.y = this->getY(i);
                v.z = this->getZ(i);

                v.applyNormalMatrix(m);

                this->setXYZ(i, v.x, v.y, v.z);
            }

            return *this;
        }

        TypedBufferAttribute<T>& transformDirection(const Matrix4& m) {

            Vector3 v;
            for (unsigned i = 0, l = this->count_; i < l; i++) {

                v.x = this->getX(i);
                v.y = this->getY(i);
                v.z = this->getZ(i);

                v.transformDirection(m);

                this->setXYZ(i, v.x, v.y, v.z);
            }

            return *this;
        }

        [[nodiscard]] virtual T getX(size_t index) const {

            checkAccess(index, 0);
            return this->array_[index * this->itemSize_];
        }

        virtual TypedBufferAttribute<T>& setX(size_t index, T x) {

            this->checkMutable();
            checkAccess(index, 0);
            this->array_[index * this->itemSize_] = x;

            return *this;
        }

        [[nodiscard]] virtual T getY(size_t index) const {

            checkAccess(index, 1);
            return this->array_[index * this->itemSize_ + 1];
        }

        virtual TypedBufferAttribute<T>& setY(size_t index, T y) {

            this->checkMutable();
            checkAccess(index, 1);
            this->array_[index * this->itemSize_ + 1] = y;

            return *this;
        }

        [[nodiscard]] virtual T getZ(size_t index) const {

            checkAccess(index, 2);
            return this->array_[index * this->itemSize_ + 2];
        }

        virtual TypedBufferAttribute<T>& setZ(size_t index, T z) {

            this->checkMutable();
            checkAccess(index, 2);
            this->array_[index * this->itemSize_ + 2] = z;

            return *this;
        }

        [[nodiscard]] virtual T getW(size_t index) const {

            checkAccess(index, 3);
            return this->array_[index * this->itemSize_ + 3];
        }

        virtual TypedBufferAttribute<T>& setW(size_t index, T w) {

            this->checkMutable();
            checkAccess(index, 3);
            this->array_[index * this->itemSize_ + 3] = w;

            return *this;
        }

        virtual TypedBufferAttribute<T>& setXY(size_t index, T x, T y) {

            this->checkMutable();
            checkAccess(index, 1);
            index *= this->itemSize_;

            this->array_[index + 0] = x;
            this->array_[index + 1] = y;

            return *this;
        }

        virtual TypedBufferAttribute<T>& setXYZ(size_t index, T x, T y, T z) {

            this->checkMutable();
            checkAccess(index, 2);
            index *= this->itemSize_;

            this->array_[index + 0] = x;
            this->array_[index + 1] = y;
            this->array_[index + 2] = z;

            return *this;
        }

        virtual TypedBufferAttribute<T>& setXYZW(size_t index, T x, T y, T z, T w) {

            this->checkMutable();
            checkAccess(index, 3);
            index *= this->itemSize_;

            this->array_[index + 0] = x;
            this->array_[index + 1] = y;
            this->array_[index + 2] = z;
            this->array_[index + 3] = w;

            return *this;
        }

        void setFromBufferAttribute(Vector2& target, size_t index) const {

            target.x = getX(index);
            target.y = getY(index);
        }

        void setFromBufferAttribute(Vector3& target, size_t index) const {

            target.x = getX(index);
            target.y = getY(index);
            target.z = getZ(index);
        }

        void setFromBufferAttribute(Vector4& target, size_t index) const {

            target.x = getX(index);
            target.y = getY(index);
            target.z = getZ(index);
            target.w = getW(index);
        }

        void setFromBufferAttribute(Box3& target) const {

            auto minX = +Infinity<float>;
            auto minY = +Infinity<float>;
            auto minZ = +Infinity<float>;

            auto maxX = -Infinity<float>;
            auto maxY = -Infinity<float>;
            auto maxZ = -Infinity<float>;

            for (unsigned i = 0, l = count(); i < l; i++) {

                const auto x = getX(i);
                const auto y = getY(i);
                const auto z = getZ(i);

                if (x < minX) minX = x;
                if (y < minY) minY = y;
                if (z < minZ) minZ = z;

                if (x > maxX) maxX = x;
                if (y > maxY) maxY = y;
                if (z > maxZ) maxZ = z;
            }

            target.set(minX, minY, minZ, maxX, maxY, maxZ);
        }

        void copy(const TypedBufferAttribute& source) {
            BufferAttribute::copy(source);

            this->count_ = source.count_;
            this->array_ = source.array_;
        }

        [[nodiscard]] std::unique_ptr<TypedBufferAttribute> clone() const {
            auto clone = std::unique_ptr<TypedBufferAttribute>(new TypedBufferAttribute());
            clone->copy(*this);

            return clone;
        }

        static std::unique_ptr<TypedBufferAttribute> create(std::initializer_list<T>&& array, int itemSize, bool normalized = false) {

            return create(std::vector<T>{array.begin(), array.end()}, itemSize, normalized);
        }

        template<std::ranges::range Range>
        static std::unique_ptr<TypedBufferAttribute> create(const Range& range, int itemSize, bool normalized = false) {

            return std::unique_ptr<TypedBufferAttribute>(new TypedBufferAttribute({std::ranges::begin(range), std::ranges::end(range)}, itemSize, normalized));
        }

        // Move overload: takes ownership of `array` without copying its contents.
        // Loaders that decode large attribute/index arrays should prefer this to
        // avoid a transient duplicate of every buffer. Disambiguated from the
        // std::ranges::range template above via a distinct rvalue-vector signature.
        static std::unique_ptr<TypedBufferAttribute> create(std::vector<T>&& array, int itemSize, bool normalized = false) {

            return std::unique_ptr<TypedBufferAttribute>(new TypedBufferAttribute(std::move(array), itemSize, normalized));
        }

    protected:
        TypedBufferAttribute() = default;

        TypedBufferAttribute(const std::vector<T>& array, int count): array_(array), count_(count) {}

        TypedBufferAttribute(const std::vector<T>& array, int itemSize, bool normalized)
            : BufferAttribute(itemSize, normalized), array_(array), count_(array_.size() / itemSize) {
            checkShape();
        }

        TypedBufferAttribute(std::vector<T>&& array, int itemSize, bool normalized)
            : BufferAttribute(itemSize, normalized), array_(std::move(array)), count_(static_cast<int>(array_.size()) / itemSize) {
            checkShape();
        }

        void checkShape() const {

            CHRONONMOTION_ASSERT_MSG(this->itemSize_ > 0, "BufferAttribute: itemSize must be positive");
            CHRONONMOTION_ASSERT_MSG(this->array_.size() % static_cast<size_t>(this->itemSize_) == 0,
                               "BufferAttribute: array size is not a multiple of itemSize");
        }

    private:
        std::vector<T> array_;
        int count_{};

        // Guards the two ways an element accessor goes wrong: reading past the
        // end of the array (undefined behaviour), and asking for a component the
        // attribute does not carry — getW() on an itemSize-3 attribute silently
        // returns the *next* vertex's X, which the bounds check alone misses.
        // Compiled out entirely unless CHRONONMOTION_ENABLE_ASSERTS.
        void checkAccess([[maybe_unused]] size_t index, [[maybe_unused]] int component) const {

#if CHRONONMOTION_ENABLE_ASSERTS
            CHRONONMOTION_ASSERT_MSG(this->itemSize_ > component,
                               "BufferAttribute: component index >= itemSize");
            CHRONONMOTION_ASSERT_MSG(index * static_cast<size_t>(this->itemSize_) +
                                               static_cast<size_t>(component) <
                                       this->array_.size(),
                               "BufferAttribute: element index out of range");
#endif
        }
    };

    typedef TypedBufferAttribute<unsigned int> IntBufferAttribute;
    typedef TypedBufferAttribute<float> FloatBufferAttribute;

    // Narrow attribute types. Pair these with `normalized = true` for directional
    // or [0,1] data so the renderer expands them on fetch at no bandwidth cost:
    //
    //   normal   -> Int16BufferAttribute,  normalized  (6 bytes/vertex, was 12)
    //   tangent  -> Int16BufferAttribute,  normalized  (8 bytes/vertex, was 16)
    //   uv       -> Uint16BufferAttribute, normalized  (4 bytes/vertex, was 8)
    //   color    -> Uint8BufferAttribute,  normalized  (3 bytes/vertex, was 12)
    //   skinIndex-> Uint16BufferAttribute, raw         (8 bytes/vertex, was 16)
    //
    // Positions are deliberately absent from that list: they need the dynamic
    // range of float unless the geometry is quantised against a known AABB.
    typedef TypedBufferAttribute<std::uint16_t> Uint16BufferAttribute;
    typedef TypedBufferAttribute<std::int16_t> Int16BufferAttribute;
    typedef TypedBufferAttribute<std::uint8_t> Uint8BufferAttribute;
    typedef TypedBufferAttribute<std::int8_t> Int8BufferAttribute;

}// namespace chrononmotion

#endif // CHRONONMOTION_BUFFER_ATTRIBUTE_TYPED_HPP
