#pragma once

#include <cstdio>

namespace chrononmotion::platform {

inline FILE* openCommandPipe(const char* command, const char* mode) {
#ifdef _WIN32
    return _popen(command, mode);
#else
    return popen(command, mode);
#endif
}

inline int closeCommandPipe(FILE* pipe) {
#ifdef _WIN32
    return _pclose(pipe);
#else
    return pclose(pipe);
#endif
}

} // namespace chrononmotion::platform
