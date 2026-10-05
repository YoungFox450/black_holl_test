// Moteur de rendu ray tracing d'un trou noir (OpenGL 3.3 + GLFW + glad).
//
// Le CPU ne fait presque rien : il ouvre la fenêtre, gère la caméra et dessine
// un triangle plein écran. Tout le ray tracing se passe dans le fragment
// shader (shaders/blackhole.frag), un rayon par pixel, sur la carte graphique.
//
// Commandes :
//   clic gauche + glisser : tourner autour du trou noir
//   molette               : zoom
//   D                     : afficher / cacher le disque d'accrétion
//   R                     : recharger les shaders (après modification)
//   Échap                 : quitter
//
// Option : blackhole --screenshot image.ppm [--width W --height H] [--no-disk]
//   rend une seule image hors écran puis quitte (utile pour tester).

#include <glad/gl.h>
#include <GLFW/glfw3.h>

#include "shader.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

struct Vec3 {
    float x, y, z;
};
Vec3 operator-(Vec3 a) { return {-a.x, -a.y, -a.z}; }
Vec3 cross(Vec3 a, Vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
Vec3 normalize(Vec3 a)
{
    float l = std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z);
    return {a.x / l, a.y / l, a.z / l};
}

// Caméra en orbite autour du trou noir (placé à l'origine).
struct OrbitCamera {
    float yaw = 0.0f;
    float pitch = 0.09f;      // légèrement au-dessus du disque
    float distance = 22.0f;   // en rayons de Schwarzschild
    float fovY = 1.0f;        // ~57°

    Vec3 position() const
    {
        return {distance * std::cos(pitch) * std::sin(yaw),
                distance * std::sin(pitch),
                distance * std::cos(pitch) * std::cos(yaw)};
    }
};

struct App {
    OrbitCamera camera;
    bool dragging = false;
    double lastX = 0.0, lastY = 0.0;
    bool showDisk = true;
    bool reloadRequested = false;
};

std::string shaderDir = BH_SHADER_DIR;

GLuint buildProgram()
{
    return loadShaderProgram(shaderDir + "/fullscreen.vert", shaderDir + "/blackhole.frag");
}

void setUniforms(GLuint program, const OrbitCamera& cam, int width, int height, float time, bool disk)
{
    Vec3 pos = cam.position();
    Vec3 forward = normalize(-pos);
    Vec3 right = normalize(cross(forward, {0.0f, 1.0f, 0.0f}));
    Vec3 up = cross(right, forward);

    glUseProgram(program);
    glUniform2f(glGetUniformLocation(program, "uResolution"), float(width), float(height));
    glUniform1f(glGetUniformLocation(program, "uTime"), time);
    glUniform3f(glGetUniformLocation(program, "uCamPos"), pos.x, pos.y, pos.z);
    glUniform3f(glGetUniformLocation(program, "uCamRight"), right.x, right.y, right.z);
    glUniform3f(glGetUniformLocation(program, "uCamUp"), up.x, up.y, up.z);
    glUniform3f(glGetUniformLocation(program, "uCamForward"), forward.x, forward.y, forward.z);
    glUniform1f(glGetUniformLocation(program, "uFovY"), cam.fovY);
    glUniform1i(glGetUniformLocation(program, "uDisk"), disk ? 1 : 0);
    glUniform1i(glGetUniformLocation(program, "uMaxSteps"), 600);
}

void onMouseButton(GLFWwindow* window, int button, int action, int)
{
    auto* app = static_cast<App*>(glfwGetWindowUserPointer(window));
    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        app->dragging = (action == GLFW_PRESS);
        glfwGetCursorPos(window, &app->lastX, &app->lastY);
    }
}

void onCursorPos(GLFWwindow* window, double x, double y)
{
    auto* app = static_cast<App*>(glfwGetWindowUserPointer(window));
    if (!app->dragging) return;
    app->camera.yaw -= float(x - app->lastX) * 0.005f;
    app->camera.pitch += float(y - app->lastY) * 0.005f;
    app->camera.pitch = std::clamp(app->camera.pitch, -1.5f, 1.5f);
    app->lastX = x;
    app->lastY = y;
}

void onScroll(GLFWwindow* window, double, double dy)
{
    auto* app = static_cast<App*>(glfwGetWindowUserPointer(window));
    app->camera.distance *= std::pow(0.9f, float(dy));
    app->camera.distance = std::clamp(app->camera.distance, 2.5f, 55.0f);
}

