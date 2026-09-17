
#ifndef CHRONONMOTION_INFINITY_HPP
#define CHRONONMOTION_INFINITY_HPP

#include <limits>

namespace chrononmotion {

    template<typename T>
    constexpr T Infinity = std::numeric_limits<T>::infinity();

}

#endif//CHRONONMOTION_INFINITY_HPP
