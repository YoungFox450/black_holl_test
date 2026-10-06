#pragma once

// État de l'application partagé entre la boucle principale (main.cpp) et le
// panneau de contrôle (ui.cpp).

#include "activity.hpp"
#include "asteroids.hpp"
#include "corona.hpp"
#include "binary.hpp"
#include "sky_image.hpp"
#include "star.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <string>

struct Vec3 {
    float x, y, z;
};
inline Vec3 operator-(Vec3 a) { return {-a.x, -a.y, -a.z}; }
inline Vec3 cross(Vec3 a, Vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline Vec3 normalize(Vec3 a)
{
    float l = std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z);
    return {a.x / l, a.y / l, a.z / l};
}

constexpr float kMinDistance = 2.5f;   // en rayons de Schwarzschild
constexpr float kMaxDistance = 55.0f;
constexpr float kMaxPitch = 1.5f;      // ~86°, évite le basculement aux pôles

constexpr float kMinScale = 0.25f;
constexpr float kMaxScale = 1.0f;

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
    float autoOrbitSpeed = 0.12f;   // rad/s (orbite automatique)

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
            yaw += autoOrbitSpeed * dt;
        if (pitch > kMaxPitch || pitch < -kMaxPitch) {
            pitch = std::clamp(pitch, -kMaxPitch, kMaxPitch);
            pitchVel = 0.0f;
        }
        // Zoom lissé.
        distance += (targetDistance - distance) * (1.0f - std::exp(-8.0f * dt));
    }
};

// Réglages du disque d'accrétion (envoyés au shader comme uniforms).
struct DiskSettings {
    float innerRadius = 3.0f;     // en rs ; 3 rs = ISCO, dernière orbite stable
    float outerRadius = 12.0f;
    float maxTemperature = 4500.0f; // K
    float brightness = 1.6f;
    int clumpCount = 14;          // amas de gaz chaud (32 au plus)
    bool doppler = true;          // effet Doppler relativiste
    bool gravitationalShift = true; // décalage gravitationnel vers le rouge
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
    float targetFps = 30.0f;    // FPS visé par la résolution automatique
    // Nombre max de pas par rayon : garde-fou. Avec le pas proportionnel à r, un
    // rayon qui s'échappe en prend ~20 et un tour de la sphère de photons ~30.
    int maxSteps = 200;
    float exposure = 1.0f;

    DiskSettings disk;

    // Masse du trou noir en masses solaires. Les calculs sont faits en unités
    // de rs : la masse ne change pas l'image, seulement la conversion vers
    // les kilomètres et les secondes affichée dans le panneau.
    float massSolar = 10.0f;

    bool showUi = true;

    // Mode étoile : au lieu du trou noir, on simule l'étoile `star`.
    bool starMode = false;
    int starIndex = 0;          // dans starPresets(), -1 = séquence principale
    Star star = starPresets()[0];
    // Activité de l'étoile (src/activity.cpp) : éruptions en cours et
    // décalage de phase du cycle magnétique (réglable dans le panneau).
    FlareSimulator flares;
    // Protubérances, éjections de masse coronale et couronne (src/corona.cpp).
    CoronaSimulator corona;
    bool showCorona = true;
    bool showProminences = true;
    bool showCmes = true;
    double cyclePhaseOffset = 0.4;  // 0,4 : près du maximum du cycle

    // Jets relativistes le long de l'axe du trou noir (quasar).
    bool jets = false;
    float jetPower = 1.0f;
    float jetBeta = 0.8f;       // vitesse du plasma (fraction de c)
    float jetWidth = 1.0f;      // largeur relative des jets

    // Réglages de la simulation (panneau, onglets Vue et Rendu).
    bool lensing = true;        // déviation de la lumière par la gravité
    float skyBrightness = 1.0f; // luminosité du fond de galaxie
    float asteroidScale = 1.0f; // taille d'affichage des astéroïdes
    float mouseSensitivity = 1.0f;
    int stepRequests = 0;       // pas de temps à faire pendant la pause (touche T)

    // Message bref affiché en bas de l'écran après une touche ("Jets : oui").
    std::string toast;
    float toastAge = 1.0e9f;    // secondes depuis le message

    // Astéroïdes autour du corps central (trou noir ou étoile).
    AsteroidSystem asteroids;
    FieldSettings field;
    bool showAsteroids = true;

    // Système double : étoile compagne et gaz arraché (src/binary.*).
    // mutable : le gaz avance au moment du rendu, jusqu'au temps de l'image.
    mutable BinarySystem binary;

    // Fond de ciel : vraie image de la Voie lactée ou ciel procédural (src/sky_image.*).
    SkySettings sky;
};

// Affiche un message bref en bas de l'écran (retour visuel des touches).
inline void notify(App& app, std::string text)
{
    app.toast = std::move(text);
    app.toastAge = 0.0f;
}

inline const char* onOff(bool v) { return v ? "oui" : "non"; }

// Temps de l'étoile en jours (1 s affichée = kStarDaysPerSecond jours).
inline double starDays(const App& app)
{
    return app.simTime / 10.0 * kStarDaysPerSecond;
}

