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
//
// Étoiles à neutrons : en plus de la masse, deux grandeurs décident de ce
// qu'on voit, la période de rotation P et le champ magnétique B :
//   - magnétar     : B au-dessus du champ critique quantique (4,4e9 T),
//                    magnétosphère tordue, sursauts de rayons X et gamma ;
//   - pulsar       : B / P² au-dessus de la "ligne de mort" (1,7e7 T/s²),
//                    deux faisceaux qui balaient l'espace comme un phare ;
//   - sinon        : étoile à neutrons éteinte (trop lente ou champ trop faible).

#include <string>
#include <vector>

// Objet compact : 0 = étoile ordinaire ou naine blanche.
enum class Compact { None = 0, Neutron, Pulsar, Magnetar };

struct Star {
    std::string name;
    std::string kind;          // "naine rouge", "supergéante bleue"...
    double mass = 1.0;         // M☉
    double radius = 1.0;       // R☉
    double temperature = 5772; // K (température effective)
    double rotationDays = 25;  // période de rotation (jours)
    double activity = 0.3;     // taches et éruptions, 0 = calme, 1 = très active
    Compact compact = Compact::None;
    double magneticField = 0;  // T, au pôle magnétique (étoiles à neutrons)
    double magneticTilt = 0;   // degrés entre l'axe magnétique et l'axe de rotation

    double luminosity() const;          // L☉
    double surfaceGravity() const;      // g☉
    double compactness() const;         // rs / R (0 = espace plat)
    std::string spectralClass() const;  // "G2", "M5"...
    double limbDarkening() const;       // coefficient u de I(μ) = 1 - u (1 - μ)
    double granulationScale() const;    // nombre de cellules de convection par rayon
    std::string summary() const;        // une ligne lisible (titre de la fenêtre)

    // Étoiles à neutrons : modèle du dipôle magnétique tournant.
    double spinPeriod() const;          // s
    double spinDownPower() const;       // W rayonnés par le dipôle tournant
    double periodDerivative() const;    // dP/dt (s/s) : le pulsar ralentit
};

// Étoile de la séquence principale de masse donnée (0,08 à 100 M☉).
Star mainSequenceStar(double mass);

// Naine blanche de masse donnée (0,2 à 1,4 M☉) et de température donnée.
Star whiteDwarf(double mass, double temperature);

// Étoile à neutrons de masse, période (s) et champ magnétique (T) donnés.
// Le type (pulsar, magnétar, éteinte) est déduit de P et B, voir plus haut.
Star neutronStar(double mass, double periodSeconds, double fieldTesla, double tiltDegrees);

// Étoiles réelles connues + objets typiques (paramètres mesurés).
const std::vector<Star>& starPresets();
