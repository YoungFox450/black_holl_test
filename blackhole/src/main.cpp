// Moteur de rendu ray tracing d'un trou noir (OpenGL 3.3 + GLFW + glad).
//
// Le CPU ne fait presque rien : il ouvre la fenêtre, gère la caméra et le
// temps de simulation, puis dessine des triangles plein écran. Tout le ray
// tracing se passe dans le fragment shader (shaders/blackhole.frag), un rayon
// par pixel, sur la carte graphique.
//
// Pour tourner sur une carte graphique intégrée (Intel HD/UHD), trois passes :
//   1. au démarrage, le fond de galaxie est calculé une fois dans une cubemap
//      (shaders/sky.frag) : le ray tracer n'a plus qu'à lire une texture ;
//   2. à chaque image, le ray tracing est fait dans une image plus petite que
//      la fenêtre (résolution réglée automatiquement pour tenir ~30 FPS) ;
//   3. present.frag agrandit cette image à la taille de la fenêtre.
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
//   K / L                   : baisser / augmenter la résolution du rendu
//   O                       : résolution automatique (activée au départ)
//   R                       : recharger les shaders (après modification)
//   Échap                   : quitter
//
// Options :
//   --fps N         : FPS visé par la résolution automatique (30 par défaut)
//   --scale S       : résolution fixe, S = fraction de la fenêtre (0.25 à 1)
//   --sky N         : taille d'une face du fond de ciel (1024 par défaut)
//   --screenshot image.ppm [--width W --height H]
//                [--no-disk] [--time T] [--yaw A] [--pitch A] [--distance D]
//                   : rend une seule image hors écran puis quitte
//   --bench N       : rend N images hors écran et affiche le temps moyen

#include <glad/gl.h>
#include <GLFW/glfw3.h>

#include "shader.hpp"

#include <algorithm>
#include <chrono>
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

constexpr float kMinScale = 0.25f;
constexpr float kMaxScale = 1.0f;
// Nombre max de pas par rayon : garde-fou. Avec le pas proportionnel à r, un
// rayon qui s'échappe en prend ~20 et un tour de la sphère de photons ~30.
constexpr int kMaxSteps = 200;

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

    float renderScale = 0.5f;   // fraction de la taille de la fenêtre
    bool autoScale = true;
};

std::string shaderDir = BH_SHADER_DIR;

// Les trois programmes et leurs uniforms (cherchés une seule fois).
struct Programs {
    GLuint sky = 0, trace = 0, present = 0;

    GLint skyFace = -1, skyFaceSize = -1;
    GLint resolution = -1, time = -1, camPos = -1, camRight = -1, camUp = -1,
          camForward = -1, fovY = -1, disk = -1, maxSteps = -1, skyTex = -1;
    GLint image = -1, outputSize = -1;

    void destroy()
    {
        glDeleteProgram(sky);
        glDeleteProgram(trace);
        glDeleteProgram(present);
        sky = trace = present = 0;
    }
};

bool buildPrograms(Programs& out)
{
    const std::string vert = shaderDir + "/fullscreen.vert";
    const std::string noise = shaderDir + "/noise.glsl";
    Programs p;
    p.sky = loadShaderProgram({vert}, {noise, shaderDir + "/sky.frag"});
    p.trace = loadShaderProgram({vert}, {noise, shaderDir + "/blackhole.frag"});
    p.present = loadShaderProgram({vert}, {shaderDir + "/present.frag"});
    if (!p.sky || !p.trace || !p.present) {
        p.destroy();
        return false;
    }

    p.skyFace = glGetUniformLocation(p.sky, "uFace");
    p.skyFaceSize = glGetUniformLocation(p.sky, "uFaceSize");

    p.resolution = glGetUniformLocation(p.trace, "uResolution");
    p.time = glGetUniformLocation(p.trace, "uTime");
    p.camPos = glGetUniformLocation(p.trace, "uCamPos");
    p.camRight = glGetUniformLocation(p.trace, "uCamRight");
    p.camUp = glGetUniformLocation(p.trace, "uCamUp");
    p.camForward = glGetUniformLocation(p.trace, "uCamForward");
    p.fovY = glGetUniformLocation(p.trace, "uFovY");
    p.disk = glGetUniformLocation(p.trace, "uDisk");
    p.maxSteps = glGetUniformLocation(p.trace, "uMaxSteps");
    p.skyTex = glGetUniformLocation(p.trace, "uSky");

    p.image = glGetUniformLocation(p.present, "uImage");
    p.outputSize = glGetUniformLocation(p.present, "uOutputSize");

    out = p;
    return true;
}

