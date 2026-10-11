#include "binary.hpp"

#include <algorithm>
#include <cmath>

namespace {

constexpr double kGM = 0.5;   // G M du trou noir (rs = 2 G M / c² = 1)
constexpr double kPi = 3.14159265358979323846;   // M_PI n'existe pas sous MSVC

double length3(const double* v) { return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); }

// Rapport rL / a d'Eggleton (1983), précis à 1 % pour tout q.
double eggleton(double q)
{
    double q13 = std::cbrt(q), q23 = q13 * q13;
    return 0.49 * q23 / (0.6 * q23 + std::log(1.0 + q13));
}

} // namespace

double BinarySystem::omega() const
{
    double a = settings.separation;
    return std::sqrt(kGM * (1.0 + settings.massRatio) / (a * a * a));
}

double BinarySystem::period() const { return 2.0 * kPi / omega(); }

double BinarySystem::rocheLobe() const { return settings.separation * eggleton(settings.massRatio); }

double BinarySystem::holeLobe() const { return settings.separation * eggleton(1.0 / settings.massRatio); }

double BinarySystem::radius() const { return std::min(double(settings.fill), 1.0) * rocheLobe(); }

// L1 : point de la ligne trou noir - étoile où, dans le repère qui tourne
// avec l'orbite, la gravité du trou noir, celle de l'étoile et la force
// centrifuge s'annulent. On le cherche par dichotomie.
double BinarySystem::l1Distance() const
{
    const double a = settings.separation, q = settings.massRatio;
    const double w2 = omega() * omega();
    const double xcm = a * q / (1.0 + q);   // centre de masse, depuis le trou noir
    auto force = [&](double x) {
        return -kGM / (x * x) + kGM * q / ((a - x) * (a - x)) + w2 * (x - xcm);
    };
    double lo = 1e-3 * a, hi = a * (1.0 - 1e-3);
    for (int i = 0; i < 60; ++i) {
        double mid = 0.5 * (lo + hi);
        (force(mid) < 0.0 ? lo : hi) = mid;
    }
    return 0.5 * (lo + hi);
}

// Allongement de marée. L'étoile devient un ellipsoïde de même volume
// (demi-axes : "along" vers le trou noir, "perp" autour) dont la pointe
// atteint L1 quand elle remplit son lobe.
double BinarySystem::stretch() const
{
    double r = radius();
    double k = std::clamp((settings.fill - 0.6) / 0.4, 0.0, 1.0);
    double along = r + (settings.separation - l1Distance() - r) * k * k;
    along = std::max(along, r);
    double perp = std::sqrt(r * r * r / along);
    return along / perp;
}

bool BinarySystem::overflowing() const { return settings.enabled && settings.fill >= 0.96f; }

Vec3d BinarySystem::companionPos(double t) const
{
    double th = settings.phase + omega() * t;
    double a = settings.separation;
    return {a * std::cos(th), 0.0, a * std::sin(th)};
}

Vec3d BinarySystem::companionVel(double t) const
{
    double th = settings.phase + omega() * t;
    double v = settings.separation * omega();
    return {-v * std::sin(th), 0.0, v * std::cos(th)};
}

double BinarySystem::random01()
{
    seed_ = seed_ * 1664525u + 1013904223u;
    return (seed_ >> 8) * (1.0 / 16777216.0);
}

double BinarySystem::gaussian()
{
    double u = std::max(random01(), 1e-9), v = random01();
    return std::sqrt(-2.0 * std::log(u)) * std::cos(2.0 * kPi * v);
}

void BinarySystem::clear()
{
    gas.clear();
    swallowed = intoDisk = 0;
    emitDebt_ = 0.0;
}

// Nouveaux paquets de gaz au point L1, qui tournent avec l'orbite
// (vitesse Ω × r dans le repère du trou noir), avec un peu d'agitation
// thermique. Le jet a la largeur du col de L1, une fraction de rs ici.
void BinarySystem::emit(double count)
{
    const Vec3d c = companionPos(time_);
    const double a = settings.separation, w = omega();
    const double ux = c.x / a, uz = c.z / a;   // direction trou noir -> étoile
    const double l1 = l1Distance();
    const double width = 0.012 * a;
    for (int n = 0; n < int(count) && gas.size() < kMaxGasParcels; ++n) {
        GasParcel g;
        double side = gaussian() * width, up = gaussian() * width * 0.6;
        double x = l1 - std::abs(gaussian()) * 0.01 * a;
        g.pos[0] = ux * x - uz * side;
        g.pos[1] = up;
        g.pos[2] = uz * x + ux * side;
        // Rotation avec l'orbite, plus une petite poussée vers le trou noir.
        double push = 0.004 + 0.002 * random01();
        g.vel[0] = -w * g.pos[2] - ux * push + gaussian() * 0.0015;
        g.vel[1] = gaussian() * 0.0015;
        g.vel[2] = w * g.pos[0] - uz * push + gaussian() * 0.0015;
        g.heat = 0.75f * settings.temperature;
        gas.push_back(g);
    }
}

