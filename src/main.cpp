// Black Hole Test : ray tracing d'un trou noir de Schwarzschild en OpenGL.
//
// Tout le calcul des rayons se fait dans le fragment shader
// (shaders/blackhole.frag) : un rayon par pixel, dont la trajectoire est
// courbée par la gravité du trou noir. Ce fichier gère la fenêtre,
// la caméra orbitale et les entrées clavier/souris.
//
// Contrôles :
//   clic gauche + glisser : tourner autour du trou noir
//   molette               : zoomer / dézoomer
//   R                     : recharger les shaders (édition à chaud)
//   Échap                 : quitter
//
// Option : --screenshot fichier.ppm [largeur hauteur]
//   rend une seule image dans un fichier puis quitte.

#include "gl_loader.hpp"
#include "shader.hpp"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

struct Vec3 {
    float x, y, z;
};

Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 cross(Vec3 a, Vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
Vec3 normalize(Vec3 v)
{
    float len = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    return {v.x / len, v.y / len, v.z / len};
}

// Caméra qui orbite autour de l'origine (le trou noir).
// Les distances sont exprimées en rayons de Schwarzschild (rs = 1).
struct OrbitCamera {
    float yaw = 0.6f;        // angle horizontal (radians)
    float pitch = 0.12f;     // angle vertical, petit pour voir le disque presque par la tranche
    float distance = 22.0f;
    float fovDeg = 60.0f;

    Vec3 position() const
    {
        return {distance * std::cos(pitch) * std::sin(yaw),
                distance * std::sin(pitch),
                distance * std::cos(pitch) * std::cos(yaw)};
    }
};

struct AppState {
    OrbitCamera camera;
    bool dragging = false;
    double lastX = 0.0, lastY = 0.0;
    bool reloadRequested = false;
};

void onMouseButton(GLFWwindow* window, int button, int action, int)
{
    auto* app = static_cast<AppState*>(glfwGetWindowUserPointer(window));
    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        app->dragging = (action == GLFW_PRESS);
        glfwGetCursorPos(window, &app->lastX, &app->lastY);
    }
}

void onCursorMove(GLFWwindow* window, double x, double y)
{
    auto* app = static_cast<AppState*>(glfwGetWindowUserPointer(window));
    if (!app->dragging)
        return;
    const float sensitivity = 0.005f;
    app->camera.yaw -= static_cast<float>(x - app->lastX) * sensitivity;
    app->camera.pitch += static_cast<float>(y - app->lastY) * sensitivity;
    app->camera.pitch = std::clamp(app->camera.pitch, -1.5f, 1.5f);
    app->lastX = x;
    app->lastY = y;
}

void onScroll(GLFWwindow* window, double, double dy)
{
    auto* app = static_cast<AppState*>(glfwGetWindowUserPointer(window));
    app->camera.distance *= std::pow(0.9f, static_cast<float>(dy));
    app->camera.distance = std::clamp(app->camera.distance, 3.0f, 100.0f);
}

void onKey(GLFWwindow* window, int key, int, int action, int)
{
    auto* app = static_cast<AppState*>(glfwGetWindowUserPointer(window));
    if (action != GLFW_PRESS)
        return;
    if (key == GLFW_KEY_ESCAPE)
        glfwSetWindowShouldClose(window, GLFW_TRUE);
    if (key == GLFW_KEY_R)
        app->reloadRequested = true;
}

// Envoie au shader la position et l'orientation de la caméra.
void uploadCamera(const Shader& shader, const OrbitCamera& cam, int width, int height, float time)
{
    Vec3 pos = cam.position();
    Vec3 forward = normalize(Vec3{0, 0, 0} - pos);
    Vec3 right = normalize(cross(forward, Vec3{0, 1, 0}));
    Vec3 up = cross(right, forward);
    // Matrice colonne par colonne : right, up, forward.
    const float basis[9] = {right.x, right.y, right.z, up.x, up.y, up.z, forward.x, forward.y, forward.z};

    gl::Uniform2f(shader.uniform("uResolution"), static_cast<float>(width), static_cast<float>(height));
    gl::Uniform1f(shader.uniform("uTime"), time);
    gl::Uniform3f(shader.uniform("uCamPos"), pos.x, pos.y, pos.z);
    gl::UniformMatrix3fv(shader.uniform("uCamBasis"), 1, 0, basis);
    gl::Uniform1f(shader.uniform("uFov"), cam.fovDeg * 3.14159265f / 180.0f);
}

