// Moteur de rendu ray tracing d'un trou noir (OpenGL 3.3 + GLFW + glad).
//
// Le CPU ne fait presque rien : il ouvre la fenêtre, gère la caméra et le
// temps de simulation, puis dessine un triangle plein écran. Tout le ray
// tracing se passe dans le fragment shader (shaders/blackhole.frag), un rayon
// par pixel, sur la carte graphique.
//
// Commandes (les touches de lettres marchent en AZERTY comme en QWERTY) :
//   clic gauche + glisser   : tourner autour du trou noir (avec inertie)
//   flèches ou ZQSD / WASD  : tourner autour du trou noir
//   molette ou Page↑ / Page↓: zoom
//   Espace                  : orbite automatique de la caméra
//   C                       : recentrer la caméra
//   P                       : pause de la simulation
//   + / -                   : accélérer / ralentir le temps
//   H                       : afficher / cacher le disque d'accrétion
//   R                       : recharger les shaders (après modification)
//   Échap                   : quitter
//
// Option : blackhole --screenshot image.ppm [--width W --height H]
//                    [--no-disk] [--time T] [--yaw A] [--pitch A] [--distance D]
//   rend une seule image hors écran puis quitte (utile pour tester).

#include <glad/gl.h>
#include <GLFW/glfw3.h>

#include "shader.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
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

constexpr float kMinDistance = 2.5f;   // en rayons de Schwarzschild
constexpr float kMaxDistance = 55.0f;
constexpr float kMaxPitch = 1.5f;      // ~86°, évite le basculement aux pôles

// Caméra en orbite autour du trou noir (placé à l'origine).
// Les vitesses donnent un mouvement fluide avec inertie.
struct OrbitCamera {
    float yaw = 0.0f;
    float pitch = 0.09f;      // légèrement au-dessus du disque
    float distance = 22.0f;
    float targetDistance = 22.0f;
    float fovY = 1.0f;        // ~57°

    float yawVel = 0.0f;      // rad/s
    float pitchVel = 0.0f;    // rad/s
    bool autoOrbit = false;

    Vec3 position() const
    {
        return {distance * std::cos(pitch) * std::sin(yaw),
                distance * std::sin(pitch),
                distance * std::cos(pitch) * std::cos(yaw)};
    }

    void update(float dt, bool dragging)
    {
        if (!dragging) {
            yaw += yawVel * dt;
            pitch += pitchVel * dt;
            float damping = std::exp(-3.0f * dt);
            yawVel *= damping;
            pitchVel *= damping;
        }
        if (autoOrbit)
            yaw += 0.12f * dt;
        if (pitch > kMaxPitch || pitch < -kMaxPitch) {
            pitch = std::clamp(pitch, -kMaxPitch, kMaxPitch);
            pitchVel = 0.0f;
        }
        // Zoom lissé.
        distance += (targetDistance - distance) * (1.0f - std::exp(-8.0f * dt));
    }
};

struct App {
    OrbitCamera camera;
    bool dragging = false;
    double lastX = 0.0, lastY = 0.0;
    double lastMoveTime = 0.0;
    bool showDisk = true;
    bool reloadRequested = false;

