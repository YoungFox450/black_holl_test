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
//   E                       : passer du trou noir à une étoile (et retour)
//   N / B                   : étoile suivante / précédente (Soleil, Bételgeuse...)
//   I / U                   : étoile de la séquence principale plus / moins massive
//   J                       : jets relativistes (quasar)
//   V                       : lentille gravitationnelle (marche / arrêt)
//   T                       : pendant la pause, avancer d'un pas de temps
//   F / G                   : ajouter un champ d'astéroïdes / un astéroïde
//   X                       : retirer tous les astéroïdes
//   F1 ou Tab               : afficher / cacher le panneau (onglet Touches :
//                             liste complète des touches)
//   Échap                   : quitter
//
// Options :
//   --fps N         : FPS visé par la résolution automatique (30 par défaut)
//   --scale S       : résolution fixe, S = fraction de la fenêtre (0.25 à 1)
//   --sky N         : taille d'une face du fond de ciel (1024 par défaut)
//   --screenshot image.ppm [--width W --height H]
//                [--no-disk] [--no-lensing] [--time T] [--yaw A] [--pitch A] [--distance D]
//                   : rend une seule image hors écran puis quitte
//   --bench N       : rend N images hors écran et affiche le temps moyen
//   --star N        : affiche l'étoile n° N de la liste (0 = Soleil)
//   --mass M        : affiche une étoile de la séquence principale de M masses solaires
//   --quasar        : trou noir supermassif avec disque brillant et jets
//   --binary        : système double, étoile compagne dont le gaz est arraché
//   --bh-mass M     : masse du trou noir (masses solaires)
//   --field N       : ajoute un champ de N astéroïdes
//   --advance T     : fait avancer les astéroïdes de T unités de temps avant l'image

#include <glad/gl.h>
#include <GLFW/glfw3.h>

#include "app.hpp"
#include "binary_gfx.hpp"
#include "shader.hpp"
#include "ui.hpp"
#include "star.hpp"

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

std::string shaderDir = BH_SHADER_DIR;

// Les trois programmes et leurs uniforms (cherchés une seule fois).
struct Programs {
    GLuint sky = 0, trace = 0, present = 0, star = 0, asteroid = 0;

    GLint skyFace = -1, skyFaceSize = -1;
    GLint resolution = -1, time = -1, camPos = -1, camRight = -1, camUp = -1,
          camForward = -1, fovY = -1, disk = -1, maxSteps = -1, skyTex = -1;
    GLint diskIn = -1, diskOut = -1, diskTemp = -1, diskBrightness = -1, clumpCount = -1,
          doppler = -1, gravShift = -1, jets = -1, jetBeta = -1, jetWidth = -1,
          lensing = -1, skyGain = -1;
    GLint image = -1, outputSize = -1, exposure = -1;

