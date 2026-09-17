#ifndef CHRONONMOTION_OBJECTWITHMATERIALS_HPP
#define CHRONONMOTION_OBJECTWITHMATERIALS_HPP

#include "chrononmotion/core/Object3D.hpp"
#include "chrononmotion/materials/Material.hpp"

#include <vector>

namespace chrononmotion {

    class ObjectWithMaterials: public virtual Object3D {

    public:
        // The first material, or nullptr when this object carries none. A node
        // is allowed to have no material at all — nothing here invents one.
        std::shared_ptr<Material> material() const;

        void setMaterial(const std::shared_ptr<Material>& material);

        const std::vector<std::shared_ptr<Material>>& materials() const;

        void setMaterials(const std::vector<std::shared_ptr<Material>>& materials);

        [[nodiscard]] size_t numMaterials() const;

    protected:
        std::vector<std::shared_ptr<Material>> materials_;

        explicit ObjectWithMaterials(std::vector<std::shared_ptr<Material>> materials);
    };

}// namespace chrononmotion

#endif//CHRONONMOTION_OBJECTWITHMATERIALS_HPP
