#ifndef THREEPP_FAVICON_HPP
#define THREEPP_FAVICON_HPP

#include <vector>

namespace threepp {

// Headless render module: no window icon needed; stub satisfies Canvas.cpp.
inline std::vector<unsigned char> faviconSource() { return {}; }

}

#endif
