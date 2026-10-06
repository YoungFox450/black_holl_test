#pragma once

// Système double : une étoile compagne en orbite autour du trou noir, dont
// le gaz est arraché par la gravité du trou noir (binaire X, comme
// Cygnus X-1 ou A0620-00).
//
// Unités : rs pour les distances, rs/c pour le temps (comme le ray tracer),
// G = c = 1, donc G M = 0,5 pour le trou noir.
//
//   - L'étoile tourne sur une orbite circulaire de rayon a dans le plan du
//     disque (y = 0), dans le même sens que le gaz du disque.
//     Troisième loi de Kepler : Ω² = G (M + m) / a³.
//   - Lobe de Roche : autour de chaque astre, la région où sa gravité
//     l'emporte. Rayon du lobe de l'étoile (formule d'Eggleton, q = m / M) :
//         rL / a = 0,49 q^(2/3) / (0,6 q^(2/3) + ln(1 + q^(1/3)))
//     Quand l'étoile remplit son lobe, la marée l'étire en goutte vers le
//     trou noir et son gaz s'échappe par le point de Lagrange L1, le col
//     entre les deux lobes.
//   - Le gaz qui passe L1 tombe vers le trou noir : un jet fin qui
//     s'enroule et frappe le bord du disque (point chaud). Chaque paquet de
//     gaz est une particule soumise aux deux gravités : Paczyński-Wiita
//     pour le trou noir (comme les astéroïdes), Newton pour l'étoile, plus
//     le terme dû au mouvement du trou noir autour du centre de masse (on
//     calcule dans le repère du trou noir, qui n'est pas inertiel).
//
// Simplification : les vraies distances sont des millions de fois plus
// grandes (Cygnus X-1 : 0,2 UA entre les deux astres, l'horizon fait 60 km).
// On rapproche l'étoile à quelques dizaines de rs pour voir les deux à la
// fois, en gardant les rapports qui comptent (taille du lobe, position de
// L1, forme du jet de gaz).

#include <cstdint>
#include <vector>

struct BinarySettings {
    bool enabled = false;
    float separation = 30.0f;      // a, distance trou noir - étoile (rs)
    float massRatio = 0.3f;        // q = m étoile / M trou noir
    float fill = 1.0f;             // rayon de l'étoile / rayon de son lobe de Roche
    float temperature = 4300.0f;   // K
    float brightness = 1.0f;
    float streamRate = 1.0f;       // débit du jet de gaz (relatif)
    float phase = 0.0f;            // angle de départ de l'orbite (radians)
};

struct GasParcel {
    double pos[3];
    double vel[3];
    float age = 0.0f;
    float heat = 0.0f;   // température du gaz (K)
};

struct Vec3d {
    double x, y, z;
};

class BinarySystem {
public:
    BinarySettings settings;
    std::vector<GasParcel> gas;
    int swallowed = 0;     // paquets de gaz passés sous l'horizon
    int intoDisk = 0;      // paquets arrivés sur le bord du disque

    // Grandeurs dérivées des réglages.
    double omega() const;                  // vitesse angulaire de l'orbite (rad par rs/c)
    double period() const;                 // période (rs/c)
    double rocheLobe() const;              // rayon du lobe de l'étoile (rs)
    double holeLobe() const;               // rayon du lobe du trou noir (rs)
    double radius() const;                 // rayon de l'étoile (rs)
    double stretch() const;                // allongement de marée (1 = sphère)
    double l1Distance() const;             // distance trou noir - L1 (rs)
    bool overflowing() const;              // l'étoile déborde de son lobe
    Vec3d companionPos(double t) const;    // position de l'étoile au temps t
    Vec3d companionVel(double t) const;    // vitesse (fraction de c)

    // Avance la simulation jusqu'au temps t (rs/c). Un saut en arrière ou
    // trop grand (première image, capture d'écran) recalcule un jet déjà
    // établi. diskOuter : bord du disque (0 = pas de disque).
    void update(double t, double diskOuter);
    void clear();

private:
    double time_ = 0.0;
    bool started_ = false;
    double emitDebt_ = 0.0;
    uint32_t seed_ = 4242u;
    double random01();
    double gaussian();
    void advance(double dt, double diskOuter);
    void emit(double count);
};

constexpr size_t kMaxGasParcels = 4000;
