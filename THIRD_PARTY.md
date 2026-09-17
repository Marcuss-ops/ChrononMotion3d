# Third-party software and assets

ChrononMotion carries **no vendored third-party code and no bundled assets**, and
fetches nothing at configure or build time. Everything it builds is derived from
threepp / three.js source, plus the standard library.

The index below is kept for the parts that still apply; the machinery it used to
describe (vendored GLFW/glad/libwebp/meshoptimizer/miniaudio/nlohmann/pugixml/
quickhull/stb/earcut/, the embedded helvetiker typeface, `threepp_data`, and the
vcpkg/FetchContent dependencies for PhysX, V-HACD, Assimp, Vulkan, glslang,
pybind11, tinyusdz, OpenFBX, Catch2, RLtools, FSR and DLSS) was deleted with the
renderer, the loader zoo, the examples, the tests and the applications. See
[CHRONONMOTION.md](CHRONONMOTION.md) for the ledger.

---

## three.js

ChrononMotion is a derivative work of threepp, which is a C++ port of
[three.js](https://github.com/mrdoob/three.js) (r129): the public API mirrors it
by design, and the surviving sources are the ported implementation of that API.
Substantial portions of this repository are therefore a derivative work of
three.js, and its license rides with them:

> The MIT License
>
> Copyright © 2010-2021 three.js authors
>
> Permission is hereby granted, free of charge, to any person obtaining a copy of this software
> and associated documentation files (the "Software"), to deal in the Software without
> restriction, including without limitation the rights to use, copy, modify, merge, publish,
> distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the
> Software is furnished to do so, subject to the following conditions:
>
> The above copyright notice and this permission notice shall be included in all copies or
> substantial portions of the Software.
>
> THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
> BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
> NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
> DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
> OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

## threepp

The sources under `include/threepp/` and `src/threepp/` are threepp's, trimmed;
threepp is MIT-licensed — see [LICENSE](LICENSE). Where a surviving file differs
from upstream, the file says so in a header comment, and the delta is listed in
[CHRONONMOTION.md](CHRONONMOTION.md).