    void destroy()
    {
        glDeleteProgram(sky);
        glDeleteProgram(trace);
        glDeleteProgram(present);
        glDeleteProgram(star);
        glDeleteProgram(asteroid);
        sky = trace = present = star = asteroid = 0;
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
    p.star = loadShaderProgram({vert}, {noise, shaderDir + "/star.frag"});
    p.asteroid = loadShaderProgram({shaderDir + "/asteroid.vert"}, {shaderDir + "/asteroid.frag"});
    if (!p.sky || !p.trace || !p.present || !p.star || !p.asteroid) {
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
    p.diskIn = glGetUniformLocation(p.trace, "uDiskIn");
    p.diskOut = glGetUniformLocation(p.trace, "uDiskOut");
    p.diskTemp = glGetUniformLocation(p.trace, "uDiskTemp");
    p.diskBrightness = glGetUniformLocation(p.trace, "uDiskBrightness");
    p.clumpCount = glGetUniformLocation(p.trace, "uClumpCount");
    p.doppler = glGetUniformLocation(p.trace, "uDoppler");
    p.gravShift = glGetUniformLocation(p.trace, "uGravShift");
    p.jets = glGetUniformLocation(p.trace, "uJets");
    p.jetBeta = glGetUniformLocation(p.trace, "uJetBeta");
    p.jetWidth = glGetUniformLocation(p.trace, "uJetWidth");
    p.lensing = glGetUniformLocation(p.trace, "uLensing");
    p.skyGain = glGetUniformLocation(p.trace, "uSkyGain");

    p.image = glGetUniformLocation(p.present, "uImage");
    p.outputSize = glGetUniformLocation(p.present, "uOutputSize");
    p.exposure = glGetUniformLocation(p.present, "uExposure");

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
                const App& app, float time)
{
    const OrbitCamera& cam = app.camera;
    const DiskSettings& disk = app.disk;
    Vec3 pos = cam.position();
    Vec3 forward = normalize(-pos);
    Vec3 right = normalize(cross(forward, {0.0f, 1.0f, 0.0f}));
    Vec3 up = cross(right, forward);

    glBindFramebuffer(GL_FRAMEBUFFER, target.fbo);
    glViewport(0, 0, target.width, target.height);
    glUseProgram(prog.trace);
    setCompanionUniforms(prog.trace, app);
    glUniform2f(prog.resolution, float(target.width), float(target.height));
    glUniform1f(prog.time, time);
    glUniform3f(prog.camPos, pos.x, pos.y, pos.z);
    glUniform3f(prog.camRight, right.x, right.y, right.z);
    glUniform3f(prog.camUp, up.x, up.y, up.z);
    glUniform3f(prog.camForward, forward.x, forward.y, forward.z);
    glUniform1f(prog.fovY, cam.fovY);
    glUniform1i(prog.disk, app.showDisk ? 1 : 0);
    glUniform1i(prog.maxSteps, app.maxSteps);
    glUniform1f(prog.diskIn, disk.innerRadius);
    glUniform1f(prog.diskOut, std::max(disk.outerRadius, disk.innerRadius + 0.5f));
    glUniform1f(prog.diskTemp, disk.maxTemperature);
    glUniform1f(prog.diskBrightness, disk.brightness);
    glUniform1i(prog.clumpCount, disk.clumpCount);
    glUniform1f(prog.doppler, disk.doppler ? 1.0f : 0.0f);
    glUniform1f(prog.gravShift, disk.gravitationalShift ? 1.0f : 0.0f);
    glUniform1f(prog.jets, app.jets ? app.jetPower : 0.0f);
    glUniform1f(prog.jetBeta, std::clamp(app.jetBeta, 0.0f, 0.995f));
    glUniform1f(prog.jetWidth, app.jetWidth);
    glUniform1f(prog.lensing, app.lensing ? 1.0f : 0.0f);
    glUniform1f(prog.skyGain, app.skyBrightness);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_CUBE_MAP, skyTex);
    glUniform1i(prog.skyTex, 0);
    glBindVertexArray(vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
}

// Vitesse de rotation affichée (rad/jour), bornée pour les objets qui
// tournent très vite (sinon la rotation crénelle à l'écran).
double displayOmega(const Star& star)
{
    return std::min(6.283185307 / std::max(star.rotationDays, 1e-9), 1.5);
}

// Position (repère fixe) d'un point de latitude/longitude données à la
// surface de l'étoile, entraîné par la rotation différentielle.
Vec3 surfacePoint(double lat, double lon, double angle)
{
    double a = lon + angle;
    double c = std::cos(lat);
    // Même convention que rotateY() dans star.frag.
    return {float(c * std::cos(a)), float(std::sin(lat)), float(-c * std::sin(a))};
}

// Avance la simulation des éruptions de l'étoile jusqu'au temps simTime.
void updateStarActivity(App& app, double simTime)
{
    StellarActivity act = computeActivity(app.star);
    double days = simTime / 10.0 * kStarDaysPerSecond;
    CycleState cyc = cycleState(act, cyclePhase(app, act));
    app.flares.update(app.star, act, cyc, days);
}

// Passe 2 bis : ray tracing d'une étoile (shaders/star.frag). Les paramètres
// du shader viennent du modèle physique de l'étoile (src/star.cpp) et de
// son activité magnétique (src/activity.cpp).
void traceStarFrame(const Programs& prog, GLuint vao, GLuint skyTex, const TraceTarget& target,
                    const App& app, double simTime)
{
    const OrbitCamera& cam = app.camera;
    const Star& star = app.star;
    Vec3 pos = cam.position();
    Vec3 forward = normalize(-pos);
    Vec3 right = normalize(cross(forward, {0.0f, 1.0f, 0.0f}));
    Vec3 up = cross(right, forward);
    GLuint p = prog.star;
    auto loc = [p](const char* name) { return glGetUniformLocation(p, name); };

    const double seconds = simTime / 10.0;
    const double days = seconds * kStarDaysPerSecond;
    const StellarActivity act = computeActivity(star);
    const CycleState cyc = cycleState(act, cyclePhase(app, act));
    const double kTwoPi = 6.283185307179586;

    // Rotation : angle de l'équateur et cisaillement (rotation différentielle),
    // avec le même facteur de ralenti que la rotation.
    double omega = displayOmega(star);
    double rotation = std::fmod(omega * days, kTwoPi);
    double shear = act.shearRatio * omega; // rad/jour

    // Étoiles à neutrons : 30 tours par seconde (Crabe) seraient un simple
    // flou. Rotation ralentie mais qui garde l'ordre : le pulsar milliseconde
    // tourne plus vite que le Crabe, le magnétar (7,5 s) bien plus lentement.
    const bool compact = star.compact != Compact::None;
    if (compact) {
        double rotSpeed = std::clamp(3.0 * std::pow(0.0337 / star.spinPeriod(), 0.3), 0.3, 6.0);
        rotation = std::fmod(seconds * rotSpeed, kTwoPi);
        shear = 0.0;
    }
    float beam = 0.0f, field = 0.0f;
    if (star.compact == Compact::Pulsar) {
        beam = 1.0f;
        // Lignes de champ à peine visibles, d'autant plus que B est fort.
        field = std::clamp(float((std::log10(std::max(star.magneticField, 1.0)) - 7.0) / 4.0), 0.0f, 0.35f);
    } else if (star.compact == Compact::Magnetar) {
        beam = 0.25f;
        field = 1.0f;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, target.fbo);
    glViewport(0, 0, target.width, target.height);
    glUseProgram(p);
    glUniform2f(loc("uResolution"), float(target.width), float(target.height));
    glUniform1f(loc("uTime"), float(seconds));
    glUniform3f(loc("uCamPos"), pos.x, pos.y, pos.z);
    glUniform3f(loc("uCamRight"), right.x, right.y, right.z);
    glUniform3f(loc("uCamUp"), up.x, up.y, up.z);
    glUniform3f(loc("uCamForward"), forward.x, forward.y, forward.z);
    glUniform1f(loc("uFovY"), cam.fovY);
    glUniform1i(loc("uMaxSteps"), app.maxSteps);
    glUniform1f(loc("uStarTemp"), float(star.temperature));
    glUniform1f(loc("uCompact"), float(star.compactness()));
    glUniform1f(loc("uLensing"), app.lensing ? 1.0f : 0.0f);
    glUniform1f(loc("uSkyGain"), app.skyBrightness);
    glUniform1f(loc("uLimb"), float(star.limbDarkening()));
    glUniform1f(loc("uGranScale"), float(star.granulationScale()));
    glUniform1f(loc("uRotation"), float(rotation));
    glUniform1f(loc("uPulse"), float(oscillation(act, days)));

    // Taches : amplitude de chaque bande telle que la fraction de surface
    // tachée vaille f × niveau du cycle × part de la bande.
    const double deg = kTwoPi / 360.0;
    double sigma = 7.0 * (1.0 + 2.0 * act.polewardShift) * deg;
    float band[4] = {0, 0, 0, 0};
    for (int k = 0; k < 2; ++k) {
        double lat0 = cyc.bandLatDeg[k] * deg;
        double integral = 0.0; // ∫₀^π/2 G(λ) cos λ dλ (les deux hémisphères)
        const int n = 200;
        for (int i = 0; i < n; ++i) {
            double lat = (i + 0.5) / n * kTwoPi / 4.0;
            double x = (lat - lat0) / sigma;
            integral += std::exp(-0.5 * x * x) * std::cos(lat) * (kTwoPi / 4.0) / n;
        }
        band[2 * k] = float(lat0);
        band[2 * k + 1] = float(act.spotCoverage * cyc.level * cyc.bandWeight[k] / integral);
    }
    glUniform1i(loc("uSpotsOn"), act.active() ? 1 : 0);
    glUniform4f(loc("uBand"), band[0], band[1], band[2], band[3]);
    glUniform1f(loc("uBandWidth"), float(sigma));
    glUniform1f(loc("uFaculaRatio"), float(act.active() ? act.faculaCoverage / act.spotCoverage : 0.0));
    glUniform1f(loc("uUmbraRatio"), float(act.active() ? act.umbraTemp / star.temperature : 1.0));
    glUniform1f(loc("uPenumbraRatio"), float(act.active() ? act.penumbraTemp / star.temperature : 1.0));

    // Deux générations de taches décalées d'une demi-vie : poids sin² et cos²
    // (somme 1), chacune cisaillée depuis sa naissance seulement.
    float layers[6] = {0, 0, 0, 0, 0, 0};
    if (act.active()) {
        double life = act.spotLifetimeDays;
        for (int k = 0; k < 2; ++k) {
            double u = days / life + 0.5 * k;
            double gen = std::floor(u);
            double age = u - gen;
            double w = std::sin(age * kTwoPi / 2.0);
            layers[3 * k + 0] = float(w * w);
            layers[3 * k + 1] = float(shear * age * life);
            layers[3 * k + 2] = float(std::fmod(2.0 * gen + k, 64.0));
        }
    }
    glUniform3fv(loc("uSpotLayer"), 2, layers);

    // Éruptions en cours.
    float flarePos[4 * FlareSimulator::kMax] = {};
    float flareAmp[FlareSimulator::kMax] = {};
    int nFlares = 0;
    for (int i = 0; i < app.flares.count(); ++i) {
        const Flare& f = app.flares.flares()[i];
        double s2 = std::sin(f.latitude) * std::sin(f.latitude);
        double angle = std::fmod((omega - shear * s2) * days, kTwoPi);
        Vec3 d = surfacePoint(f.latitude, f.longitude, angle);
        flarePos[4 * nFlares + 0] = d.x;
        flarePos[4 * nFlares + 1] = d.y;
        flarePos[4 * nFlares + 2] = d.z;
        flarePos[4 * nFlares + 3] = float(std::max(2.0 * f.areaFraction, 1e-5));
        flareAmp[nFlares] = float(app.flares.profile(f, days));
        ++nFlares;
    }
    glUniform1i(loc("uFlareCount"), nFlares);
    glUniform4fv(loc("uFlarePos"), FlareSimulator::kMax, flarePos);
    glUniform1fv(loc("uFlareAmp"), FlareSimulator::kMax, flareAmp);

    glUniform1f(loc("uMagTilt"), float(star.magneticTilt * 3.14159265 / 180.0));
    glUniform1f(loc("uBeam"), beam);
    glUniform1f(loc("uField"), field);
    glUniform1f(loc("uBursts"), star.compact == Compact::Magnetar ? 1.0f : 0.0f);
    glUniform1f(loc("uCaps"), compact && star.magneticField > 0.0 ? 1.0f : 0.0f);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_CUBE_MAP, skyTex);
    glUniform1i(loc("uSky"), 0);
    glBindVertexArray(vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
}

// Astéroïdes : positions envoyées à la carte graphique à chaque image et
// dessinées en points par-dessus le ray tracing (shaders/asteroid.*).
struct AsteroidRenderer {
    GLuint vao = 0, vbo = 0;
    std::vector<float> data;

    void draw(const Programs& prog, const TraceTarget& target, const App& app)
    {
        const auto& items = app.asteroids.items;
        if (!app.showAsteroids || items.empty()) return;
        if (!vao) {
            glGenVertexArrays(1, &vao);
            glGenBuffers(1, &vbo);
            glBindVertexArray(vao);
            glBindBuffer(GL_ARRAY_BUFFER, vbo);
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), nullptr);
            glEnableVertexAttribArray(1);
            glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                                  reinterpret_cast<void*>(3 * sizeof(float)));
        }
        data.clear();
        for (const Asteroid& a : items) {
            data.insert(data.end(), {float(a.pos[0]), float(a.pos[1]), float(a.pos[2]),
                                     a.sizeKm, a.heat, a.fragment ? 1.0f : 0.0f});
        }
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(data.size() * sizeof(float)), data.data(), GL_STREAM_DRAW);

        const OrbitCamera& cam = app.camera;
        Vec3 pos = cam.position();
        Vec3 forward = normalize(-pos);
        Vec3 right = normalize(cross(forward, {0.0f, 1.0f, 0.0f}));
        Vec3 up = cross(right, forward);
        GLuint p = prog.asteroid;
        auto loc = [p](const char* name) { return glGetUniformLocation(p, name); };

        // Lumière du corps central : le disque pour le trou noir, la surface
        // pour une étoile (couleur de corps noir approchée, assez pour la roche).
        float lr = 1.0f, lg = 0.85f, lb = 0.7f, scale = 120.0f, ambient = 0.6f;
        if (app.starMode) {
            double t = std::clamp(app.star.temperature, 2000.0, 40000.0);
            lr = float(std::clamp(1.6 - t / 12000.0, 0.6, 1.0));
            lg = float(std::clamp(0.4 + t / 12000.0, 0.5, 0.95));
            lb = float(std::clamp(t / 9000.0, 0.25, 1.0));
            scale = 6.0f;
            ambient = 0.0f;
        } else if (!app.showDisk) {
            scale = 2.0f;   // sans disque, seulement la lueur du ciel
        } else {
            scale *= app.disk.brightness / 1.6f;
        }

        glBindFramebuffer(GL_FRAMEBUFFER, target.fbo);
        glViewport(0, 0, target.width, target.height);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glEnable(GL_PROGRAM_POINT_SIZE);
        glUseProgram(p);
        glUniform3f(loc("uCamPos"), pos.x, pos.y, pos.z);
        glUniform3f(loc("uCamRight"), right.x, right.y, right.z);
        glUniform3f(loc("uCamUp"), up.x, up.y, up.z);
        glUniform3f(loc("uCamForward"), forward.x, forward.y, forward.z);
        glUniform1f(loc("uTanHalf"), std::tan(cam.fovY * 0.5f));
        glUniform1f(loc("uAspect"), float(target.width) / float(target.height));
        glUniform1f(loc("uResY"), float(target.height));
        // Ombre du trou noir : paramètre d'impact critique 3√3/2 rs ≈ 2,6 rs.
        glUniform1f(loc("uOccluder"), app.starMode ? 1.0f : 2.598f);
        glUniform1f(loc("uBlackHole"), app.starMode ? 0.0f : 1.0f);
        glUniform1i(loc("uDisk"), !app.starMode && app.showDisk ? 1 : 0);
        glUniform1f(loc("uDiskIn"), app.disk.innerRadius);
        glUniform1f(loc("uDiskOut"), app.disk.outerRadius);
        glUniform1f(loc("uLightScale"), scale);
        glUniform3f(loc("uLightColor"), lr, lg, lb);
        glUniform1f(loc("uAmbient"), ambient);
        glUniform1f(loc("uSizeScale"), app.asteroidScale);
        glDrawArrays(GL_POINTS, 0, GLsizei(items.size()));
        glDisable(GL_PROGRAM_POINT_SIZE);
        glDisable(GL_BLEND);
    }

    void destroy()
    {
        glDeleteBuffers(1, &vbo);
        glDeleteVertexArrays(1, &vao);
        vao = vbo = 0;
    }
};

