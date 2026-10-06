#include "asteroids.hpp"

#include <algorithm>
#include <cmath>

namespace {

constexpr double kPi = 3.14159265358979;
constexpr double kSunMassKg = 1.98892e30;
constexpr double kSunGM = 1.32712e20;            // m³/s²
constexpr double kSunRsMeters = 2953.25;         // 2GM☉/c²
constexpr double kSunRadiusMeters = 6.957e8;
constexpr double kSunRsOverC = 9.8510e-6;        // s

// Autour d'une étoile, le temps affiché est compressé : G M = 3 (rayons
// d'étoile)³ / s², un tour à 2 rayons dure ~10 s à l'écran. La vraie
// période est affichée dans le panneau (realPeriod).
constexpr double kStarGM = 3.0;

constexpr double kVaporizeK = 1500.0;   // la roche commence à se sublimer
constexpr float kMinSizeKm = 0.05f;     // en dessous : vaporisé
constexpr double kEscapeRadius = 400.0;

double length3(const double v[3]) { return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); }

} // namespace

double CentralBody::rocheRadius(double density) const
{
    double meters = std::cbrt(3.0 * massKg / (2.0 * kPi * std::max(density, 1.0)));
    return meters / unitMeters;
}

double CentralBody::circularSpeed(double r) const
{
    // Paczyński-Wiita : v² = G M r / (r - rs)².
    double d = std::max(r - pwRs, 1e-3);
    return std::sqrt(gm * r) / d;
}

double CentralBody::realPeriod(double r) const
{
    if (blackHole) {
        // Ω = v / r en temps de la scène (rs/c), converti en secondes.
        return 2.0 * kPi * r / circularSpeed(r) * unitSeconds;
    }
    double a = r * unitMeters;
    return 2.0 * kPi * std::sqrt(a * a * a / (kSunGM * massKg / kSunMassKg));
}

CentralBody blackHoleBody(double massSolar)
{
    CentralBody b;
    b.blackHole = true;
    b.gm = 0.5;          // rs = 2 G M = 1
    b.pwRs = 1.0;
    b.surface = 1.0;     // horizon
    b.massKg = massSolar * kSunMassKg;
    b.unitMeters = massSolar * kSunRsMeters;
    b.unitSeconds = massSolar * kSunRsOverC;
    return b;
}

CentralBody starBody(const Star& star)
{
    CentralBody b;
    b.blackHole = false;
    b.gm = kStarGM;
    b.pwRs = std::min(star.compactness(), 0.9);   // compte pour une étoile à neutrons
    b.surface = 1.0;
    b.temperature = star.temperature;
    b.massKg = star.mass * kSunMassKg;
    b.unitMeters = star.radius * kSunRadiusMeters;
    return b;
}

double AsteroidSystem::random01()
{
    // xorshift32 : rapide et reproductible (mêmes champs à chaque lancement).
    seed_ ^= seed_ << 13;
    seed_ ^= seed_ >> 17;
    seed_ ^= seed_ << 5;
    return (seed_ >> 8) * (1.0 / 16777216.0);
}

double AsteroidSystem::gaussian()
{
    double u = std::max(random01(), 1e-9), v = random01();
    return std::sqrt(-2.0 * std::log(u)) * std::cos(2.0 * kPi * v);
}

void AsteroidSystem::addOrbit(const CentralBody& body, double r, double speedFactor,
                              double inclinationDeg, double azimuthDeg, float sizeKm)
{
    if (items.size() >= kMaxAsteroids) return;
    r = std::max(r, body.surface * 1.05);
    double az = azimuthDeg * kPi / 180.0;
    double inc = inclinationDeg * kPi / 180.0;
    double v = body.circularSpeed(r) * speedFactor;

    Asteroid a;
    a.pos[0] = r * std::cos(az);
    a.pos[1] = 0.0;
    a.pos[2] = r * std::sin(az);
    // Même sens de rotation que le gaz du disque (φ croissant), orbite
    // basculée autour de la direction radiale.
    double tx = -std::sin(az), tz = std::cos(az);
    a.vel[0] = v * std::cos(inc) * tx;
    a.vel[1] = v * std::sin(inc);
    a.vel[2] = v * std::cos(inc) * tz;
    a.sizeKm = sizeKm;
    items.push_back(a);
}

