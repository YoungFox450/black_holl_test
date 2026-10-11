#include "regions.hpp"

#include <algorithm>
#include <cmath>

namespace {
constexpr double kPi = 3.14159265358979;
constexpr double kDeg = kPi / 180.0;
// Règle de Gnevyshev-Waldmeier : l'aire décroît de ~10 MSH par jour
// (MSH : millionième d'hémisphère, 1 MSH = 5e-7 de la surface).
constexpr double kDecayMshPerDay = 10.0;
constexpr double kMsh = 5.0e-7;
}

double ActiveRegionSimulator::uniform()
{
    // xorshift64*
    rng_ ^= rng_ >> 12;
    rng_ ^= rng_ << 25;
    rng_ ^= rng_ >> 27;
    return double((rng_ * 0x2545F4914F6CDD1Dull) >> 11) * (1.0 / 9007199254740992.0);
}

double ActiveRegionSimulator::gaussian()
{
    double u = std::max(uniform(), 1e-12);
    return std::sqrt(-2.0 * std::log(u)) * std::cos(2.0 * kPi * uniform());
}

void ActiveRegionSimulator::clear()
{
    count_ = 0;
    started_ = false;
}

double ActiveRegionSimulator::spotRadius(double area)
{
    return 2.0 * std::asin(std::sqrt(std::clamp(area, 0.0, 1.0)));
}

double ActiveRegionSimulator::areaAt(const ActiveRegion& r, double days) const
{
    double age = days - r.bornDays;
    if (age <= 0.0 || age >= r.lifeDays) return 0.0;
    // Émergence en quelques jours, puis décroissance linéaire.
    if (age < r.growDays) {
        double x = age / r.growDays;
        return r.peakArea * x * (2.0 - x);
    }
    return r.peakArea * (1.0 - (age - r.growDays) / (r.lifeDays - r.growDays));
}

double ActiveRegionSimulator::totalArea(double days) const
{
    double a = 0.0;
    for (int i = 0; i < count_; ++i) a += areaAt(regions_[i], days);
    return a;
}

void ActiveRegionSimulator::center(const ActiveRegion& r, double& lat, double& lon) const
{
    // Moyenne pondérée par les aires (la tête est plus grosse).
    lat = kLeadShare * r.lat[0] + (1.0 - kLeadShare) * r.lat[1];
    lon = kLeadShare * r.lon[0] + (1.0 - kLeadShare) * r.lon[1];
}

double ActiveRegionSimulator::equatorFrameLongitude(double lat, double lon, double days) const
{
    double s2 = std::sin(lat) * std::sin(lat);
    return lon - shear_ * s2 * days;
}

double ActiveRegionSimulator::axisAngle(const ActiveRegion& r, double days) const
{
    double a0 = equatorFrameLongitude(r.lat[0], r.lon[0], days);
    double a1 = equatorFrameLongitude(r.lat[1], r.lon[1], days);
    double cl = std::cos(0.5 * (r.lat[0] + r.lat[1]));
    return std::atan2(r.lat[1] - r.lat[0], (a1 - a0) * cl);
}

int ActiveRegionSimulator::pick(double u, double days) const
{
    double sum = totalArea(days);
    if (sum <= 0.0) return -1;
    double x = u * sum;
    for (int i = 0; i < count_; ++i) {
        x -= areaAt(regions_[i], days);
        if (x <= 0.0) return i;
    }
    return count_ - 1;
}

