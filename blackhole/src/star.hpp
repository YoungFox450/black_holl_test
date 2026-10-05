#pragma once

// Modèles physiques des étoiles.
//
// Une étoile est décrite par sa masse, son rayon et sa température de
// surface. Le reste se déduit par des lois physiques :
//   - luminosité (Stefan-Boltzmann)       L = R² (T / T☉)⁴          [L☉]
//   - gravité de surface                   g = M / R²                [g☉]
//   - compacité (relativité générale)      rs / R = 2GM / (c² R)
//   - type spectral (O B A F G K M)        d'après T
// Pour une étoile de la séquence principale, la masse seule suffit
// (relations masse-luminosité et masse-rayon), et pour une naine blanche
// le rayon vient de la relation masse-rayon de Chandrasekhar (Nauenberg).
//
// Unités : masse en masses solaires, rayon en rayons solaires,
// température en kelvins.

#include <string>
#include <vector>

struct Star {
    std::string name;
    std::string kind;          // "naine rouge", "supergéante bleue"...
    double mass = 1.0;         // M☉
    double radius = 1.0;       // R☉
    double temperature = 5772; // K (température effective)
    double rotationDays = 25;  // période de rotation (jours)
    double activity = 0.3;     // taches et éruptions, 0 = calme, 1 = très active

    double luminosity() const;          // L☉
    double surfaceGravity() const;      // g☉
    double compactness() const;         // rs / R (0 = espace plat)
    std::string spectralClass() const;  // "G2", "M5"...
    double limbDarkening() const;       // coefficient u de I(μ) = 1 - u (1 - μ)
    double granulationScale() const;    // nombre de cellules de convection par rayon
    std::string summary() const;        // une ligne lisible (titre de la fenêtre)
};

// Étoile de la séquence principale de masse donnée (0,08 à 100 M☉).
Star mainSequenceStar(double mass);

// Naine blanche de masse donnée (0,2 à 1,4 M☉) et de température donnée.
Star whiteDwarf(double mass, double temperature);

// Étoiles réelles connues + objets typiques (paramètres mesurés).
const std::vector<Star>& starPresets();