AsteroidRenderer asteroidGfx;

// Trou noir ou étoile, selon le mode, puis les astéroïdes.
void traceScene(const Programs& prog, GLuint vao, GLuint skyTex, const TraceTarget& target,
                const App& app, double simTime)
{
    if (app.starMode) {
        traceStarFrame(prog, vao, skyTex, target, app, simTime);
    } else {
        app.binary.update(simTime, app.showDisk ? app.disk.outerRadius : 0.0);
        traceFrame(prog, vao, skyTex, target, app, float(simTime));
    }
    asteroidGfx.draw(prog, target, app);
    if (!app.starMode)
        drawGasStream(shaderDir, target.fbo, target.width, target.height, app);
}

// Passe 3 : agrandissement + tone mapping vers outFbo (0 = la fenêtre).
void presentFrame(const Programs& prog, GLuint vao, const TraceTarget& target, GLuint outFbo,
                  int outW, int outH, float exposure)
{
    glBindFramebuffer(GL_FRAMEBUFFER, outFbo);
    glViewport(0, 0, outW, outH);
    glUseProgram(prog.present);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, target.tex);
    glUniform1i(prog.image, 0);
    glUniform2f(prog.outputSize, float(outW), float(outH));
    glUniform1f(prog.exposure, exposure);
    glBindVertexArray(vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
}

