
#ifndef THREEPP_LOAD_GLAD_HPP
#define THREEPP_LOAD_GLAD_HPP

#include <glad/glad.h>


namespace threepp {

    void loadGlad();
    void loadGladWithProc(GLADloadproc getProcAddress);
}

#endif//THREEPP_LOAD_GLAD_HPP