// Vitesse de rotation affichée de l'étoile (rad/jour), bornée pour les
// objets qui tournent très vite (sinon la rotation crénelle à l'écran).
inline double displayOmega(const Star& star)
{
    return std::min(6.283185307 / std::max(star.rotationDays, 1e-9), 1.5);
}

// Angle de rotation affiché de l'équateur au temps `days`.
inline double starRotationAngle(const Star& star, double days)
{
    return std::fmod(displayOmega(star) * days, 6.283185307179586);
}

// Phase (0..1) du cycle magnétique de l'étoile.
inline double cyclePhase(const App& app, const StellarActivity& act)
{
    if (act.cycleYears <= 0.0) return app.cyclePhaseOffset;
    double p = starDays(app) / (act.cycleYears * 365.25) + app.cyclePhaseOffset;
    return p - std::floor(p);
}

// Le corps central actuel, vu par les astéroïdes.
inline CentralBody centralBody(const App& app)
{
    return app.starMode ? starBody(app.star) : blackHoleBody(app.massSolar);
}

// Temps de la scène écoulé pour dt secondes réelles : rs/c pour le trou
// noir, secondes affichées (simTime / 10) pour une étoile.
inline double sceneDt(const App& app, double dt)
{
    return dt * app.simSpeed / (app.starMode ? 10.0 : 1.0);
}

// Champ d'astéroïdes par défaut, adapté à la scène (en rs ou en rayons
// d'étoile).
inline FieldSettings defaultField(bool starMode)
{
    FieldSettings f;
    if (starMode) {
        // Autour du Soleil, la roche fond en dessous de ~7 rayons : le
        // champ commence plus près pour qu'on voie les deux.
        f.innerRadius = 4.0f;
        f.outerRadius = 8.0f;
    }
    return f;
}

// Distance de caméra par défaut : en rayons de Schwarzschild pour le trou
// noir, en rayons de l'étoile pour une étoile.
constexpr float kBlackHoleDistance = 22.0f;
constexpr float kStarDistance = 4.0f;

inline void setStarMode(App& app, bool on)
{
    if (app.starMode == on) return;
    app.starMode = on;
    app.camera.targetDistance = on ? kStarDistance : kBlackHoleDistance;
    // Les unités changent (rs ou rayon d'étoile) : on repart de zéro.
    app.asteroids.clear();
    app.field = defaultField(on);
}

// Recentre la caméra (distance adaptée au trou noir ou à l'étoile).
inline void resetCamera(App& app)
{
    bool autoOrbit = app.camera.autoOrbit;
    float autoOrbitSpeed = app.camera.autoOrbitSpeed;
    app.camera = OrbitCamera{};
    app.camera.autoOrbit = autoOrbit;
    app.camera.autoOrbitSpeed = autoOrbitSpeed;
    if (app.starMode)
        app.camera.distance = app.camera.targetDistance = kStarDistance;
}

inline void selectPreset(App& app, int index)
{
    const auto& presets = starPresets();
    int n = int(presets.size());
    app.starIndex = ((index % n) + n) % n;
    app.star = presets[app.starIndex];
    app.flares.clear();
    app.corona.clear();
    std::cout << app.star.summary() << "\n";
}

inline void selectMainSequence(App& app, double mass)
{
    app.starIndex = -1;
    app.star = mainSequenceStar(mass);
    app.flares.clear();
    app.corona.clear();
    std::cout << app.star.summary() << "\n";
}

// Quasar : trou noir supermassif (~1 milliard de soleils, comme 3C 273) qui
// avale énormément de gaz. Disque très chaud et très lumineux, jets
// relativistes le long de l'axe.
inline void applyQuasar(App& app)
{
    setStarMode(app, false);
    app.massSolar = 8.9e8f;
    app.disk.maxTemperature = 22000.0f;
    app.disk.brightness = 2.8f;
    app.disk.outerRadius = 18.0f;
    app.disk.clumpCount = 32;
    app.showDisk = true;
    app.jets = true;
    app.jetPower = 1.0f;
    app.camera.targetDistance = 35.0f;
    app.camera.pitch = 0.35f;
}

// Dans un système double, le disque tient dans le lobe de Roche du trou
// noir (au-delà, le gaz serait attiré par l'étoile) : ~80 % du lobe.
inline void fitDiskToBinary(App& app)
{
    float maxOuter = float(0.8 * app.binary.holeLobe());
    app.disk.outerRadius = std::min(app.disk.outerRadius, std::max(maxOuter, app.disk.innerRadius + 1.0f));
}

// Système double : le trou noir arrache le gaz d'une étoile compagne qui
// remplit son lobe de Roche. Vue de biais pour voir l'étoile, le jet de gaz
// et le disque qu'il alimente.
inline void applyBinary(App& app)
{
    setStarMode(app, false);
    app.binary.settings.enabled = true;
    app.showDisk = true;
    fitDiskToBinary(app);
    app.camera.targetDistance = 50.0f;
    app.camera.pitch = 0.45f;
}

// Trou noir "calme" : réglages de départ.
inline void applyStellarBlackHole(App& app)
{
    setStarMode(app, false);
    app.massSolar = 10.0f;
    app.disk = DiskSettings{};
    app.jets = false;
    app.camera.targetDistance = kBlackHoleDistance;
}