App* appOf(GLFWwindow* window) { return static_cast<App*>(glfwGetWindowUserPointer(window)); }

void onMouseButton(GLFWwindow* window, int button, int action, int)
{
    App* app = appOf(window);
    if (button != GLFW_MOUSE_BUTTON_LEFT) return;
    if (action == GLFW_PRESS && uiWantsMouse()) return;   // clic sur le panneau
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
    const float k = 0.005f * app->mouseSensitivity;
    float dYaw = -float(x - app->lastX) * k;
    float dPitch = float(y - app->lastY) * k;
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
    if (uiWantsMouse()) return;
    zoom(appOf(window)->camera, float(dy));
}

void onKey(GLFWwindow* window, int key, int, int action, int)
{
    if (action != GLFW_PRESS) return;
    App* app = appOf(window);
    if (key == GLFW_KEY_F1 || key == GLFW_KEY_TAB) {
        app->showUi = !app->showUi;
        return;
    }
    if (uiWantsKeyboard()) return;   // saisie dans le panneau
    switch (key) {
    case GLFW_KEY_ESCAPE: glfwSetWindowShouldClose(window, GLFW_TRUE); break;
    case GLFW_KEY_SPACE:
        app->camera.autoOrbit = !app->camera.autoOrbit;
        notify(*app, std::string("Orbite automatique : ") + onOff(app->camera.autoOrbit));
        break;
    default: break;
    }
}