// Calcule le fond de galaxie une fois pour toutes dans une cubemap.
// R11F_G11F_B10F : couleurs HDR (étoiles > 1) pour 4 octets par texel.
GLuint bakeSky(const Programs& prog, GLuint vao, int size)
{
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_CUBE_MAP, tex);
    for (int face = 0; face < 6; ++face)
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, 0, GL_R11F_G11F_B10F, size, size, 0,
                     GL_RGB, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

    GLuint fbo = 0;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, size, size);
    glUseProgram(prog.sky);
    glUniform1f(prog.skyFaceSize, float(size));
    glBindVertexArray(vao);
    // Une face par appel (et glFinish) : chaque appel reste court, Windows ne
    // croit pas que la carte graphique est bloquée (délai TDR de 2 s).
    for (int face = 0; face < 6; ++face) {
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, tex, 0);
        glUniform1i(prog.skyFace, face);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glFinish();
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &fbo);

    glGenerateMipmap(GL_TEXTURE_CUBE_MAP);
    return tex;
}

// Image (basse résolution) dans laquelle le ray tracer dessine.
struct TraceTarget {
    GLuint tex = 0, fbo = 0;
    int width = 0, height = 0;

    void resize(int w, int h)
    {
        if (w == width && h == height) return;
        width = w;
        height = h;
        if (!tex) {
            glGenTextures(1, &tex);
            glGenFramebuffers(1, &fbo);
        }
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R11F_G11F_B10F, w, h, 0, GL_RGB, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    void destroy()
    {
        glDeleteFramebuffers(1, &fbo);
        glDeleteTextures(1, &tex);
        *this = {};
    }
};

// Taille de l'image de ray tracing pour une fenêtre donnée. L'échelle est
// arrondie au 1/20 pour ne pas réallouer la texture à chaque image.
void traceSize(int outW, int outH, float scale, int& w, int& h)
{
    float s = std::round(scale * 20.0f) / 20.0f;
    w = std::max(1, int(std::lround(outW * s)));
    h = std::max(1, int(std::lround(outH * s)));
}

// Passe 2 : ray tracing dans target.
void traceFrame(const Programs& prog, GLuint vao, GLuint skyTex, const TraceTarget& target,
                const OrbitCamera& cam, float time, bool disk)
{
    Vec3 pos = cam.position();
    Vec3 forward = normalize(-pos);
    Vec3 right = normalize(cross(forward, {0.0f, 1.0f, 0.0f}));
    Vec3 up = cross(right, forward);

    glBindFramebuffer(GL_FRAMEBUFFER, target.fbo);
    glViewport(0, 0, target.width, target.height);
    glUseProgram(prog.trace);
    glUniform2f(prog.resolution, float(target.width), float(target.height));
    glUniform1f(prog.time, time);
    glUniform3f(prog.camPos, pos.x, pos.y, pos.z);
    glUniform3f(prog.camRight, right.x, right.y, right.z);
    glUniform3f(prog.camUp, up.x, up.y, up.z);
    glUniform3f(prog.camForward, forward.x, forward.y, forward.z);
    glUniform1f(prog.fovY, cam.fovY);
    glUniform1i(prog.disk, disk ? 1 : 0);
    glUniform1i(prog.maxSteps, kMaxSteps);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_CUBE_MAP, skyTex);
    glUniform1i(prog.skyTex, 0);
    glBindVertexArray(vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
}

// Passe 3 : agrandissement + tone mapping vers outFbo (0 = la fenêtre).
void presentFrame(const Programs& prog, GLuint vao, const TraceTarget& target, GLuint outFbo,
                  int outW, int outH)
{
    glBindFramebuffer(GL_FRAMEBUFFER, outFbo);
    glViewport(0, 0, outW, outH);
    glUseProgram(prog.present);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, target.tex);
    glUniform1i(prog.image, 0);
    glUniform2f(prog.outputSize, float(outW), float(outH));
    glBindVertexArray(vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
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
    case 'o': case 'O': app->autoScale = !app->autoScale; break;
    case 'k': case 'K':
        app->autoScale = false;
        app->renderScale = std::max(kMinScale, app->renderScale - 0.05f);
        break;
    case 'l': case 'L':
        app->autoScale = false;
        app->renderScale = std::min(kMaxScale, app->renderScale + 0.05f);
        break;
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

bool writePPM(const std::string& path, int w, int h)
{
    std::vector<unsigned char> pixels(size_t(w) * h * 3);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());

    std::ofstream out(path, std::ios::binary);
    if (!out) {
        std::cerr << "Impossible d'écrire " << path << "\n";
        return false;
    }
    out << "P6\n" << w << " " << h << "\n255\n";
    for (int y = h - 1; y >= 0; --y) // OpenGL lit de bas en haut
        out.write(reinterpret_cast<const char*>(&pixels[size_t(y) * w * 3]), std::streamsize(w) * 3);
    std::cout << "Image enregistrée : " << path << "\n";
    return true;
}

// Rendu hors écran : une image enregistrée en PPM (--screenshot), ou N images
// chronométrées (--bench).
int renderOffscreen(const Programs& prog, GLuint vao, GLuint skyTex, const App& app, int w, int h,
                    const std::string& path, int benchFrames)
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

