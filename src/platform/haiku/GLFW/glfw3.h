// The GLFW calls of BambuStudio.cpp, for Haiku.
//
// GLFW is not a dependency on Haiku (it has no port there). The only thing
// the application uses it for is the hidden OpenGL context in which the
// command line mode renders its thumbnails (BambuStudio.cpp: glfwInit(),
// glfwWindowHint(), glfwCreateWindow(), glfwMakeContextCurrent(),
// glfwTerminate()). This header declares exactly those calls so that
// BambuStudio.cpp compiles unchanged.
//
// As it stands there is no context: glfwInit() fails, which the caller
// already handles ("skip thumbnail generating"); slicing and export work
// without thumbnails. The native context to put behind
// glfwCreateWindow()/glfwMakeContextCurrent() is an off-screen BGLView
// (BGL_RGB | BGL_DEPTH | BGL_ALPHA on a hidden BWindow, LockGL() held by
// the calling thread), which needs the application's BApplication: that
// belongs with the GL canvas of the wxWidgets port.
#ifndef slic3r_platform_haiku_glfw3_h_
#define slic3r_platform_haiku_glfw3_h_

#define GLFW_FALSE 0
#define GLFW_TRUE 1
#define GLFW_NOT_INITIALIZED 0x00010001
#define GLFW_PLATFORM_ERROR 0x00010008

#define GLFW_VISIBLE 0x00020004
#define GLFW_RED_BITS 0x00021001
#define GLFW_GREEN_BITS 0x00021002
#define GLFW_BLUE_BITS 0x00021003
#define GLFW_ALPHA_BITS 0x00021004
#define GLFW_CONTEXT_VERSION_MAJOR 0x00022002
#define GLFW_CONTEXT_VERSION_MINOR 0x00022003
#define GLFW_OPENGL_FORWARD_COMPAT 0x00022006
#define GLFW_OPENGL_PROFILE 0x00022008
#define GLFW_CONTEXT_CREATION_API 0x0002200B
#define GLFW_OPENGL_CORE_PROFILE 0x00032001
#define GLFW_OPENGL_COMPAT_PROFILE 0x00032002
#define GLFW_OSMESA_CONTEXT_API 0x00036003

struct GLFWwindow;
struct GLFWmonitor;
typedef void (*GLFWerrorfun)(int error_code, const char *description);

namespace slic3r_haiku_glfw {
inline GLFWerrorfun &error_callback()
{
    static GLFWerrorfun callback = nullptr;
    return callback;
}
} // namespace slic3r_haiku_glfw

inline void glfwGetVersion(int *major, int *minor, int *revision)
{
    // the version the calls are taken from
    if (major != nullptr) *major = 3;
    if (minor != nullptr) *minor = 3;
    if (revision != nullptr) *revision = 7;
}

inline GLFWerrorfun glfwSetErrorCallback(GLFWerrorfun callback)
{
    GLFWerrorfun previous = slic3r_haiku_glfw::error_callback();
    slic3r_haiku_glfw::error_callback() = callback;
    return previous;
}

inline int glfwInit()
{
    if (slic3r_haiku_glfw::error_callback() != nullptr)
        slic3r_haiku_glfw::error_callback()(GLFW_PLATFORM_ERROR, "no off-screen OpenGL context on Haiku yet");
    return GLFW_FALSE;
}

inline int glfwGetError(const char **description)
{
    if (description != nullptr)
        *description = "no off-screen OpenGL context on Haiku yet";
    return GLFW_PLATFORM_ERROR;
}

inline void glfwWindowHint(int /*hint*/, int /*value*/) {}

inline GLFWwindow *glfwCreateWindow(int /*width*/, int /*height*/, const char * /*title*/, GLFWmonitor * /*monitor*/, GLFWwindow * /*share*/)
{
    return nullptr;
}

inline void glfwMakeContextCurrent(GLFWwindow * /*window*/) {}

inline void glfwTerminate() {}

#endif // slic3r_platform_haiku_glfw3_h_
