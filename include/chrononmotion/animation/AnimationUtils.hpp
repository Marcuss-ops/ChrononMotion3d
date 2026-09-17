
#ifndef CHRONONMOTION_ANIMATIONUTILS_HPP
#define CHRONONMOTION_ANIMATIONUTILS_HPP

#include <vector>

namespace chrononmotion::AnimationUtils {

    inline std::vector<float> arraySlice(const std::vector<float>& array, int from = 0, int to = 0) {

        if (from == 0 && to == 0) return array;

        return {array.begin() + from, array.begin() + to};
    }

}// namespace chrononmotion::AnimationUtils

#endif//CHRONONMOTION_ANIMATIONUTILS_HPP