    // Temps de simulation en unités rs/c. simSpeed = unités par seconde réelle.
    double simTime = 0.0;
    float simSpeed = 10.0f;
    bool paused = false;
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

App* appOf(GLFWwindow* window) { return static_cast<App*>(glfwGetWindowUserPointer(window)); }

void onMouseButton(GLFWwindow* window, int button, int action, int)
{
    App* app = appOf(window);
    if (button != GLFW_MOUSE_BUTTON_LEFT) return;
    double now = glfwGetTime();
    app->dragging = (action == GLFW_PRESS);
    // Au clic, ou si la souris était immobile avant le relâchement : pas d'élan.
    if (app->dragging || now - app->lastMoveTime > 0.1) {
        app->camera.yawVel = 0.0f;
        app->camera.pitchVel = 0.0f;
    }
    glfwGetCursorPos(window, &app->lastX, &app->lastY);
    app->lastMoveTime = now;
}

void onCursorPos(GLFWwindow* window, double x, double y)
{
    App* app = appOf(window);
    if (!app->dragging) return;
    float dYaw = -float(x - app->lastX) * 0.005f;
    float dPitch = float(y - app->lastY) * 0.005f;
    app->camera.yaw += dYaw;
    app->camera.pitch = std::clamp(app->camera.pitch + dPitch, -kMaxPitch, kMaxPitch);

    // On mémorise la vitesse du geste pour l'élan au relâchement.
    double now = glfwGetTime();
    float dt = float(std::max(now - app->lastMoveTime, 1e-3));
    app->camera.yawVel = std::clamp(dYaw / dt, -4.0f, 4.0f);
    app->camera.pitchVel = std::clamp(dPitch / dt, -4.0f, 4.0f);
    app->lastMoveTime = now;
    app->lastX = x;
    app->lastY = y;
}

void zoom(OrbitCamera& cam, float steps)
{
    cam.targetDistance = std::clamp(cam.targetDistance * std::pow(0.88f, steps), kMinDistance, kMaxDistance);
}

void onScroll(GLFWwindow* window, double, double dy)
{
    zoom(appOf(window)->camera, float(dy));
}

void onKey(GLFWwindow* window, int key, int, int action, int)
{
    if (action != GLFW_PRESS) return;
    App* app = appOf(window);
    switch (key) {
    case GLFW_KEY_ESCAPE: glfwSetWindowShouldClose(window, GLFW_TRUE); break;
    case GLFW_KEY_SPACE: app->camera.autoOrbit = !app->camera.autoOrbit; break;
    default: break;
    }
}

// Les commandes "lettre" passent par les caractères tapés : elles suivent
// la disposition du clavier (AZERTY, QWERTY...).
void onChar(GLFWwindow* window, unsigned int c)
{
    App* app = appOf(window);
    switch (c) {
    case 'h': case 'H': app->showDisk = !app->showDisk; break;
    case 'r': case 'R': app->reloadRequested = true; break;
    case 'p': case 'P': app->paused = !app->paused; break;
    case 'c': case 'C': {
        bool autoOrbit = app->camera.autoOrbit;
        app->camera = OrbitCamera{};
        app->camera.autoOrbit = autoOrbit;
        break;
    }
    case '+': app->simSpeed = std::min(app->simSpeed * 1.5f, 200.0f); break;
    case '-': app->simSpeed = std::max(app->simSpeed / 1.5f, 0.25f); break;
    default: break;
    }
}

// Touches maintenues : rotation et zoom continus.
// ZQSD / WASD sont lues par position physique (GLFW_KEY_W = Z en AZERTY).
void handleHeldKeys(GLFWwindow* window, App& app, float dt)
{
    auto down = [&](int k) { return glfwGetKey(window, k) == GLFW_PRESS; };
    const float accel = 6.0f * dt;   // vitesse cible ~1,2 rad/s
    OrbitCamera& cam = app.camera;
    if (down(GLFW_KEY_LEFT)  || down(GLFW_KEY_A)) cam.yawVel   = std::max(cam.yawVel - accel, -1.2f);
    if (down(GLFW_KEY_RIGHT) || down(GLFW_KEY_D)) cam.yawVel   = std::min(cam.yawVel + accel,  1.2f);
    if (down(GLFW_KEY_UP)    || down(GLFW_KEY_W)) cam.pitchVel = std::min(cam.pitchVel + accel, 1.0f);
    if (down(GLFW_KEY_DOWN)  || down(GLFW_KEY_S)) cam.pitchVel = std::max(cam.pitchVel - accel, -1.0f);
    if (down(GLFW_KEY_PAGE_UP))   zoom(cam, 3.0f * dt);
    if (down(GLFW_KEY_PAGE_DOWN)) zoom(cam, -3.0f * dt);
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
    setUniforms(program, app.camera, w, h, float(app.simTime), app.showDisk);
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
    App app;
    std::string screenshotPath;
    int width = 1280, height = 720;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        bool hasValue = i + 1 < argc;
        if (arg == "--screenshot" && hasValue) screenshotPath = argv[++i];
        else if (arg == "--width" && hasValue) width = std::atoi(argv[++i]);
        else if (arg == "--height" && hasValue) height = std::atoi(argv[++i]);
        else if (arg == "--no-disk") app.showDisk = false;
        else if (arg == "--time" && hasValue) app.simTime = std::atof(argv[++i]);
        else if (arg == "--yaw" && hasValue) app.camera.yaw = float(std::atof(argv[++i]));
        else if (arg == "--pitch" && hasValue) app.camera.pitch = float(std::atof(argv[++i]));
        else if (arg == "--distance" && hasValue)
            app.camera.distance = app.camera.targetDistance = float(std::atof(argv[++i]));
        else if (arg == "--shaders" && hasValue) shaderDir = argv[++i];
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
    glfwSetCharCallback(window, onChar);

    double lastFrame = glfwGetTime();
    double fpsTimer = lastFrame;
    int frames = 0;

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        double now = glfwGetTime();
        float dt = float(std::min(now - lastFrame, 0.1)); // borne si la fenêtre a gelé
        lastFrame = now;

        if (app.reloadRequested) {
            app.reloadRequested = false;
            if (GLuint fresh = buildProgram()) {
                glDeleteProgram(program);
                program = fresh;
                std::cout << "Shaders rechargés\n";
            }
        }

        handleHeldKeys(window, app, dt);
        app.camera.update(dt, app.dragging);
        if (!app.paused)
            app.simTime += double(dt) * app.simSpeed;

        int fbW, fbH;
        glfwGetFramebufferSize(window, &fbW, &fbH);
        glViewport(0, 0, fbW, fbH);

        setUniforms(program, app.camera, fbW, fbH, float(app.simTime), app.showDisk);
        glBindVertexArray(vao);
        glDrawArrays(GL_TRIANGLES, 0, 3);

        glfwSwapBuffers(window);

        ++frames;
        if (now - fpsTimer >= 1.0) {
            char title[160];
            std::snprintf(title, sizeof(title),
                          "Trou noir - ray tracing | %d FPS | temps x%.2g%s | distance %.1f rs",
                          frames, app.simSpeed, app.paused ? " (pause)" : "", app.camera.distance);
            glfwSetWindowTitle(window, title);
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