void ActiveRegionSimulator::spawn(const Star& star, const StellarActivity& act, const CycleState& cyc,
                                  int cycleNumber, double days)
{
    (void)star;
    (void)act;
    if (count_ >= kMax) return;
    ActiveRegion r;
    r.bornDays = days;
    r.seed = float(uniform() * 97.0);

    // Bande du cycle en cours ou du précédent (ils se chevauchent).
    int band = uniform() < cyc.bandWeight[0] ? 0 : 1;
    double sigma = 7.0 * (1.0 + 2.0 * act.polewardShift);
    double latDeg = std::clamp(std::abs(cyc.bandLatDeg[band] + sigma * gaussian()), 0.5, 85.0);
    double hemi = uniform() < 0.5 ? 1.0 : -1.0;
    double lat = hemi * latDeg * kDeg;

    // Aire au maximum : log-normale autour de la moyenne qui donne la bonne
    // fraction tachée (Bogdan et al. 1988 : écart-type ~0,8 en ln A pour
    // les groupes visibles).
    const double s = 0.8;
    r.peakArea = std::min(meanArea_ * std::exp(s * gaussian() - 0.5 * s * s), 0.12);

    // Vie : A_max / W (Gnevyshev-Waldmeier). Sur le Soleil, même les plus
    // grands groupes ne passent pas 4 à 5 rotations ; les taches des
    // étoiles très actives durent des mois, voire des années.
    r.lifeDays = std::clamp(r.peakArea / kMsh / kDecayMshPerDay, 6.0, maxLife_);
    r.growDays = std::clamp(0.1 * r.lifeDays, 1.0, 10.0);

    // Loi de Joy et écart entre les taches (~4 rayons de la tache de tête).
    r.tilt = 32.1 * kDeg * std::sin(std::abs(lat));
    double rLead = spotRadius(kLeadShare * r.peakArea);
    double sep = std::min(4.0 * rLead + 1.5 * kDeg, 0.6);
    double dEast = 0.5 * sep * std::cos(r.tilt);
    double dEq = 0.5 * sep * std::sin(r.tilt);    // vers l'équateur
    double lon = 2.0 * kPi * uniform();
    double cl = std::max(std::cos(lat), 0.2);
    // Tête : vers l'ouest (sens de la rotation, longitude croissante) et
    // vers l'équateur ; queue : à l'opposé.
    r.lat[0] = lat - hemi * dEq;
    r.lat[1] = lat + hemi * dEq;
    r.lon[0] = lon + dEast / cl;
    r.lon[1] = lon - dEast / cl;
    // On garde la convention des éruptions : la longitude « à t = 0 »,
    // chaque tache tournant à la vitesse de sa latitude.
    for (int k = 0; k < 2; ++k) {
        double s2 = std::sin(r.lat[k]) * std::sin(r.lat[k]);
        r.lon[k] -= (omega_ - shear_ * s2) * days;
    }

    // Loi de Hale : polarité de tête + au nord pendant les cycles pairs,
    // - au sud, et l'inverse aux cycles impairs.
    int cycle = cycleNumber - band;
    int north = (cycle % 2 == 0) ? 1 : -1;
    r.leadingPolarity = hemi > 0.0 ? north : -north;

    regions_[count_++] = r;
    ++total_;
}

void ActiveRegionSimulator::update(const Star& star, const StellarActivity& act, const CycleState& cyc,
                                   int cycleNumber, double days)
{
    if (!act.active()) {
        count_ = 0;
        target_ = 0;
        started_ = false;
        return;
    }
    omega_ = displayOmega(star);
    maxLife_ = 120.0 * (1.0 + 3.0 * act.polewardShift);
    shear_ = act.shearRatio * omega_;
    const bool restart = !started_ || days < lastDays_ || days - lastDays_ > 50.0;
    const double dt = restart ? 0.0 : days - lastDays_;

    // Nombre de groupes visibles et aire moyenne, pour que l'aire tachée
    // moyenne vaille f × niveau du cycle (un groupe a en moyenne ~55 % de
    // son aire maximale pendant sa vie).
    double coverage = act.spotCoverage * cyc.level;
    target_ = coverage < 2e-5 ? 0
                              : std::clamp(int(std::lround(1.0 + 3.0 * std::log10(coverage / 2e-5))), 1, kMax);
    meanArea_ = target_ > 0 ? coverage / (0.55 * target_) : 0.0;

    if (restart) {
        count_ = 0;
        // Remplissage immédiat, à des âges au hasard.
        for (int i = 0; i < target_; ++i) {
            spawn(star, act, cyc, cycleNumber, days);
            ActiveRegion& r = regions_[count_ - 1];
            double age = uniform() * r.lifeDays;
            // Recule la naissance sans changer la position affichée.
            r.bornDays -= age;
        }
    } else if (target_ > 0) {
        // Naissances : processus de Poisson qui garde ~target groupes.
        double life = std::clamp(meanArea_ / kMsh / kDecayMshPerDay, 6.0, maxLife_);
        double rate = target_ / life;
        double births = 1.0 - std::exp(-rate * dt);
        if (count_ < kMax && uniform() < births) spawn(star, act, cyc, cycleNumber, days);
    }

    for (int i = 0; i < count_;) {
        if (days - regions_[i].bornDays >= regions_[i].lifeDays) regions_[i] = regions_[--count_];
        else ++i;
    }
    started_ = true;
    lastDays_ = days;
}
