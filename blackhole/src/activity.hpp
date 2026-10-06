#pragma once

// Activité magnétique et oscillations des étoiles, déduites de leurs
// paramètres (masse, rayon, température, rotation) par des lois empiriques
// publiées. Rien n'est réglé « à l'œil » : chaque grandeur vient d'une
// formule, rappelée à côté du code (src/activity.cpp).
//
//   1. Dynamo : seules les étoiles à enveloppe convective (T < ~6 700 K,
//      « cassure de Kraft ») ont des taches, des cycles et des éruptions.
//   2. Rotation -> activité : nombre de Rossby Ro = P_rot / τ_c
//      (τ_c : temps de retournement convectif, Wright et al. 2018), puis
//      relation rotation-activité L_X / L_bol (Wright et al. 2011).
//   3. Taches : fraction de surface, température (Berdyugina 2005),
//      durée de vie (règle de Gnevyshev-Waldmeier), facules.
//   4. Cycle magnétique : période (branche « inactive » de Böhm-Vitense
//      2007), forme du cycle (Hathaway 1994), latitude des taches qui
//      descend vers l'équateur (loi de Spörer, diagramme papillon).
//   5. Rotation différentielle : Ω(λ) = Ω_eq (1 - α sin²λ) (Snodgrass),
//      cisaillement ΔΩ ∝ T^8,6 (Collier Cameron 2007).
//   6. Éruptions : processus de Poisson, énergies en loi de puissance
//      dN/dE ∝ E^-2 (Hawley 2014), profil de Davenport (2014), corps noir
//      à ~9 000 K (Kowalski 2013).
//   7. Oscillations de type solaire : ν_max ∝ g / √T, Δν ∝ √ρ, amplitude
//      ∝ (L/M)^0,8 (Kjeldsen & Bedding 1995).
//
// Le temps des étoiles est compté en JOURS : une seconde de simulation
// affichée vaut kStarDaysPerSecond jours.

#include "star.hpp"

#include <array>
#include <cstdint>

constexpr double kStarDaysPerSecond = 2.0;

struct StellarActivity {
    // Dynamo
    double convective = 0.0;      // 0 = enveloppe radiative, 1 = convective
    double turnoverDays = 0.0;    // τ_c
    double rossby = 0.0;          // Ro = P_rot / τ_c
    bool saturated = false;       // Ro < 0,13 : activité au maximum
    double xrayRatio = 0.0;       // L_X / L_bol

    // Taches
    double spotCoverage = 0.0;    // fraction moyenne de la surface (0..1)
    double faculaCoverage = 0.0;
    double umbraTemp = 0.0;       // K
    double penumbraTemp = 0.0;    // K
    double spotLifetimeDays = 0.0;

    // Cycle
    double cycleYears = 0.0;      // 0 = pas de cycle régulier
    double cycleDepth = 0.0;      // 0 = activité constante, 1 = cycle marqué
    double polewardShift = 0.0;   // 0 = loi de Spörer, 1 = taches polaires

    // Rotation différentielle
    double shearRadPerDay = 0.0;  // ΔΩ = Ω_eq - Ω_pôle
    double shearRatio = 0.0;      // α = ΔΩ / Ω_eq

    // Éruptions (énergie mesurée en « durée équivalente » ED : énergie / L_bol)
    double flaresPerDay = 0.0;    // éruptions d'ED > 1 s, en moyenne sur le cycle
    double flareAlpha = 2.0;

    // Oscillations
    double numaxMicroHz = 0.0;
    double deltaNuMicroHz = 0.0;
    double oscAmplitude = 0.0;    // δL / L

    bool active() const { return spotCoverage > 0.0; }
};

StellarActivity computeActivity(const Star& star);

// État du cycle à une phase donnée (0..1 = un cycle).
struct CycleState {
    double level = 1.0;            // activité / moyenne
    double bandLatDeg[2] = {0, 0}; // latitude des 2 bandes (cycle actuel, précédent)
    double bandWeight[2] = {0, 0}; // part de la surface tachée dans chaque bande
};
CycleState cycleState(const StellarActivity& act, double phase);

// Mesurée sur le bruit fbm(3 octaves) de shaders/noise.glsl : valeur dépassée
// par une fraction 10^-k des points, k = 0 ; 0,5 ; 1 ; ... ; 4.
constexpr std::array<float, 9> kSpotNoiseQuantiles = {
    0.0826f, 0.4808f, 0.5599f, 0.6222f, 0.6624f, 0.6875f, 0.7012f, 0.7118f, 0.7226f};

// Éruptions en cours, simulées sur le CPU (processus de Poisson).
struct Flare {
    double latitude = 0.0, longitude = 0.0; // radians, repère tournant
    double startDays = 0.0;      // début (jours)
    double halfSeconds = 1.0;    // largeur à mi-hauteur affichée (secondes)
    double peakRatio = 0.0;      // L_pic / L_bol
    double areaFraction = 0.0;   // aire / surface de l'étoile
};

class FlareSimulator {
public:
    static constexpr int kMax = 4;

    // Avance la simulation jusqu'au temps `days` (cycle : niveau d'activité).
    void update(const Star& star, const StellarActivity& act, const CycleState& cyc,
                double days);
    void clear();

    // Intensité relative du profil de Davenport (2014) au temps `days`.
    double profile(const Flare& f, double days) const;

    const std::array<Flare, kMax>& flares() const { return flares_; }
    int count() const { return count_; }
    double visibleThresholdSeconds() const { return edMin_; }
    int total() const { return total_; }

private:
    double uniform();
    void spawn(const Star& star, const StellarActivity& act, const CycleState& cyc, double days);

    std::array<Flare, kMax> flares_{};
    int count_ = 0;
    int total_ = 0;
    bool started_ = false;
    double lastDays_ = 0.0;
    bool hasNext_ = false;
    double nextDays_ = 0.0;
    double edMin_ = 1.0;
    std::uint64_t rng_ = 0x9E3779B97F4A7C15ull;
};

// Variation relative de luminosité due aux oscillations (somme de 3 modes
// autour de ν_max). Nulle si la période est trop courte pour être vue.
double oscillation(const StellarActivity& act, double days);
