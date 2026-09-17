
#ifndef CHRONONMOTION_GROUP_HPP
#define CHRONONMOTION_GROUP_HPP

#include "chrononmotion/core/Object3D.hpp"

namespace chrononmotion {

    // This class is almost identical to an Object3D. Its purpose is to make working with groups of objects syntactically clearer.
    class Group: public Object3D {

    public:
        [[nodiscard]] std::string type() const override;

        static std::shared_ptr<Group> create();

        ~Group() override = default;

    protected:
        std::shared_ptr<Object3D> createDefault() override;
    };

}// namespace chrononmotion

#endif//CHRONONMOTION_GROUP_HPP