void onKey(GLFWwindow* window, int key, int, int action, int)
{
    if (action != GLFW_PRESS) return;
    auto* app = static_cast<App*>(glfwGetWindowUserPointer(window));
    switch (key) {
    case GLFW_KEY_ESCAPE: glfwSetWindowShouldClose(window, GLFW_TRUE); break;
    case GLFW_KEY_D: app->showDisk = !app->showDisk; break;
    case GLFW_KEY_R: app->reloadRequested = true; break;
    default: break;
    }
}

// Rendu d'une image dans un framebuffer hors écran, enregistrée en PPM.
int renderScreenshot(GLuint program, GLuint vao, const App& app, int w, int h, const std::string& path)
{
    GLuint tex = 0, fbo = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        std::cerr << "Framebuffer incomplet\n";
        return EXIT_FAILURE;
    }

    glViewport(0, 0, w, h);
    setUniforms(program, app.camera, w, h, 0.0f, app.showDisk);
    glBindVertexArray(vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    std::vector<unsigned char> pixels(size_t(w) * h * 3);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());

    std::ofstream out(path, std::ios::binary);
    out << "P6\n" << w << " " << h << "\n255\n";
    for (int y = h - 1; y >= 0; --y) // OpenGL lit de bas en haut
        out.write(reinterpret_cast<const char*>(&pixels[size_t(y) * w * 3]), std::streamsize(w) * 3);
    std::cout << "Image enregistrée : " << path << "\n";

    glDeleteFramebuffers(1, &fbo);
    glDeleteTextures(1, &tex);
    return EXIT_SUCCESS;
}

} // namespace

int main(int argc, char** argv)
{
    std::string screenshotPath;
    bool showDisk = true;
    int width = 1280, height = 720;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--screenshot" && i + 1 < argc) screenshotPath = argv[++i];
        else if (arg == "--width" && i + 1 < argc) width = std::atoi(argv[++i]);
        else if (arg == "--height" && i + 1 < argc) height = std::atoi(argv[++i]);
        else if (arg == "--no-disk") showDisk = false;
        else if (arg == "--shaders" && i + 1 < argc) shaderDir = argv[++i];
    }

    if (!glfwInit()) {
        std::cerr << "Échec de l'initialisation de GLFW\n";
        return EXIT_FAILURE;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif
    if (!screenshotPath.empty())
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);

    GLFWwindow* window = glfwCreateWindow(width, height, "Trou noir - ray tracing", nullptr, nullptr);
    if (!window) {
        std::cerr << "Impossible de créer la fenêtre OpenGL 3.3\n";
        glfwTerminate();
        return EXIT_FAILURE;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    if (!gladLoadGL(glfwGetProcAddress)) {
        std::cerr << "Impossible de charger les fonctions OpenGL (glad)\n";
        return EXIT_FAILURE;
    }
    std::cout << "OpenGL " << glGetString(GL_VERSION) << " - " << glGetString(GL_RENDERER) << "\n";

    GLuint program = buildProgram();
    if (!program) {
        glfwTerminate();
        return EXIT_FAILURE;
    }

    // OpenGL core exige un VAO lié, même si le triangle n'a aucun attribut.
    GLuint vao = 0;
    glGenVertexArrays(1, &vao);

    App app;
    app.showDisk = showDisk;

    if (!screenshotPath.empty()) {
        int code = renderScreenshot(program, vao, app, width, height, screenshotPath);
        glfwTerminate();
        return code;
    }

    glfwSetWindowUserPointer(window, &app);
    glfwSetMouseButtonCallback(window, onMouseButton);
    glfwSetCursorPosCallback(window, onCursorPos);
    glfwSetScrollCallback(window, onScroll);
    glfwSetKeyCallback(window, onKey);

    double fpsTimer = glfwGetTime();
    int frames = 0;

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        if (app.reloadRequested) {
            app.reloadRequested = false;
            if (GLuint fresh = buildProgram()) {
                glDeleteProgram(program);
                program = fresh;
                std::cout << "Shaders rechargés\n";
            }
        }

        int fbW, fbH;
        glfwGetFramebufferSize(window, &fbW, &fbH);
        glViewport(0, 0, fbW, fbH);

        setUniforms(program, app.camera, fbW, fbH, float(glfwGetTime()), app.showDisk);
        glBindVertexArray(vao);
        glDrawArrays(GL_TRIANGLES, 0, 3);

        glfwSwapBuffers(window);

        ++frames;
        double now = glfwGetTime();
        if (now - fpsTimer >= 1.0) {
            std::string title = "Trou noir - ray tracing | " + std::to_string(frames) + " FPS";
            glfwSetWindowTitle(window, title.c_str());
            frames = 0;
            fpsTimer = now;
        }
    }

    glDeleteVertexArrays(1, &vao);
    glDeleteProgram(program);
    glfwDestroyWindow(window);
    glfwTerminate();
    return EXIT_SUCCESS;
}
