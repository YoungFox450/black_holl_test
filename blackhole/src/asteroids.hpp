#pragma once

// Astéroïdes en orbite autour du corps central de la scène (trou noir ou
// étoile), calculés sur le processeur.
//
// Chaque astéroïde est une particule test : il subit la gravité du corps
// central mais pas celle des autres astéroïdes (ils sont des milliards de
// fois plus légers). Ce qui lui arrive dépend du corps central :
//   - trou noir : potentiel de Paczyński-Wiita Φ = -GM / (r - rs), qui
//     reproduit la dernière orbite stable (3 rs) et la chute sans retour de
//     la relativité générale. Sous l'horizon (r < rs), il est avalé ;
//   - étoile : gravité de Newton (Paczyński-Wiita avec rs/R pour une étoile à
//     neutrons). Il est chauffé par le rayonnement, T = T* sqrt(R / 2d) :
//     au-delà de ~1500 K la roche se vaporise et l'astéroïde fond. S'il
//     touche la surface, il s'écrase ;
//   - pour les deux : sous la limite de Roche, les forces de marée
//     dépassent la gravité propre de l'astéroïde et il se disloque en
//     fragments qui s'étalent le long de l'orbite.
//
// Unités de la scène : rs pour le trou noir (temps en rs/c), rayon de
// l'étoile pour une étoile (temps en secondes affichées).

#include "star.hpp"

#include <cstdint>
#include <vector>

struct Asteroid {
    double pos[3];
    double vel[3];
    float sizeKm = 1.0f;     // diamètre
    float heat = 0.0f;       // température de surface (K), pour l'incandescence
    bool fragment = false;   // morceau d'un astéroïde disloqué (ne se redisloque pas)
};

// Ce qu'il faut savoir du corps central pour faire bouger les astéroïdes.
struct CentralBody {
    bool blackHole = true;
    double gm = 0.5;            // G M en unités de la scène
    double pwRs = 1.0;          // rs du potentiel de Paczyński-Wiita (0 = Newton)
    double surface = 1.0;       // r < surface : avalé (horizon) ou écrasé (étoile)
    double temperature = 0.0;   // T de surface de l'étoile (0 : trou noir)
    double massKg = 0.0;
    double unitMeters = 1.0;    // une unité de la scène en mètres
    double unitSeconds = 1.0;   // une unité de temps de la scène en secondes réelles (trou noir)

    // Distance sous laquelle un astéroïde de densité rho (kg/m³) est disloqué
    // par les marées, en unités de la scène : d = (3 M / (2 π ρ))^(1/3)
    // (limite de Roche d'un corps solide).
    double rocheRadius(double density) const;
    // Vitesse d'une orbite circulaire au rayon r.
    double circularSpeed(double r) const;
    // Période réelle (s) d'une orbite circulaire au rayon r.
    double realPeriod(double r) const;
};

CentralBody blackHoleBody(double massSolar);
CentralBody starBody(const Star& star);

// Champ d'astéroïdes : anneau entre innerRadius et outerRadius.
struct FieldSettings {
    int count = 300;
    float innerRadius = 6.0f;
    float outerRadius = 11.0f;
    float inclination = 4.0f;      // dispersion des inclinaisons (degrés)
    float eccentricity = 0.1f;     // dispersion des vitesses autour de la vitesse circulaire
};

struct AsteroidStats {
    int swallowed = 0;   // passés sous l'horizon
    int impacts = 0;     // écrasés sur l'étoile
    int disrupted = 0;   // disloqués par les marées
    int vaporized = 0;   // fondus par la chaleur de l'étoile
    int ejected = 0;     // partis à l'infini
};

class AsteroidSystem {
public:
    std::vector<Asteroid> items;
    AsteroidStats stats;
    double density = 2500.0;   // kg/m³ (roche ; 7800 pour du fer)

    // Un astéroïde au rayon r, vitesse = speedFactor x vitesse circulaire
    // (1 : orbite circulaire, < 1 : il plonge vers le centre, >= 1,41 : il
    // s'échappe), orbite inclinée de inclinationDeg sur le plan du disque.
    void addOrbit(const CentralBody& body, double r, double speedFactor, double inclinationDeg,
                  double azimuthDeg, float sizeKm);
    void addField(const CentralBody& body, const FieldSettings& field);
    void step(const CentralBody& body, double dt);
    void clear();

private:
    uint32_t seed_ = 12345u;
    double random01();
    double gaussian();
};

constexpr size_t kMaxAsteroids = 8000;