// Les commandes "lettre" passent par les caractères tapés : elles suivent
// la disposition du clavier (AZERTY, QWERTY...).
void onChar(GLFWwindow* window, unsigned int c)
{
    if (uiWantsKeyboard()) return;
    App* app = appOf(window);
    App& a = *app;
    char buf[96];
    switch (c) {
    case 'h': case 'H':
        a.showDisk = !a.showDisk;
        notify(a, std::string("Disque d'accrétion : ") + onOff(a.showDisk));
        break;
    case 'r': case 'R':
        a.reloadRequested = true;
        notify(a, "Shaders rechargés");
        break;
    case 'p': case 'P':
        a.paused = !a.paused;
        notify(a, a.paused ? "Pause (T : avancer d'un pas)" : "Lecture");
        break;
    case 't': case 'T':
        if (a.paused) {
            ++a.stepRequests;
            notify(a, "Un pas de temps");
        } else {
            notify(a, "T avance d'un pas pendant la pause (P)");
        }
        break;
    case 'c': case 'C':
        resetCamera(a);
        notify(a, "Caméra recentrée");
        break;
    case '+': case '-':
        a.simSpeed = c == '+' ? std::min(a.simSpeed * 1.5f, 200.0f) : std::max(a.simSpeed / 1.5f, 0.25f);
        std::snprintf(buf, sizeof(buf), "Vitesse du temps : × %.3g", a.simSpeed);
        notify(a, buf);
        break;
    case 'o': case 'O':
        a.autoScale = !a.autoScale;
        notify(a, std::string("Résolution automatique : ") + onOff(a.autoScale));
        break;
    case 'k': case 'K': case 'l': case 'L':
        a.autoScale = false;
        a.renderScale = (c == 'k' || c == 'K') ? std::max(kMinScale, a.renderScale - 0.05f)
                                               : std::min(kMaxScale, a.renderScale + 0.05f);
        std::snprintf(buf, sizeof(buf), "Résolution : %.2f × fenêtre", a.renderScale);
        notify(a, buf);
        break;
    case 'v': case 'V':
        a.lensing = !a.lensing;
        notify(a, std::string("Lentille gravitationnelle : ") + onOff(a.lensing));
        break;
    case 'e': case 'E':
        setStarMode(a, !a.starMode);
        if (a.starMode) std::cout << a.star.summary() << "\n";
        notify(a, a.starMode ? "Étoile : " + a.star.name : std::string("Trou noir"));
        break;
    case 'n': case 'N': case 'b': case 'B':
        setStarMode(a, true);
        selectPreset(a, a.starIndex + ((c == 'n' || c == 'N') ? 1 : -1));
        notify(a, a.star.name);
        break;
    case 'i': case 'I': case 'u': case 'U':
        setStarMode(a, true);
        selectMainSequence(a, (c == 'i' || c == 'I') ? a.star.mass * 1.25 : a.star.mass / 1.25);
        std::snprintf(buf, sizeof(buf), "Étoile de %.3g masses solaires", a.star.mass);
        notify(a, buf);
        break;
    case 'j': case 'J':
        a.jets = !a.jets;
        notify(a, a.starMode ? std::string("Jets : ") + onOff(a.jets) + " (visibles autour du trou noir, touche E)"
                             : std::string("Jets relativistes : ") + onOff(a.jets));
        break;
    case 'f': case 'F':
        a.asteroids.addField(centralBody(a), a.field);
        std::snprintf(buf, sizeof(buf), "Champ de %d astéroïdes ajouté", a.field.count);
        notify(a, buf);
        break;
    case 'g': case 'G': {
        double r = 0.5 * (a.field.innerRadius + a.field.outerRadius);
        a.asteroids.addOrbit(centralBody(a), r, 1.0, 0.0, 0.0, 5.0f);
        notify(a, "Astéroïde ajouté");
        break;
    }
    case 'x': case 'X':
        a.asteroids.clear();
        notify(a, "Astéroïdes retirés");
        break;
    default: break;
    }
}