    TraceTarget target;
    int tw, th;
    traceSize(w, h, app.renderScale, tw, th);
    target.resize(tw, th);

    int frames = std::max(1, benchFrames);
    // Une image de chauffe (compilation paresseuse des shaders par le pilote).
    const float t0 = float(app.simTime);
    traceFrame(prog, vao, skyTex, target, app.camera, t0, app.showDisk);
    glFinish();
    auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < frames; ++i) {
        traceFrame(prog, vao, skyTex, target, app.camera, t0 + i * app.simSpeed / 60.0f, app.showDisk);
        presentFrame(prog, vao, target, fbo, w, h);
        glFinish();
    }
    double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();

    int code = EXIT_SUCCESS;
    if (benchFrames > 0) {
        std::cout << "Rendu " << tw << "x" << th << " -> " << w << "x" << h << " : "
                  << ms / frames << " ms par image (" << 1000.0 * frames / ms << " FPS)\n";
    }
    if (!path.empty()) {
        traceFrame(prog, vao, skyTex, target, app.camera, t0, app.showDisk);
        presentFrame(prog, vao, target, fbo, w, h);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        if (!writePPM(path, w, h)) code = EXIT_FAILURE;
    }

    target.destroy();
    glDeleteFramebuffers(1, &fbo);
    glDeleteTextures(1, &tex);
    return code;
}

// Résolution automatique : on mesure le temps GPU du ray tracing avec une
// requête de chronométrage (sans bloquer : le résultat est lu quand il est
// prêt) et on ajuste l'échelle pour tenir le budget. Le coût est
// proportionnel au nombre de pixels, donc à l'échelle au carré.
struct AutoResolution {
    GLuint query = 0;
    bool pending = false;
    double budgetMs = 1000.0 / 30.0;

    void begin()
    {
        if (!query) glGenQueries(1, &query);
        if (!pending) glBeginQuery(GL_TIME_ELAPSED, query);
    }

    void end()
    {
        if (!pending) {
            glEndQuery(GL_TIME_ELAPSED);
            pending = true;
        }
    }

    void update(App& app)
    {
        if (!pending) return;
        GLint ready = 0;
        glGetQueryObjectiv(query, GL_QUERY_RESULT_AVAILABLE, &ready);
        if (!ready) return;
        GLuint64 ns = 0;
        glGetQueryObjectui64v(query, GL_QUERY_RESULT, &ns);
        pending = false;
        if (!app.autoScale) return;

        double ms = std::max(0.1, ns / 1.0e6);
        float ideal = app.renderScale * float(std::sqrt(budgetMs / ms));
        // Lissé pour éviter que la résolution ne "pompe".
        app.renderScale += 0.15f * (ideal - app.renderScale);
        app.renderScale = std::clamp(app.renderScale, kMinScale, kMaxScale);
    }
};

} // namespace