bool saveFramePPM(const std::string& path, int width, int height)
{
    std::vector<unsigned char> pixels(static_cast<size_t>(width) * height * 3);
    gl::PixelStorei(gl::PACK_ALIGNMENT, 1);
    gl::ReadPixels(0, 0, width, height, gl::RGB, gl::UNSIGNED_BYTE, pixels.data());

    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f)
        return false;
    std::fprintf(f, "P6\n%d %d\n255\n", width, height);
    // OpenGL lit de bas en haut, le PPM s'écrit de haut en bas.
    for (int y = height - 1; y >= 0; --y)
        std::fwrite(&pixels[static_cast<size_t>(y) * width * 3], 1, static_cast<size_t>(width) * 3, f);
    std::fclose(f);
    return true;
}

// Dossier des shaders : à côté de l'exécutable (copié par CMake).
std::string shaderDir(const char* argv0)
{
    std::string exe(argv0);
    size_t slash = exe.find_last_of("/\\");
    return (slash == std::string::npos ? std::string(".") : exe.substr(0, slash)) + "/shaders/";
}

// Boucle de rendu. Les objets OpenGL (Shader, VAO) vivent dans cette
// fonction pour être détruits avant la fermeture du contexte.
int run(GLFWwindow* window, AppState& app, const std::string& dir, const std::string& screenshotPath)
{
    const bool offscreen = !screenshotPath.empty();
    Shader shader;
    if (!shader.load(dir + "fullscreen.vert", dir + "blackhole.frag"))
        return EXIT_FAILURE;

    // Le core profile exige un VAO, même vide : le triangle plein écran
    // est généré dans le vertex shader à partir de gl_VertexID.
    GLuint vao = 0;
    gl::GenVertexArrays(1, &vao);
    gl::BindVertexArray(vao);

    int exitCode = EXIT_SUCCESS;
    while (!glfwWindowShouldClose(window)) {
        if (app.reloadRequested) {
            app.reloadRequested = false;
            if (shader.load(dir + "fullscreen.vert", dir + "blackhole.frag"))
                std::printf("Shaders rechargés.\n");
        }

        int fbWidth = 0, fbHeight = 0;
        glfwGetFramebufferSize(window, &fbWidth, &fbHeight);
        gl::Viewport(0, 0, fbWidth, fbHeight);
        gl::Clear(gl::COLOR_BUFFER_BIT);

        shader.use();
        uploadCamera(shader, app.camera, fbWidth, fbHeight, static_cast<float>(glfwGetTime()));
        gl::DrawArrays(gl::TRIANGLES, 0, 3);

        if (offscreen) {
            gl::Finish();
            gl::ReadBuffer(gl::BACK);
            if (saveFramePPM(screenshotPath, fbWidth, fbHeight))
                std::printf("Image enregistrée : %s (%dx%d)\n", screenshotPath.c_str(), fbWidth, fbHeight);
            else
                exitCode = EXIT_FAILURE;
            break;
        }

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    gl::DeleteVertexArrays(1, &vao);
    return exitCode;
}

} // namespace

int main(int argc, char** argv)
{
    std::string screenshotPath;
    int width = 1280, height = 720;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--screenshot" && i + 1 < argc) {
            screenshotPath = argv[++i];
            if (i + 2 < argc) {
                width = std::atoi(argv[++i]);
                height = std::atoi(argv[++i]);
            }
        }
    }
    glfwSetErrorCallback([](int code, const char* desc) { std::fprintf(stderr, "[glfw] erreur %d : %s\n", code, desc); });
    if (!glfwInit())
        return EXIT_FAILURE;

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif
    if (!screenshotPath.empty())
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);

    GLFWwindow* window = glfwCreateWindow(width, height, "Black Hole Test", nullptr, nullptr);
    if (!window) {
        std::fprintf(stderr, "Impossible de créer une fenêtre OpenGL 3.3.\n");
        glfwTerminate();
        return EXIT_FAILURE;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    if (!gl::load()) {
        glfwTerminate();
        return EXIT_FAILURE;
    }
    std::printf("OpenGL %s sur %s\n", gl::GetString(gl::VERSION), gl::GetString(gl::RENDERER));

    AppState app;
    glfwSetWindowUserPointer(window, &app);
    glfwSetMouseButtonCallback(window, onMouseButton);
    glfwSetCursorPosCallback(window, onCursorMove);
    glfwSetScrollCallback(window, onScroll);
    glfwSetKeyCallback(window, onKey);

    int exitCode = run(window, app, shaderDir(argv[0]), screenshotPath);

    glfwDestroyWindow(window);
    glfwTerminate();
    return exitCode;
}

