#include "corona.hpp"

#include <algorithm>
#include <cmath>

namespace {

constexpr double kPi = 3.14159265358979;
constexpr double kSigmaSB = 5.6704e-5;      // erg/cm²/s/K⁴
constexpr double kSunMassLoss = 2.0e-14;    // M☉/an
constexpr double kSunEscapeKms = 617.7;     // vitesse de libération à la surface du Soleil
// F_X que le modèle d'activité donne au Soleil (Ro = 1,8, L_X/L_bol ~ 6e-7) :
// les lois calées sur le Soleil sont écrites relativement à lui.
constexpr double kSunXrayFlux = 3.7e4;      // erg/cm²/s

double smoothstep(double a, double b, double x)
{
    double t = std::clamp((x - a) / (b - a), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

// Vent de Parker isotherme : vitesse à la distance r (en rayons d'étoile)
// pour une couronne à T (K). Solution qui passe le point sonique
// r_c = G M / 2 c_s² :
//   (v/c_s)² - ln (v/c_s)² = 4 ln (r/r_c) + 4 r_c/r - 3   (branche supersonique)
double parkerSpeedKms(const Star& s, double temperatureK, double r)
{
    const double kB = 1.380649e-16, mp = 1.6726e-24;
    double cs2 = kB * temperatureK / (0.6 * mp);                 // cm²/s²
    double gm = 1.32712e26 * s.mass;                             // cm³/s²
    double rStar = s.radius * 6.957e10;                          // cm
    double rc = gm / (2.0 * cs2) / rStar;                        // rayons d'étoile
    rc = std::max(rc, 1.0);    // point sonique sous la surface : on le pose à la base
    double rhs = 4.0 * std::log(r / rc) + 4.0 * rc / r - 3.0;
    // x - ln x = rhs, x = (v/c_s)² > 1 : dichotomie.
    double lo = 1.0, hi = 400.0;
    for (int i = 0; i < 60; ++i) {
        double mid = 0.5 * (lo + hi);
        if (mid - std::log(mid) < rhs) lo = mid;
        else hi = mid;
    }
    return std::sqrt(0.5 * (lo + hi) * cs2) * 1e-5;
}

} // namespace

CoronaModel computeCorona(const Star& s, const StellarActivity& act)
{
    CoronaModel c;
    if (s.radius < 0.02) {
        c.windKind = "aucun (objet dégénéré)";
        return c;
    }
    const double vesc = kSunEscapeKms * std::sqrt(s.mass / s.radius);
    const double L = s.luminosity();
    const bool giant = s.surfaceGravity() < 0.03;

    // « Ligne de partage coronale » (Linsky & Haisch 1979) : les géantes plus
    // froides que ~K3 (Bételgeuse, Aldébaran) n'ont plus de couronne chaude,
    // seulement une chromosphère tiède et un vent lent et massif.
    const bool coolGiant = giant && s.temperature < 4300.0;
    c.active = act.active() && !coolGiant;
    c.xrayFlux = act.xrayRatio * kSigmaSB * std::pow(s.temperature, 4.0);
    if (c.active) c.temperatureMK = 0.11 * std::pow(c.xrayFlux, 0.26);
    else c.xrayFlux = 0.0;

    if (s.temperature > 9000.0 && L > 100.0) {
        // Étoiles chaudes : la lumière pousse le gaz par les raies
        // d'absorption (théorie CAK). Ṁ ~ 1e-6 (L / 1e5 L☉)^1,8 M☉/an,
        // ordre de grandeur de Vink et al. (2000) ; v∞ = 2,6 v_esc (Lamers 1995).
        c.windKind = "poussé par la lumière";
        c.massLoss = 1.0e-6 * std::pow(L / 1.0e5, 1.8);
        c.windKms = 2.6 * vesc;
    } else if (giant && s.temperature < 7000.0) {
        // Géantes froides : loi de Reimers (1975), Ṁ = 4e-13 η L R / M avec
        // η = 0,5. Vent lent, une fraction de la vitesse de libération.
        c.windKind = "vent froid de géante (Reimers)";
        c.massLoss = 2.0e-13 * L * s.radius / s.mass;
        c.windKms = std::max(0.2 * vesc, 8.0);
    } else if (c.active) {
        // Naines à couronne chaude : Ṁ par unité de surface ∝ F_X^1,34 (Wood
        // et al. 2005), jusqu'à F_X ~ 1e6. Au-delà (« ligne de partage des
        // vents »), les étoiles très actives perdent moins de masse que prévu.
        c.windKind = "vent coronal (Parker)";
        double fx = std::min(c.xrayFlux, 1.0e6);
        c.massLoss = kSunMassLoss * s.radius * s.radius * std::pow(fx / kSunXrayFlux, 1.34);
        if (c.xrayFlux > 1.0e6) c.massLoss *= 0.1;
        c.windKms = parkerSpeedKms(s, c.temperatureMK * 1.0e6, 215.0 / s.radius);
    } else {
        c.windKind = "très faible (ni couronne chaude, ni assez de lumière)";
        c.massLoss = 1.0e-6 * std::pow(L / 1.0e5, 1.8);
        c.windKms = 2.6 * vesc;
    }

    // Rayon de corotation : orbite dont la période vaut la rotation de
    // l'étoile, r_co = (G M P² / 4π²)^(1/3).
    double p = s.rotationDays * 86400.0;
    double rco = std::cbrt(1.32712e20 * s.mass * p * p / (4.0 * kPi * kPi));
    c.corotationRadius = rco / (s.radius * 6.957e8);

    if (c.active) {
        // CME : ~0,5 par jour au minimum solaire, ~5 au maximum (Yashiro et al.
        // 2004), en moyenne ~2,5. Pour les autres étoiles, on suit l'activité :
        // ∝ F_X^0,7. Les étoiles très actives retiennent une partie des
        // éjections sous leur champ magnétique (Alvarado-Gómez et al. 2018).
        double rx = c.xrayFlux / kSunXrayFlux;
        c.cmePerDay = 2.5 * std::pow(rx, 0.7) * s.radius * s.radius;
        c.cmeEscape = 1.0 / (1.0 + 0.1 * std::sqrt(rx));
        // Rotateurs rapides : nuages froids piégés vers la corotation (AB Dor,
        // Collier Cameron & Robinson 1989), quand elle est assez proche.
        c.slingshot = c.corotationRadius < 6.0;
    }
    return c;
}

// --- Simulateur ----------------------------------------------------------------

double CoronaSimulator::uniform()
{
    rng_ ^= rng_ >> 12;
    rng_ ^= rng_ << 25;
    rng_ ^= rng_ >> 27;
    return double((rng_ * 0x2545F4914F6CDD1Dull) >> 11) * (1.0 / 9007199254740992.0);
}

double CoronaSimulator::gaussian()
{
    double u = std::max(uniform(), 1e-12), v = uniform();
    return std::sqrt(-2.0 * std::log(u)) * std::cos(2.0 * kPi * v);
}

void CoronaSimulator::clear()
{
    promCount_ = cmeCount_ = 0;
    cmeTotal_ = cmeFailed_ = 0;
    started_ = false;
    nextCme_ = -1.0;
}

double CoronaSimulator::heightAt(const Prominence& p, double days) const
{
    if (p.eruptDays < 0.0 || days < p.eruptDays) return p.height;
    // Disparition brusque : la protubérance monte de plus en plus vite.
    return std::min(p.height * std::exp((days - p.eruptDays) / 1.2), p.height + 0.8);
}

double CoronaSimulator::liftAt(const Prominence& p, double days) const
{
    if (p.eruptDays < 0.0 || days < p.eruptDays) return 0.0;
    return std::min((days - p.eruptDays) / 2.0, 1.0);
}

double CoronaSimulator::fadeAt(const Prominence& p, double days) const
{
    double age = days - p.bornDays;
    double f = smoothstep(0.0, 1.5, age) * (1.0 - smoothstep(p.lifeDays - 2.0, p.lifeDays, age));
    if (p.eruptDays >= 0.0 && days > p.eruptDays) f *= std::exp(-(days - p.eruptDays) / 1.2);
    return f;
}

double CoronaSimulator::cmeRadius(const Cme& c, double days) const
{
    double x = c.displaySpeed * std::max(days - c.startDays, 0.0) / kStarDaysPerSecond;
    if (c.escapes) return 0.1 + x;
    // Retenue par le champ : la bulle plafonne puis retombe.
    return 0.1 + 0.9 * (1.0 - std::exp(-x / 0.9));
}

double CoronaSimulator::cmeBrightness(const Cme& c, double days) const
{
    double sec = (days - c.startDays) / kStarDaysPerSecond;
    if (sec < 0.0) return 0.0;
    double r = cmeRadius(c, days);
    // Le gaz se dilue en s'étalant (∝ 1/r²) ; une bulle retenue s'éteint.
    double b = smoothstep(0.0, 0.3, sec) * 1.4 / (r + 0.4);
    if (!c.escapes) b *= 1.0 - smoothstep(2.0, 4.0, sec);
    else b *= 1.0 - smoothstep(3.5, 6.0, r);
    return b;
}

void CoronaSimulator::spawnProminence(const Star& star, const StellarActivity& act,
                                      const CoronaModel& cor, const CycleState& cyc, double days)
{
    if (promCount_ >= kMaxProminences) return;
    Prominence p;
    p.bornDays = days;
    p.seed = float(uniform() * 100.0);
    p.longitude = 2.0 * kPi * uniform();
    p.orientation = (uniform() - 0.5) * 1.2;   // plutôt alignées est-ouest
    double u = uniform();
    double lat;
    double rot = std::max(star.rotationDays, 0.05);
    if (cor.slingshot && u < 0.6) {
        // Nuage froid tenu près du rayon de corotation, qui tourne avec
        // l'étoile pendant quelques tours avant d'être éjecté.
        p.type = 2;
        lat = 25.0 * gaussian();
        p.height = std::clamp(cor.corotationRadius - 1.0, 0.3, 5.0) * (0.85 + 0.3 * uniform());
        p.halfLength = 0.2 + 0.15 * uniform();       // taille du nuage (rayons)
        p.lifeDays = rot * (4.0 + 8.0 * uniform());
    } else if (u < 0.55) {
        // Protubérance active, basse et courte, au-dessus des taches.
        p.type = 1;
        int band = uniform() < cyc.bandWeight[0] ? 0 : 1;
        lat = cyc.bandLatDeg[band] + 6.0 * gaussian();
        p.height = 0.025 + 0.05 * uniform();
        p.halfLength = 0.04 + 0.08 * uniform();
        p.lifeDays = 3.0 + 8.0 * uniform();
    } else {
        // Protubérance calme : longue et haute, dans les bandes ou vers les
        // pôles (« couronne polaire », qui monte vers les pôles au maximum).
        p.type = 0;
        if (uniform() < 0.35 && act.polewardShift < 0.5)
            lat = 50.0 + 20.0 * uniform();
        else
            lat = cyc.bandLatDeg[0] + 10.0 * gaussian();
        p.height = 0.06 + 0.14 * uniform();
        p.halfLength = 0.10 + 0.25 * uniform();
        p.lifeDays = 15.0 + 45.0 * uniform();
    }
    lat = std::clamp(std::abs(lat), 0.0, 80.0) * kPi / 180.0;
    p.latitude = uniform() < 0.5 ? lat : -lat;
    // Les nuages en corotation finissent éjectés par la force centrifuge
    // (sans grosse CME). Les autres éruptions viennent du tirage des CME
    // (update), pour garder la bonne fréquence d'éjections.
    if (p.type == 2 && uniform() < 0.8) p.eruptDays = days + p.lifeDays * (0.5 + 0.4 * uniform());
    proms_[promCount_++] = p;
}

bool CoronaSimulator::recentCme(double days) const
{
    for (int i = 0; i < cmeCount_; ++i)
        if (days - cmes_[i].startDays < 4.0 * kStarDaysPerSecond) return true;
    return false;
}

void CoronaSimulator::addCme(const Star& star, const StellarActivity& act, double lat, double lon,
                             double days, double rotation)
{
    const CoronaModel cor = computeCorona(star, act);
    Cme c;
    double a = lon + rotation, cl = std::cos(lat);
    // Même convention que surfacePoint() (main.cpp) et rotateY() (star.frag).
    c.dir[0] = cl * std::cos(a);
    c.dir[1] = std::sin(lat);
    c.dir[2] = -cl * std::sin(a);
    c.startDays = days;
    // Vitesses : loi log-normale, médiane ~450 km/s pour le Soleil.
    double rx = std::max(cor.xrayFlux / kSunXrayFlux, 1e-3);
    c.speedKms = 450.0 * std::pow(rx, 0.08) * std::pow(10.0, 0.25 * gaussian());
    c.displaySpeed = std::clamp(0.9 * c.speedKms / 450.0, 0.3, 2.5);
    c.width = 0.35 + 0.35 * uniform();
    c.escapes = uniform() < cor.cmeEscape;
    ++cmeTotal_;
    if (!c.escapes) ++cmeFailed_;
    if (cmeCount_ < kMaxCmes) {
        cmes_[cmeCount_++] = c;
    } else {
        // Remplace la plus ancienne.
        int oldest = 0;
        for (int i = 1; i < kMaxCmes; ++i)
            if (cmes_[i].startDays < cmes_[oldest].startDays) oldest = i;
        cmes_[oldest] = c;
    }
}

void CoronaSimulator::launchCme(const Star& star, const StellarActivity& act, double days,
                                double rotation)
{
    if (!computeCorona(star, act).active) return;
    // La protubérance la plus haute qui n'éclate pas encore part la première.
    int best = -1;
    for (int i = 0; i < promCount_; ++i)
        if (proms_[i].eruptDays < 0.0 && (best < 0 || proms_[i].height > proms_[best].height)) best = i;
    if (best >= 0) {
        proms_[best].eruptDays = days + 1e-3;
        proms_[best].launchesCme = true;
        return;   // la CME part quand l'éruption est lancée (update)
    }
    double lat = (uniform() - 0.5) * 1.0;
    addCme(star, act, lat, 2.0 * kPi * uniform(), days, rotation);
}

void CoronaSimulator::update(const Star& star, const StellarActivity& act, const CycleState& cyc,
                             double days, double rotation)
{
    const CoronaModel cor = computeCorona(star, act);
    if (!cor.active) {
        promCount_ = cmeCount_ = 0;
        started_ = false;
        return;
    }
    const bool restart = !started_ || days < lastDays_ || days - lastDays_ > 50.0;
    const double dt = restart ? 0.0 : days - lastDays_;
    if (restart) {
        promCount_ = cmeCount_ = 0;
        nextCme_ = -1.0;
    }

    // Nombre de protubérances visibles : 1 sur une étoile calme, jusqu'à 6
    // sur une étoile couverte de taches (même échelle logarithmique que
    // l'aire tachée : Soleil au maximum ~3).
    double spotted = std::max(act.spotCoverage * cyc.level, 1e-7);
    double index = std::clamp((std::log10(spotted) + 3.5) / 3.0, 0.0, 1.0);
    int target = 1 + int(std::lround(5.0 * index));
    if (restart) {
        // Remplissage immédiat, avec des âges au hasard.
        for (int i = 0; i < target; ++i) {
            spawnProminence(star, act, cor, cyc, days);
            Prominence& p = proms_[promCount_ - 1];
            double age = uniform() * 0.7 * p.lifeDays;
            p.bornDays -= age;
            if (p.eruptDays >= 0.0) p.eruptDays = std::max(p.eruptDays - age, days + 1.0);
        }
    } else if (promCount_ < target) {
        // Naissances : une à la fois, environ une par durée de vie / cible.
        double rate = target / (cor.slingshot ? 3.0 : 12.0);   // par jour
        if (uniform() < 1.0 - std::exp(-rate * dt)) spawnProminence(star, act, cor, cyc, days);
    }

    // Éruptions de protubérances -> CME, et fin de vie.
    for (int i = 0; i < promCount_;) {
        Prominence& p = proms_[i];
        bool launch = p.eruptDays >= 0.0 && lastDays_ < p.eruptDays && days >= p.eruptDays && !restart;
        // Une CME à la fois à l'écran (au plus une toutes les 4 s affichées) :
        // sinon la protubérance éclate sans qu'on montre la bulle.
        if (launch && p.launchesCme)
            addCme(star, act, p.latitude, p.longitude, p.eruptDays, rotation);
        bool over = days > p.bornDays + p.lifeDays ||
                    (p.eruptDays >= 0.0 && days > p.eruptDays + 10.0);
        if (over) proms_[i] = proms_[--promCount_];
        else ++i;
    }

    // CME sans protubérance (grosses éruptions) : processus de Poisson.
    // L'étoile en produit souvent bien plus qu'on ne peut en montrer : au
    // plus une toutes les 8 secondes affichées.
    double rate = std::min(cor.cmePerDay * cyc.level, 1.0 / (8.0 * kStarDaysPerSecond));
    if (nextCme_ < 0.0) nextCme_ = days - std::log(1.0 - uniform()) / rate;
    if (days >= nextCme_ && !recentCme(days)) {
        // Souvent, c'est une protubérance qui se déstabilise (« disparition
        // brusque ») et devient le cœur de la bulle ; sinon une grosse éruption.
        int pick = -1;
        for (int i = 0; i < promCount_; ++i)
            if (proms_[i].type != 2 && proms_[i].eruptDays < 0.0) { pick = i; break; }
        if (pick >= 0 && uniform() < 0.6) {
            proms_[pick].eruptDays = days + 1e-3;
            proms_[pick].launchesCme = true;
        } else {
            int band = uniform() < cyc.bandWeight[0] ? 0 : 1;
            double lat = (cyc.bandLatDeg[band] + 8.0 * gaussian()) * kPi / 180.0;
            addCme(star, act, uniform() < 0.5 ? lat : -lat, 2.0 * kPi * uniform(), days, rotation);
        }
        nextCme_ = days - std::log(1.0 - uniform()) / rate;
    }

    for (int i = 0; i < cmeCount_;) {
        if (days > cmes_[i].startDays + 1.0 && cmeBrightness(cmes_[i], days) <= 0.0)
            cmes_[i] = cmes_[--cmeCount_];
        else
            ++i;
    }
    started_ = true;
    lastDays_ = days;
}