int main(int argc, char** argv)
{
    App app;
    std::string screenshotPath;
    int benchFrames = 0;
    int width = 1280, height = 720;
    float fixedScale = -1.0f;
    double targetFps = 30.0;
    int skySize = 1024;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        bool hasValue = i + 1 < argc;
        if (arg == "--screenshot" && hasValue) screenshotPath = argv[++i];
        else if (arg == "--bench" && hasValue) benchFrames = std::max(1, std::atoi(argv[++i]));
        else if (arg == "--width" && hasValue) width = std::max(16, std::atoi(argv[++i]));
        else if (arg == "--height" && hasValue) height = std::max(16, std::atoi(argv[++i]));
        else if (arg == "--scale" && hasValue) fixedScale = float(std::atof(argv[++i]));
        else if (arg == "--fps" && hasValue) targetFps = std::max(5.0, std::atof(argv[++i]));
        else if (arg == "--sky" && hasValue) skySize = std::clamp(std::atoi(argv[++i]), 128, 4096);
        else if (arg == "--no-disk") app.showDisk = false;
        else if (arg == "--time" && hasValue) app.simTime = std::atof(argv[++i]);
        else if (arg == "--yaw" && hasValue) app.camera.yaw = float(std::atof(argv[++i]));
        else if (arg == "--pitch" && hasValue) app.camera.pitch = float(std::atof(argv[++i]));
        else if (arg == "--distance" && hasValue)
            app.camera.distance = app.camera.targetDistance = float(std::atof(argv[++i]));
        else if (arg == "--shaders" && hasValue) shaderDir = argv[++i];
    }
    const bool offscreen = !screenshotPath.empty() || benchFrames > 0;

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
    if (offscreen)
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
        glfwTerminate();
        return EXIT_FAILURE;
    }
    std::cout << "OpenGL " << glGetString(GL_VERSION) << " - " << glGetString(GL_RENDERER) << "\n";

    // Supprime les coutures entre les faces de la cubemap du ciel.
    glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);

    Programs prog;
    if (!buildPrograms(prog)) {
        glfwTerminate();
        return EXIT_FAILURE;
    }

    // OpenGL core exige un VAO lié, même si le triangle n'a aucun attribut.
    GLuint vao = 0;
    glGenVertexArrays(1, &vao);

    auto bakeStart = std::chrono::steady_clock::now();
    GLuint skyTex = bakeSky(prog, vao, skySize);
    std::cout << "Fond de ciel " << skySize << "x" << skySize << "x6 calculé en "
              << std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - bakeStart).count()
              << " ms\n";

    if (fixedScale > 0.0f) {
        app.renderScale = std::clamp(fixedScale, kMinScale, kMaxScale);
        app.autoScale = false;
    } else if (offscreen) {
        app.renderScale = 1.0f; // capture : pleine résolution par défaut
        app.autoScale = false;
    }

    if (offscreen) {
        int code = renderOffscreen(prog, vao, skyTex, app, width, height, screenshotPath, benchFrames);
        glfwTerminate();
        return code;
    }

    glfwSetWindowUserPointer(window, &app);
    glfwSetMouseButtonCallback(window, onMouseButton);
    glfwSetCursorPosCallback(window, onCursorPos);
    glfwSetScrollCallback(window, onScroll);
    glfwSetKeyCallback(window, onKey);
    glfwSetCharCallback(window, onChar);

    TraceTarget target;
    AutoResolution autoRes;
    // 85 % de la durée d'une image pour le ray tracing, le reste pour
    // l'affichage et la synchronisation.
    autoRes.budgetMs = 0.85 * 1000.0 / targetFps;

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
            Programs fresh;
            if (buildPrograms(fresh)) {
                prog.destroy();
                prog = fresh;
                glDeleteTextures(1, &skyTex);
                skyTex = bakeSky(prog, vao, skySize);
                std::cout << "Shaders rechargés\n";
            }
        }

        handleHeldKeys(window, app, dt);
        app.camera.update(dt, app.dragging);
        if (!app.paused)
            app.simTime += double(dt) * app.simSpeed;

        int fbW, fbH;
        glfwGetFramebufferSize(window, &fbW, &fbH);
        if (fbW <= 0 || fbH <= 0) { // fenêtre réduite : on attend sans rien calculer
            glfwWaitEvents();
            lastFrame = glfwGetTime();
            continue;
        }

        autoRes.update(app);
        int tw, th;
        traceSize(fbW, fbH, app.renderScale, tw, th);
        target.resize(tw, th);

        autoRes.begin();
        traceFrame(prog, vao, skyTex, target, app.camera, float(app.simTime), app.showDisk);
        autoRes.end();
        presentFrame(prog, vao, target, 0, fbW, fbH);

        glfwSwapBuffers(window);

        ++frames;
        if (now - fpsTimer >= 1.0) {
            char title[200];
            std::snprintf(title, sizeof(title),
                          "Trou noir - ray tracing | %d FPS | rendu %dx%d%s | temps x%.2g%s | distance %.1f rs",
                          frames, tw, th, app.autoScale ? " (auto)" : "", app.simSpeed,
                          app.paused ? " (pause)" : "", app.camera.distance);
            glfwSetWindowTitle(window, title);
            frames = 0;
            fpsTimer = now;
        }
    }

    target.destroy();
    glDeleteQueries(1, &autoRes.query);
    glDeleteTextures(1, &skyTex);
    glDeleteVertexArrays(1, &vao);
    prog.destroy();
    glfwDestroyWindow(window);
    glfwTerminate();
    return EXIT_SUCCESS;
}