void BinarySystem::advance(double dt, double diskOuter)
{
    const double q = settings.massRatio;
    // Le gaz déborde dès que l'étoile touche son lobe ; plus elle déborde,
    // plus le col L1 s'élargit et plus le débit est fort.
    if (overflowing()) {
        double over = std::clamp((settings.fill - 0.96) / 0.04, 0.0, 1.0);
        emitDebt_ += dt * 5.0 * settings.streamRate * (0.35 + 0.65 * over);
        double whole = std::floor(emitDebt_);
        emitDebt_ -= whole;
        emit(whole);
    }

    const double gmStar = kGM * q;
    const double perp = radius() / std::cbrt(stretch());   // plus petit demi-axe
    size_t kept = 0;
    for (GasParcel& g : gas) {
        double t = 0.0;
        bool gone = false;
        while (t < dt) {
            // Pas adapté à la distance au corps le plus proche.
            Vec3d c = companionPos(time_ + t);
            double rb = length3(g.pos);
            double dc[3] = {g.pos[0] - c.x, g.pos[1] - c.y, g.pos[2] - c.z};
            double rc = length3(dc);
            double near = std::min(rb - 1.0, rc);
            double h = std::clamp(0.04 * near * std::sqrt(near / kGM), 0.005, 2.0);
            h = std::min(h, dt - t);

            // Verlet : demi-pas de vitesse, pas de position, demi-pas de vitesse.
            auto accel = [&](const double* p, double time, double* acc) {
                Vec3d cc = companionPos(time);
                double r = length3(p);
                double fb = -kGM / ((r - 1.0) * (r - 1.0)) / r;   // Paczyński-Wiita
                double d[3] = {p[0] - cc.x, p[1] - cc.y, p[2] - cc.z};
                double rd = length3(d);
                double fc = -gmStar / (rd * rd * rd);
                double ra = settings.separation;
                double fi = -gmStar / (ra * ra * ra);   // le trou noir est attiré par l'étoile
                acc[0] = fb * p[0] + fc * d[0] + fi * cc.x;
                acc[1] = fb * p[1] + fc * d[1] + fi * cc.y;
                acc[2] = fb * p[2] + fc * d[2] + fi * cc.z;
            };
            double acc[3];
            accel(g.pos, time_ + t, acc);
            for (int k = 0; k < 3; ++k) {
                g.vel[k] += 0.5 * h * acc[k];
                g.pos[k] += h * g.vel[k];
            }
            accel(g.pos, time_ + t + h, acc);
            for (int k = 0; k < 3; ++k) g.vel[k] += 0.5 * h * acc[k];
            t += h;

            double r = length3(g.pos);
            if (r < 1.05) { ++swallowed; gone = true; break; }
            if (diskOuter > 0.0 && r < diskOuter * 0.95 && std::abs(g.pos[1]) < 1.5) {
                ++intoDisk;
                gone = true;
                break;
            }
            Vec3d c2 = companionPos(time_ + t);
            double back[3] = {g.pos[0] - c2.x, g.pos[1] - c2.y, g.pos[2] - c2.z};
            if (g.age > 20.0f && length3(back) < perp * 0.9) { gone = true; break; }   // retombé sur l'étoile
            if (r > 90.0) { gone = true; break; }
        }
        g.age += float(dt);
        if (gone || g.age > 4000.0f) continue;

        // Le gaz chauffe en tombant (compression, frottements) : de la
        // température de l'étoile à des dizaines de milliers de degrés.
        double r = length3(g.pos);
        double hot = 26000.0 * std::pow(4.0 / std::max(r, 1.5), 0.75);
        g.heat = float(std::max(0.75 * settings.temperature, hot));
        gas[kept++] = g;
    }
    gas.resize(kept);
    time_ += dt;
}

void BinarySystem::update(double t, double diskOuter)
{
    if (!settings.enabled) {
        if (started_) clear();
        started_ = false;
        return;
    }
    double dt = t - time_;
    if (!started_ || dt < 0.0 || dt > 300.0) {
        // Jet déjà établi : on fait couler le gaz pendant un moment.
        clear();
        started_ = true;
        const double warm = 450.0;
        time_ = t - warm;
        for (double done = 0.0; done < warm; done += 1.0) advance(1.0, diskOuter);
        time_ = t;
        return;
    }
    // Pas d'au plus 2 rs/c (temps accéléré) pour garder le débit régulier.
    while (dt > 1e-9) {
        double h = std::min(dt, 2.0);
        advance(h, diskOuter);
        dt -= h;
    }
}
