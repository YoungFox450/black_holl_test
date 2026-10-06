#include "star.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {

constexpr double kSunTemperature = 5772.0;          // K
constexpr double kSunSchwarzschildKm = 2.953;       // 2GM☉/c²
constexpr double kSunRadiusKm = 695700.0;
constexpr double kChandrasekhar = 1.44;             // M☉
constexpr double kNeutronStarRadius = 12.0 / kSunRadiusKm;

} // namespace

double Star::luminosity() const
{
    return radius * radius * std::pow(temperature / kSunTemperature, 4.0);
}

double Star::surfaceGravity() const
{
    return mass / (radius * radius);
}

double Star::compactness() const
{
    return kSunSchwarzschildKm * mass / (kSunRadiusKm * radius);
}

std::string Star::spectralClass() const
{
    // Bornes de température (début de chaque classe) et sous-type 0 à 9.
    struct Range { char letter; double hot, cool; };
    static const Range classes[] = {
        {'O', 50000, 30000}, {'B', 30000, 10000}, {'A', 10000, 7500},
        {'F', 7500, 6000},   {'G', 6000, 5200},   {'K', 5200, 3700},
        {'M', 3700, 2300},
    };
    double t = std::clamp(temperature, 2300.0, 50000.0);
    for (const Range& c : classes) {
        if (t >= c.cool) {
            int sub = int(std::clamp(10.0 * (c.hot - t) / (c.hot - c.cool), 0.0, 9.0));
            return std::string(1, c.letter) + std::to_string(sub);
        }
    }
    return "M9";
}

double Star::limbDarkening() const
{
    // Loi linéaire : u ≈ 0,6 pour le Soleil, plus faible pour les étoiles
    // chaudes (atmosphère plus transparente), plus fort pour les froides.
    return std::clamp(0.95 - temperature / 17000.0, 0.15, 0.9);
}

double Star::granulationScale() const
{
    // La taille des cellules de convection suit l'échelle de hauteur de
    // l'atmosphère, H ∝ T / g : les supergéantes (g minuscule) n'ont que
    // quelques cellules géantes, les naines denses en ont d'innombrables.
    double h = (temperature / kSunTemperature) / std::max(surfaceGravity(), 1e-9);
    return std::clamp(40.0 * std::pow(h, -0.25), 2.5, 90.0);
}

std::string Star::summary() const
{
    char buf[256];
    std::snprintf(buf, sizeof(buf), "%s (%s, %s) | M %.3g M☉ | R %.3g R☉ | T %.0f K | L %.3g L☉",
                  name.c_str(), kind.c_str(), spectralClass().c_str(), mass, radius, temperature,
                  luminosity());
    return buf;
}

Star mainSequenceStar(double mass)
{
    mass = std::clamp(mass, 0.08, 100.0);
    Star s;
    s.name = "Séquence principale";
    s.mass = mass;

    // Relation masse-luminosité (par morceaux, ajustée sur les étoiles réelles).
    double L;
    if (mass < 0.43)      L = 0.23 * std::pow(mass, 2.3);
    else if (mass < 2.0)  L = std::pow(mass, 4.0);
    else if (mass < 55.0) L = 1.4 * std::pow(mass, 3.5);
    else                  L = 32000.0 * mass;

    // Relation masse-rayon.
    s.radius = mass < 1.0 ? std::pow(mass, 0.8) : std::pow(mass, 0.57);

    // Température déduite de Stefan-Boltzmann : L = R² T⁴.
    s.temperature = kSunTemperature * std::pow(L / (s.radius * s.radius), 0.25);

    // Les petites étoiles convectives tournent vite et sont actives,
    // les étoiles chaudes (sans enveloppe convective) n'ont pas de taches.
    s.activity = std::clamp((6500.0 - s.temperature) / 3500.0, 0.0, 1.0);
    s.rotationDays = mass < 1.0 ? 25.0 * mass : 25.0 / (mass * mass);

    if (s.temperature < 3700)      s.kind = "naine rouge";
    else if (s.temperature < 5200) s.kind = "naine orange";
    else if (s.temperature < 6000) s.kind = "naine jaune";
    else if (s.temperature < 10000) s.kind = "étoile blanche";
    else                            s.kind = "étoile bleue";
    return s;
}

Star whiteDwarf(double mass, double temperature)
{
    mass = std::clamp(mass, 0.2, 1.42);
    Star s;
    s.name = "Naine blanche";
    s.kind = "naine blanche";
    s.mass = mass;
    s.temperature = temperature;
    // Relation masse-rayon de Nauenberg (gaz d'électrons dégénérés) :
    // plus une naine blanche est lourde, plus elle est petite.
    double x = mass / kChandrasekhar;
    s.radius = 0.0126 * std::pow(x, -1.0 / 3.0) * std::sqrt(1.0 - std::pow(x, 4.0 / 3.0));
    s.rotationDays = 0.1;
    s.activity = 0.0;
    return s;
}

const std::vector<Star>& starPresets()
{
    static const std::vector<Star> presets = [] {
        std::vector<Star> v;
        //          nom                     type                       M      R        T      rot.   activité
        v.push_back({"Soleil",              "naine jaune",             1.0,   1.0,     5772,  25.4,  0.35});
        v.push_back({"Proxima du Centaure", "naine rouge",             0.122, 0.154,   3042,  83.0,  0.9});
        v.push_back({"Sirius A",            "étoile blanche",          2.06,  1.71,    9940,  5.5,   0.0});
        v.push_back({"Rigel",               "supergéante bleue",       21.0,  78.9,    12100, 25.0,  0.0});
        v.push_back({"Bételgeuse",          "supergéante rouge",       16.5,  764.0,   3600,  36000, 0.2});
        v.push_back({"Aldébaran",           "géante orange",           1.16,  44.2,    3900,  520,   0.1});
        Star wd = whiteDwarf(1.02, 25000);
        wd.name = "Sirius B";
        v.push_back(wd);
        v.push_back({"Étoile à neutrons",   "étoile à neutrons",       1.4,   kNeutronStarRadius, 1.0e6, 1e-5, 0.0});
        return v;
    }();
    return presets;
}