void AsteroidSystem::addField(const CentralBody& body, const FieldSettings& f)
{
    double rIn = std::max(double(f.innerRadius), body.surface * 1.05);
    double rOut = std::max(double(f.outerRadius), rIn + 0.1);
    for (int i = 0; i < f.count && items.size() < kMaxAsteroids; ++i) {
        // Répartition uniforme en surface dans l'anneau.
        double u = random01();
        double r = std::sqrt(rIn * rIn + u * (rOut * rOut - rIn * rIn));
        double speed = 1.0 + f.eccentricity * (random01() - 0.5) * 2.0;
        double inc = f.inclination * gaussian();
        // Tailles : beaucoup de petits, peu de gros (loi de puissance).
        float size = float(std::min(0.3 * std::pow(std::max(random01(), 1e-4), -0.6), 60.0));
        addOrbit(body, r, speed, inc, random01() * 360.0, size);
    }
}

void AsteroidSystem::clear()
{
    items.clear();
    stats = {};
}

void AsteroidSystem::step(const CentralBody& body, double dt)
{
    if (dt <= 0.0 || items.empty()) return;
    const double roche = body.rocheRadius(density);
    std::vector<Asteroid> born;   // fragments créés pendant ce pas

    auto accel = [&](const double p[3], double out[3]) {
        double r = length3(p);
        double d = std::max(r - body.pwRs, 1e-3);
        double k = -body.gm / (d * d * r);
        out[0] = k * p[0];
        out[1] = k * p[1];
        out[2] = k * p[2];
    };

    size_t keep = 0;
    for (size_t i = 0; i < items.size(); ++i) {
        Asteroid a = items[i];
        bool alive = true;

        // Sous-pas : ~200 par orbite à la distance actuelle (vitesse Verlet).
        double r = length3(a.pos);
        double h = 0.03 * std::pow(std::max(r - body.pwRs, 0.05), 1.5) / std::sqrt(body.gm);
        int n = std::clamp(int(std::ceil(dt / h)), 1, 400);
        double sub = dt / n;
        double acc[3];
        accel(a.pos, acc);
        for (int s = 0; s < n; ++s) {
            for (int k = 0; k < 3; ++k) {
                a.vel[k] += 0.5 * sub * acc[k];
                a.pos[k] += sub * a.vel[k];
            }
            accel(a.pos, acc);
            for (int k = 0; k < 3; ++k)
                a.vel[k] += 0.5 * sub * acc[k];
            if (length3(a.pos) < body.surface) break;
        }
        r = length3(a.pos);

        if (r < body.surface) {
            if (body.blackHole) ++stats.swallowed;
            else ++stats.impacts;
            alive = false;
        } else if (r > kEscapeRadius) {
            ++stats.ejected;
            alive = false;
        }

        // Chaleur de l'étoile : équilibre radiatif T = T* sqrt(R / 2d).
        if (alive && !body.blackHole) {
            double t = body.temperature * std::sqrt(1.0 / (2.0 * r));
            a.heat = float(t);
            if (t > kVaporizeK) {
                // Perte de masse ~ flux reçu au-delà de la sublimation.
                // Plafonnée pour qu'on voie l'astéroïde fondre.
                double rate = std::min(0.02 * (std::pow(t / kVaporizeK, 4.0) - 1.0), 0.15);
                a.sizeKm *= float(std::exp(-rate * dt));
                if (a.sizeKm < kMinSizeKm) {
                    ++stats.vaporized;
                    alive = false;
                }
            }
        }

        // Marées : dislocation en fragments qui s'étalent le long de l'orbite.
        if (alive && !a.fragment && r < roche) {
            ++stats.disrupted;
            alive = false;
            int pieces = items.size() + born.size() < kMaxAsteroids - 8 ? 6 : 2;
            float size = a.sizeKm / std::cbrt(float(pieces));
            double v = length3(a.vel);
            for (int p = 0; p < pieces; ++p) {
                Asteroid f = a;
                f.fragment = true;
                f.sizeKm = size;
                // Écarts de vitesse de ~1 % : énergies orbitales différentes,
                // donc périodes différentes. Les morceaux se dispersent.
                for (int k = 0; k < 3; ++k) {
                    f.vel[k] += 0.012 * v * gaussian();
                    f.pos[k] += 0.004 * r * gaussian();
                }
                born.push_back(f);
            }
        }

        if (alive) items[keep++] = a;
    }
    items.resize(keep);
    for (const Asteroid& f : born)
        if (items.size() < kMaxAsteroids) items.push_back(f);
}
