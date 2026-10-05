#include "gl_loader.hpp"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <cstdio>

namespace gl {

#define GL_DEFINE(ret, name, ...) PFN_##name name = nullptr;
GL_FUNCTIONS(GL_DEFINE)
#undef GL_DEFINE

bool load()
{
    bool ok = true;
#define GL_LOAD(ret, name, ...)                                                  \
    name = reinterpret_cast<PFN_##name>(glfwGetProcAddress("gl" #name));         \
    if (!name) {                                                                 \
        std::fprintf(stderr, "[gl] fonction introuvable : gl%s\n", #name);       \
        ok = false;                                                              \
    }
    GL_FUNCTIONS(GL_LOAD)
#undef GL_LOAD
    return ok;
}

} // namespace gl