// Touches maintenues : rotation et zoom continus.
// ZQSD / WASD sont lues par position physique (GLFW_KEY_W = Z en AZERTY).
void handleHeldKeys(GLFWwindow* window, App& app, float dt)
{
    if (uiWantsKeyboard()) return;
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
int renderOffscreen(const Programs& prog, GLuint vao, GLuint skyTex, App& app, int w, int h,
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
    const double t0 = app.simTime;
    if (app.starMode) {
        // Simule les éruptions des 20 dernières secondes pour l'image.
        for (int i = 200; i >= 0; --i)
            updateStarActivity(app, t0 - i * 1.0);
    }
    traceScene(prog, vao, skyTex, target, app, t0);
    glFinish();
    auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < frames; ++i) {
        traceScene(prog, vao, skyTex, target, app, t0 + i * app.simSpeed / 60.0);
        presentFrame(prog, vao, target, fbo, w, h, app.exposure);
        glFinish();
    }
    double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();

    int code = EXIT_SUCCESS;
    if (benchFrames > 0) {
        std::cout << "Rendu " << tw << "x" << th << " -> " << w << "x" << h << " : "
                  << ms / frames << " ms par image (" << 1000.0 * frames / ms << " FPS)\n";
    }
    if (!path.empty()) {
        traceScene(prog, vao, skyTex, target, app, t0);
        presentFrame(prog, vao, target, fbo, w, h, app.exposure);
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
    float lastMs = 0.0f;   // dernier temps GPU mesuré

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
        lastMs = float(ns / 1.0e6);
        if (!app.autoScale) return;
        double ms = std::max(0.1, ns / 1.0e6);
        // 85 % de la durée d'une image pour le ray tracing, le reste pour
        // l'affichage, le panneau et la synchronisation.
        double budgetMs = 0.85 * 1000.0 / app.targetFps;
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
    int skySize = 1024;
    int fieldCount = 0;
    double advance = 0.0;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        bool hasValue = i + 1 < argc;
        if (arg == "--screenshot" && hasValue) screenshotPath = argv[++i];
        else if (arg == "--bench" && hasValue) benchFrames = std::max(1, std::atoi(argv[++i]));
        else if (arg == "--width" && hasValue) width = std::max(16, std::atoi(argv[++i]));
        else if (arg == "--height" && hasValue) height = std::max(16, std::atoi(argv[++i]));
        else if (arg == "--scale" && hasValue) fixedScale = float(std::atof(argv[++i]));
        else if (arg == "--fps" && hasValue) app.targetFps = float(std::max(5.0, std::atof(argv[++i])));
        else if (arg == "--sky" && hasValue) skySize = std::clamp(std::atoi(argv[++i]), 128, 4096);
        else if (arg == "--no-disk") app.showDisk = false;
        else if (arg == "--no-lensing") app.lensing = false;
        else if (arg == "--time" && hasValue) app.simTime = std::atof(argv[++i]);
        else if (arg == "--yaw" && hasValue) app.camera.yaw = float(std::atof(argv[++i]));
        else if (arg == "--pitch" && hasValue) app.camera.pitch = float(std::atof(argv[++i]));
        else if (arg == "--distance" && hasValue)
            app.camera.distance = app.camera.targetDistance = float(std::atof(argv[++i]));
        else if (arg == "--shaders" && hasValue) shaderDir = argv[++i];
        else if (arg == "--star" && hasValue) { setStarMode(app, true); selectPreset(app, std::atoi(argv[++i])); }
        else if (arg == "--mass" && hasValue) { setStarMode(app, true); selectMainSequence(app, std::atof(argv[++i])); }
        else if (arg == "--quasar") applyQuasar(app);
        else if (arg == "--binary") applyBinary(app);
        else if (arg == "--bh-mass" && hasValue) app.massSolar = float(std::atof(argv[++i]));
        else if (arg == "--field" && hasValue) fieldCount = std::max(0, std::atoi(argv[++i]));
        else if (arg == "--advance" && hasValue) advance = std::atof(argv[++i]);
    }
    if (app.starMode || app.jets || app.binary.settings.enabled) app.camera.distance = app.camera.targetDistance;
    if (fieldCount > 0) {
        app.field.count = fieldCount;
        app.asteroids.addField(centralBody(app), app.field);
    }
    // Avance par pas de 1/60 s à vitesse x10, comme dans la fenêtre.
    for (double t = 0.0; t < advance; t += sceneDt(app, 1.0 / 60.0))
        app.asteroids.step(centralBody(app), sceneDt(app, 1.0 / 60.0));
    if (advance > 0.0) {
        const AsteroidStats& st = app.asteroids.stats;
        std::cout << app.asteroids.items.size() << " astéroïdes | avalés " << st.swallowed << " | écrasés "
                  << st.impacts << " | disloqués " << st.disrupted << " | vaporisés " << st.vaporized
                  << " | éjectés " << st.ejected << "\n";
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
        asteroidGfx.destroy();
        destroyBinaryGfx();
        prog.destroy();
        glfwTerminate();
        return code;
    }

    glfwSetWindowUserPointer(window, &app);
    glfwSetMouseButtonCallback(window, onMouseButton);
    glfwSetCursorPosCallback(window, onCursorPos);
    glfwSetScrollCallback(window, onScroll);
    glfwSetKeyCallback(window, onKey);
    glfwSetCharCallback(window, onChar);
    uiInit(window);   // après nos callbacks : ImGui les enchaîne

    TraceTarget target;
    AutoResolution autoRes;

    double lastFrame = glfwGetTime();
    double fpsTimer = lastFrame;
    int frames = 0;
    UiStats stats;

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
        if (!app.paused) {
            app.simTime += double(dt) * app.simSpeed;
            app.asteroids.step(centralBody(app), sceneDt(app, dt));
        } else if (app.stepRequests > 0) {
            // Pas à pas (touche T) : 1/30 s de simulation à la vitesse choisie.
            for (; app.stepRequests > 0; --app.stepRequests) {
                app.simTime += app.simSpeed / 30.0;
                app.asteroids.step(centralBody(app), sceneDt(app, 1.0 / 30.0));
            }
        }
        if (app.starMode)
            updateStarActivity(app, app.simTime);

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
        traceScene(prog, vao, skyTex, target, app, app.simTime);
        autoRes.end();
        presentFrame(prog, vao, target, 0, fbW, fbH, app.exposure);

        stats.traceWidth = tw;
        stats.traceHeight = th;
        stats.gpuMs = autoRes.lastMs;
        uiDraw(app, stats);

        glfwSwapBuffers(window);

        ++frames;
        if (now - fpsTimer >= 1.0) {
            char title[320];
            std::snprintf(title, sizeof(title),
                          "Trou noir - ray tracing | %d FPS | rendu %dx%d%s | temps x%.2g%s | distance %.1f rs",
                          frames, tw, th, app.autoScale ? " (auto)" : "", app.simSpeed,
                          app.paused ? " (pause)" : "", app.camera.distance);
            if (app.starMode)
                std::snprintf(title, sizeof(title), "%s | %d FPS | rendu %dx%d%s", app.star.summary().c_str(),
                              frames, tw, th, app.autoScale ? " (auto)" : "");
            glfwSetWindowTitle(window, title);
            stats.fps = frames;
            frames = 0;
            fpsTimer = now;
        }
    }

    uiShutdown();
    asteroidGfx.destroy();
    destroyBinaryGfx();
    target.destroy();
    glDeleteQueries(1, &autoRes.query);
    glDeleteTextures(1, &skyTex);
    glDeleteVertexArrays(1, &vao);
    prog.destroy();
    glfwDestroyWindow(window);
    glfwTerminate();
    return EXIT_SUCCESS;
}
