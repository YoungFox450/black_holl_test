#include "activity.hpp"

#include <algorithm>
#include <cmath>

namespace {

constexpr double kPi = 3.14159265358979;
constexpr double kSunTemperature = 5772.0;

double smoothstep(double a, double b, double x)
{
    double t = std::clamp((x - a) / (b - a), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

// Forme d'un cycle solaire (Hathaway, Wilson & Reichmann 1994) :
// F(x) = x³ / (exp(x²) - 0,71), x = temps / b, b ≈ 0,38 période.
// Montée rapide (maximum vers 40 % du cycle), descente lente.
constexpr double kCycleB = 0.38;

double hathaway(double phase)
{
    if (phase <= 0.0) return 0.0;
    double x = phase / kCycleB;
    return x * x * x / (std::exp(x * x) - 0.71);
}

// Moyenne de F sur un cycle, en comptant la fin du cycle précédent qui
// chevauche le début du suivant : ∫₀² F(φ) dφ.
double hathawayMean()
{
    static const double mean = [] {
        double s = 0.0;
        const int n = 4000;
        for (int i = 0; i < n; ++i) s += hathaway(2.0 * (i + 0.5) / n);
        return s * 2.0 / n;
    }();
    return mean;
}

// Temps de retournement convectif τ_c (jours).
//  - naines : ajustement de Wright et al. (2018), 0,08 à 1,36 M☉ :
//      log τ_c = 2,33 - 1,50 M + 0,31 M²
//  - géantes (gravité faible) : enveloppe convective profonde, modèles
//    d'évolution de Gunn et al. (1998) : τ_c ~ 150 jours.
double turnoverTime(const Star& s)
{
    double m = std::clamp(s.mass, 0.08, 1.36);
    double dwarf = std::pow(10.0, 2.33 - 1.50 * m + 0.31 * m * m);
    double giant = 150.0;
    double logg = std::log10(std::max(s.surfaceGravity(), 1e-12)); // relatif au Soleil
    double w = smoothstep(-0.5, -1.5, logg);                      // 0 = naine, 1 = géante
    return std::exp((1.0 - w) * std::log(dwarf) + w * std::log(giant));
}

} // namespace

StellarActivity computeActivity(const Star& s)
{
    StellarActivity a;
    // Objets dégénérés (naines blanches, étoiles à neutrons) : pas de dynamo
    // convective ni d'oscillations de type solaire.
    if (s.radius < 0.02) return a;

    // 1. Enveloppe convective : disparaît au-dessus de ~6 700 K (cassure de
    //    Kraft), là où l'énergie sort par rayonnement.
    a.convective = 1.0 - smoothstep(6200.0, 7000.0, s.temperature);

    // 2. Rotation -> activité (Wright et al. 2011) :
    //    L_X / L_bol = 10^-3,13                    si Ro < 0,13 (saturé)
    //                = 10^-3,13 (Ro / 0,13)^-2,7   sinon
    a.turnoverDays = turnoverTime(s);
    a.rossby = s.rotationDays / a.turnoverDays;
    a.saturated = a.rossby < 0.13;
    const double rxSat = std::pow(10.0, -3.13);
    double rel = a.saturated ? 1.0 : std::pow(a.rossby / 0.13, -2.7);
    a.xrayRatio = rxSat * rel * a.convective;

    if (a.convective > 0.0) {
        // 3. Taches. Fraction de surface calée sur deux mesures :
        //    Soleil ~0,1 % en moyenne (L_X/L_bol ~ 10^-6,2) et naines M
        //    saturées ~40 % (O'Neal et al. 2004) -> f = 0,4 (L_X / L_X,sat)^0,84.
        a.spotCoverage = std::min(0.4 * std::pow(rel, 0.84) * a.convective, 0.5);
        // Facules : ~10 fois l'aire des taches sur le Soleil calme, autant
        // que les taches sur les étoiles très actives (Shapiro et al. 2014).
        a.faculaCoverage = std::min(0.548 * std::sqrt(a.spotCoverage), 0.6);
        // Écart de température ombre / photosphère : ajustement linéaire des
        // mesures compilées par Berdyugina (2005) (~1 700 K pour le Soleil,
        // ~200 K pour les naines M froides). Pénombre : ~1/4 de cet écart.
        double dT = std::max(200.0, 0.6 * s.temperature - 1780.0);
        a.umbraTemp = s.temperature - dT;
        a.penumbraTemp = s.temperature - 0.25 * dT;
        // Durée de vie (règle de Gnevyshev-Waldmeier) : T = A_max / W avec
        // W ≈ 10 MSH/jour, pour une grande tache typique de 300 MSH.
        a.spotLifetimeDays = 300.0 / 10.0;

        // 4. Cycle magnétique. Période proportionnelle à la rotation
        //    (branche « inactive » de Böhm-Vitense 2007, P_cyc ≈ 158 P_rot :
        //    11 ans pour le Soleil). Les étoiles saturées n'ont plus de cycle
        //    net, et leurs taches montent vers les pôles car la force de
        //    Coriolis dévie le flux magnétique (Schüssler & Solanki 1992).
        a.cycleDepth = smoothstep(0.1, 0.4, a.rossby);
        a.cycleYears = a.cycleDepth > 0.0 ? 158.0 * s.rotationDays / 365.25 : 0.0;
        a.polewardShift = std::clamp(-std::log10(std::max(a.rossby, 1e-6)), 0.0, 1.0);

        // 5. Rotation différentielle : ΔΩ = 0,073 rad/j (Soleil, Snodgrass)
        //    × (T / T☉)^8,6 (Collier Cameron 2007) : forte pour les étoiles F,
        //    quasi nulle pour les naines M entièrement convectives.
        double omega = 2.0 * kPi / std::max(s.rotationDays, 1e-9);
        a.shearRadPerDay = 0.073 * std::pow(s.temperature / kSunTemperature, 8.6) * a.convective;
        a.shearRatio = std::min(a.shearRadPerDay / omega, 0.6);
        a.shearRadPerDay = a.shearRatio * omega;

        // 6. Éruptions : la fréquence suit l'émission X (même origine
        //    magnétique), calée sur la naine M saturée GJ 1243 (~12 éruptions
        //    par jour d'énergie > 1 s × L_bol, Hawley et al. 2014).
        a.flaresPerDay = 12.0 * rel * a.convective;

        // 7. Oscillations de type solaire (excitées par la convection) :
        //    ν_max = 3 090 µHz (g / g☉) (T / T☉)^-1/2      (Brown et al. 1991)
        //    Δν    = 135 µHz √(M / R³)                     (densité moyenne)
        //    δL/L  = 4,7 ppm (L / M)^0,8                   (Kjeldsen & Bedding 1995)
        a.numaxMicroHz = 3090.0 * s.surfaceGravity() / std::sqrt(s.temperature / kSunTemperature);
        a.deltaNuMicroHz = 135.0 * std::sqrt(s.mass / (s.radius * s.radius * s.radius));
        a.oscAmplitude = std::min(4.7e-6 * std::pow(s.luminosity() / s.mass, 0.8), 0.3) * a.convective;
    }
    return a;
}

CycleState cycleState(const StellarActivity& a, double phase)
{
    CycleState c;
    phase -= std::floor(phase);
    // Cycle en cours + fin du cycle précédent (ils se chevauchent).
    double fNow = hathaway(phase);
    double fPrev = hathaway(phase + 1.0);
    double sum = fNow + fPrev;
    c.level = (1.0 - a.cycleDepth) + a.cycleDepth * sum / hathawayMean();

    // Loi de Spörer (ajustement de Hathaway 2011) : les taches naissent vers
    // 28° de latitude et descendent vers l'équateur, λ = 28° exp(-t / 90 mois)
    // pour un cycle de 132 mois.
    const double k = 132.0 / 90.0;
    double latNow = 28.0 * std::exp(-k * phase);
    double latPrev = 28.0 * std::exp(-k * (phase + 1.0));
    // Rotateurs rapides : taches repoussées vers les hautes latitudes.
    c.bandLatDeg[0] = latNow + (65.0 - latNow) * a.polewardShift;
    c.bandLatDeg[1] = latPrev + (65.0 - latPrev) * a.polewardShift;
    if (sum > 0.0) {
        c.bandWeight[0] = fNow / sum;
        c.bandWeight[1] = fPrev / sum;
    } else {
        c.bandWeight[0] = 1.0;
    }
    return c;
}

// --- Éruptions ---------------------------------------------------------------

namespace {
constexpr double kFlareTemp = 9000.0;   // K (Kowalski et al. 2013)
}

double FlareSimulator::uniform()
{
    // xorshift64*
    rng_ ^= rng_ >> 12;
    rng_ ^= rng_ << 25;
    rng_ ^= rng_ >> 27;
    return double((rng_ * 0x2545F4914F6CDD1Dull) >> 11) * (1.0 / 9007199254740992.0);
}

void FlareSimulator::clear()
{
    count_ = 0;
    started_ = false;
    hasNext_ = false;
}

double FlareSimulator::profile(const Flare& f, double days) const
{
    // Profil empirique de Davenport et al. (2014), t en largeurs à
    // mi-hauteur depuis le pic : montée polynomiale, double décroissance
    // exponentielle (phase impulsive puis phase graduelle).
    double t = (days - f.startDays) / kStarDaysPerSecond / f.halfSeconds - 1.0;
    if (t < -1.0) return 0.0;
    if (t <= 0.0)
        return std::max(0.0, 1.0 + t * (1.941 + t * (-0.175 + t * (-2.246 - 1.125 * t))));
    return 0.6890 * std::exp(-1.600 * t) + 0.3030 * std::exp(-0.2783 * t);
}

void FlareSimulator::spawn(const Star& star, const StellarActivity& act, const CycleState& cyc,
                           double days)
{
    // Énergie (durée équivalente ED, en secondes) tirée dans la loi de
    // puissance dN/dED ∝ ED^-α entre ED_min et 1000 ED_min.
    double u = uniform();
    double ed = edMin_ * std::pow(1.0 - u * (1.0 - 1e-3), -1.0 / (act.flareAlpha - 1.0));

    // Durée : t½ ∝ E^0,39 (Maehara et al. 2015), ~3 min à ED = 1 s.
    // Affichée au ralenti (0,4 s pour ED = 1 s) en gardant la même loi.
    double halfReal = 180.0 * std::pow(ed, 0.39);
    Flare f;
    f.halfSeconds = 0.4 * std::pow(ed, 0.39);
    f.startDays = days;
    // L_pic = E / (1,827 t½) : 1,827 = intégrale du profil de Davenport.
    f.peakRatio = ed / (1.827 * halfReal);
    // Aire : rayonne comme un corps noir à 9 000 K.
    f.areaFraction = std::min(f.peakRatio * std::pow(star.temperature / kFlareTemp, 4.0), 0.05);

    // Position : dans une bande de taches, longitude au hasard.
    int band = uniform() < cyc.bandWeight[0] ? 0 : 1;
    double sigma = 7.0 * (1.0 + 2.0 * act.polewardShift);
    double g = std::sqrt(-2.0 * std::log(std::max(uniform(), 1e-12))) * std::cos(2.0 * kPi * uniform());
    double lat = std::clamp(cyc.bandLatDeg[band] + sigma * g, 0.0, 89.0) * kPi / 180.0;
    f.latitude = uniform() < 0.5 ? lat : -lat;
    f.longitude = 2.0 * kPi * uniform();

    ++total_;
    if (count_ < kMax) {
        flares_[count_++] = f;
    } else {
        // Plus de place : remplace la plus faible.
        int weakest = 0;
        for (int i = 1; i < kMax; ++i)
            if (flares_[i].peakRatio * profile(flares_[i], days) <
                flares_[weakest].peakRatio * profile(flares_[weakest], days))
                weakest = i;
        flares_[weakest] = f;
    }
}

void FlareSimulator::update(const Star& star, const StellarActivity& act, const CycleState& cyc,
                            double days)
{
    double rate = act.flaresPerDay * cyc.level; // ED > 1 s, par jour
    // Avec α = 2, N(> ED) ∝ 1 / ED : on n'affiche que les éruptions assez
    // grosses pour en voir au plus une toutes les 2 secondes (les petites
    // seraient invisibles et se chevaucheraient).
    edMin_ = std::max(1.0, rate * kStarDaysPerSecond / 0.5);
    double visibleRate = rate / edMin_;

    // Retour en arrière ou saut dans le temps : on repart de zéro.
    if (!started_ || days < lastDays_ || days - lastDays_ > 50.0) {
        count_ = 0;
        hasNext_ = false;
    }
    started_ = true;
    lastDays_ = days;
    if (visibleRate <= 0.0) {
        hasNext_ = false;
    } else {
        if (!hasNext_) nextDays_ = days - std::log(1.0 - uniform()) / visibleRate;
        hasNext_ = true;
        for (int n = 0; nextDays_ <= days && n < 8; ++n) {
            spawn(star, act, cyc, nextDays_);
            nextDays_ -= std::log(1.0 - uniform()) / visibleRate;
        }
        if (nextDays_ <= days) nextDays_ = days - std::log(1.0 - uniform()) / visibleRate;
    }

    // Retire les éruptions éteintes.
    for (int i = 0; i < count_;) {
        const Flare& f = flares_[i];
        bool pastPeak = days > f.startDays + f.halfSeconds * kStarDaysPerSecond;
        if (pastPeak && profile(f, days) < 0.01) flares_[i] = flares_[--count_];
        else ++i;
    }
}

double oscillation(const StellarActivity& a, double days)
{
    if (a.oscAmplitude <= 0.0 || a.numaxMicroHz <= 0.0) return 0.0;
    double secondsPerDisplaySecond = kStarDaysPerSecond * 86400.0;
    double periodDisplay = 1.0 / (a.numaxMicroHz * 1e-6) / secondsPerDisplaySecond;
    // Période plus courte qu'une image : la variation se moyenne à zéro.
    double visible = smoothstep(0.15, 0.5, periodDisplay);
    if (visible <= 0.0) return 0.0;
    double t = days * 86400.0; // secondes réelles
    double v = 0.0;
    // Trois modes radiaux séparés de Δν autour de ν_max : leurs battements
    // donnent la variation irrégulière typique des géantes rouges.
    const double amp[3] = {0.55, 1.0, 0.55};
    const double phase0[3] = {0.0, 2.1, 4.4};
    for (int k = -1; k <= 1; ++k) {
        double nu = (a.numaxMicroHz + k * a.deltaNuMicroHz) * 1e-6;
        if (nu <= 0.0) continue;
        v += amp[k + 1] * std::sin(std::fmod(2.0 * kPi * nu * t, 2.0 * kPi) + phase0[k + 1]);
    }
    return a.oscAmplitude * visible * v / 1.5;
}
