# render/ third-party inventory

All sources under `render/` are reintroduced from upstream
[threepp](https://github.com/markaren/threepp) at pinned revision
`e2b165ece3904012d97a08029e0bd6cd5252892a`, plus the vendored dependencies
threepp itself builds against. Nothing is fetched at configure time.

| Component | Location | License |
|---|---|---|
| threepp sources (GL renderer, materials, lights, textures, loaders, canvas, scene core, animation mixer/property bindings, OpenGL post-processing) | `include/threepp/`, `src/threepp/`, `shaders/` | MIT — see `../LICENSE`, copyright headers retained |
| glad (OpenGL loader) | `thirdparty/glad/` | MIT (header retained in `glad.h`) |
| GLFW (headless window/context) | `thirdparty/glfw/` | zlib/libpng — see `thirdparty/glfw/LICENSE.md` |
| stb_image / stb_image_write / stb_truetype | `thirdparty/stb/` | MIT (in-file headers retained) |
| nlohmann/json | `thirdparty/nlohmann/` | MIT (in-file header retained) |
| meshoptimizer | `thirdparty/meshoptimizer/` | MIT (in-file header retained) |
| quickhull | `thirdparty/quickhull/` | MIT (in-file header retained) |
| libwebp decoder subset | `thirdparty/libwebp/` | BSD-style — see `thirdparty/libwebp/COPYING` and `PATENTS` |
| three.js (origin of threepp's port) | via threepp | MIT (three.js authors, quoted in `../THIRD_PARTY.md`) |

Excluded on purpose: Vulkan renderer, audio, editor,
examples, python bindings — see `README.md`.
