// https://github.com/mrdoob/three.js/blob/r129/src/core/BufferAttribute.js

#ifndef CHRONONMOTION_BUFFER_ATTRIBUTE_BASE_HPP
#define CHRONONMOTION_BUFFER_ATTRIBUTE_BASE_HPP

#include "chrononmotion/math/Box3.hpp"
#include "chrononmotion/math/Color.hpp"
#include "chrononmotion/math/Vector2.hpp"
#include "chrononmotion/math/Vector3.hpp"
#include "chrononmotion/math/Vector4.hpp"

#include "chrononmotion/constants.hpp"
#include "chrononmotion/core/Assert.hpp"
#include "chrononmotion/core/misc.hpp"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <type_traits>
#include <vector>

namespace chrononmotion {

    template<class T>
    class TypedBufferAttribute;

    // The scalar type backing a BufferAttribute's array.
    //
    // The narrow types exist to cut the resident cost of vertex data: a normal
    // stored as Int16 (normalized) is 3x6=6 bytes instead of 12, a UV as UInt16
    // is 4 instead of 8, and a vertex colour as UInt8 is 3 instead of 12. See
    // compressAttributes() in BufferGeometryUtils.hpp for the opt-in conversion.
    //
    // Narrow attributes carry raw integers; `normalized()` declares that the
    // consumer should map them onto [0,1] (unsigned) or [-1,1] (signed). getX()
    // and friends always return the *stored* value — use denormalize() or
    // attributeToFloatArray() when a float is wanted.
    enum class AttributeType {
        Float,
        UInt32,
        UInt16,
        Int16,
        UInt8,
        Int8
    };

    namespace detail {

        template<class T>
        struct AttributeTypeOf {
            static_assert(sizeof(T) == 0,
                          "TypedBufferAttribute supports float, unsigned int, "
                          "uint16_t, int16_t, uint8_t and int8_t only");
        };

        // clang-format off
        template<> struct AttributeTypeOf<float>         { static constexpr AttributeType value = AttributeType::Float;  };
        template<> struct AttributeTypeOf<unsigned int>  { static constexpr AttributeType value = AttributeType::UInt32; };
        template<> struct AttributeTypeOf<std::uint16_t> { static constexpr AttributeType value = AttributeType::UInt16; };
        template<> struct AttributeTypeOf<std::int16_t>  { static constexpr AttributeType value = AttributeType::Int16;  };
        template<> struct AttributeTypeOf<std::uint8_t>  { static constexpr AttributeType value = AttributeType::UInt8;  };
        template<> struct AttributeTypeOf<std::int8_t>   { static constexpr AttributeType value = AttributeType::Int8;   };
        // clang-format on

    }// namespace detail

    [[nodiscard]] constexpr size_t bytesPerElement(AttributeType type) {

        switch (type) {
            case AttributeType::Float:
            case AttributeType::UInt32: return 4;
            case AttributeType::UInt16:
            case AttributeType::Int16: return 2;
            case AttributeType::UInt8:
            case AttributeType::Int8: return 1;
        }
        return 0;
    }

    [[nodiscard]] constexpr bool isIntegral(AttributeType type) {

        return type != AttributeType::Float;
    }

    // Map a stored integer onto the range its `normalized` flag implies, matching
    // the OpenGL/Vulkan UNORM and SNORM conversion rules exactly. Float
    // attributes pass through untouched.
    [[nodiscard]] inline float denormalize(float value, AttributeType type) {

        switch (type) {
            case AttributeType::Float: return value;
            case AttributeType::UInt32: return value / 4294967295.f;
            case AttributeType::UInt16: return value / 65535.f;
            case AttributeType::Int16: return std::max(value / 32767.f, -1.f);
            case AttributeType::UInt8: return value / 255.f;
            case AttributeType::Int8: return std::max(value / 127.f, -1.f);
        }
        return value;
    }

    // ChrononMotion: upstream's attribute also carried GPU upload state — a
    // partial `updateRange`, a `DrawUsage` hint (StaticDraw/DynamicDraw) and an
    // `InstancedBufferAttribute` with a vertex-attrib divisor. All three only
    // mean something to a renderer that uploads the buffer, and nothing in this
    // tree consumes them, so they are gone. What is left is the data: the
    // backing array, its item size, and a version a consumer can poll.
    class BufferAttribute {

    public:
        unsigned int version = 0;
        void freeze() { readOnly_ = true; }
        [[nodiscard]] bool readOnly() const { return readOnly_; }
        void checkMutable() const {
            if (readOnly_) throw std::logic_error("BufferAttribute: attribute is read-only");
        }
        [[nodiscard]] virtual int count() const = 0;

        // Type-erased access to the backing store. These let consumers that only
        // move bytes around — buffer uploads, geometry copies — stay agnostic of
        // the scalar type instead of enumerating every instantiation.
        [[nodiscard]] virtual AttributeType type() const = 0;

        [[nodiscard]] virtual const void* data() const = 0;

        [[nodiscard]] virtual size_t byteLength() const = 0;

        [[nodiscard]] virtual std::unique_ptr<BufferAttribute> cloneUntyped() const = 0;

        [[nodiscard]] int itemSize() const {

            return itemSize_;
        }

        [[nodiscard]] bool normalized() const {

            return normalized_;
        }

        void needsUpdate() {

            checkMutable();
            ++version;
        }

        template<class T>
        TypedBufferAttribute<T>* typed() {

            return dynamic_cast<TypedBufferAttribute<T>*>(this);
        }

        virtual ~BufferAttribute() = default;

    protected:
        bool readOnly_{false};
        int itemSize_{};
        bool normalized_{};

        BufferAttribute() = default;

        BufferAttribute(int itemSize, bool normalized)
            : itemSize_(itemSize), normalized_(normalized) {}

        void copy(const BufferAttribute& source) {

            this->itemSize_ = source.itemSize_;
            this->normalized_ = source.normalized_;
        }
    };


}// namespace chrononmotion

#endif // CHRONONMOTION_BUFFER_ATTRIBUTE_BASE_HPP
