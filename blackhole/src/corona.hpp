#pragma once

// Couronne, vent, protubérances et éjections de masse coronale (CME) des
// étoiles à dynamo (enveloppe convective), déduits de leur activité
// magnétique (src/activity.cpp). Formules rappelées dans corona.cpp.
//
//   1. Couronne : flux X de surface F_X = (L_X / L_bol) σ T⁴, température
//      T_cor = 0,11 F_X^0,26 MK (Johnstone & Güdel 2015). Au minimum du
//      cycle, de longs jets coronaux (« streamers ») près de l'équateur ;
//      au maximum, une couronne plus ronde.
//   2. Vent : coronal pour les naines (Ṁ ∝ R² F_X^1,34, Wood et al. 2005,
//      vitesse du vent de Parker), loi de Reimers pour les géantes froides,
//      vent poussé par la lumière pour les étoiles chaudes et lumineuses.
//   3. Protubérances : nuages de gaz froid (~8 000 K) tenus par le champ
//      magnétique au-dessus de la surface. Plus nombreuses quand l'étoile
//      est active, dans les bandes de taches et dans la « couronne
//      polaire ». Vues au bord : arches roses (raie Hα) ; vues sur le
//      disque : filaments sombres. Sur les rotateurs rapides, nuages
//      piégés vers le rayon de corotation (« slingshot prominences »).
//   4. Éjections de masse coronale : une protubérance qui se déstabilise
//      ou une grosse éruption éjecte une bulle de plasma (structure en
//      3 parties : front brillant, cavité, cœur = la protubérance). Sur les
//      étoiles très actives, le champ magnétique en retient une grande
//      partie (confinement, Alvarado-Gómez et al. 2018).

#include "activity.hpp"
#include "star.hpp"

#include <array>
#include <cstdint>

struct CoronaModel {
    bool active = false;          // étoile à dynamo : couronne chaude, protubérances, CME
    double xrayFlux = 0.0;        // F_X, erg/cm²/s
    double temperatureMK = 0.0;   // température de la couronne
    double windKms = 0.0;         // vitesse du vent loin de l'étoile
    double massLoss = 0.0;        // Ṁ, masses solaires par an
    const char* windKind = "";    // mécanisme du vent
    double cmePerDay = 0.0;       // éjections par jour (moyenne sur le cycle)
    double cmeEscape = 1.0;       // fraction qui s'échappe (le reste retombe)
    double corotationRadius = 0.0;// rayons d'étoile (orbite qui tourne avec l'étoile)
    bool slingshot = false;       // protubérances piégées vers la corotation
};

CoronaModel computeCorona(const Star& star, const StellarActivity& act);

struct Prominence {
    double latitude = 0.0, longitude = 0.0;  // pied du milieu (repère tournant, radians)
    double orientation = 0.0;    // direction de la protubérance sur la surface (rad)
    double halfLength = 0.1;     // demi-longueur (radians d'arc)
    double height = 0.08;        // hauteur (rayons d'étoile)
    int type = 0;                // 0 rideau calme, 1 arche active, 2 nuage en corotation
    double bornDays = 0.0;
    double lifeDays = 20.0;
    double eruptDays = -1.0;     // début de l'éruption (< 0 : stable)
    float seed = 0.0f;
    bool launchesCme = false;    // son éruption lance une CME
};

struct Cme {
    double dir[3] = {1, 0, 0};   // direction (repère fixe : la bulle ne tourne plus)
    double startDays = 0.0;
    double speedKms = 450.0;
    double displaySpeed = 0.8;   // rayons par seconde affichée
    double width = 0.6;          // demi-ouverture (radians)
    bool escapes = true;         // false : retenue par le champ, elle retombe
};

class CoronaSimulator {
public:
    static constexpr int kMaxProminences = 6;
    static constexpr int kMaxCmes = 3;

    // Avance jusqu'au temps `days`. `rotation` : angle de rotation affiché
    // de l'étoile (pour lancer les CME dans la bonne direction).
    void update(const Star& star, const StellarActivity& act, const CycleState& cyc,
                double days, double rotation);
    void clear();
    // Lance tout de suite une éjection (touche M) : la protubérance la
    // plus haute éclate, ou une bulle part d'une région active.
    void launchCme(const Star& star, const StellarActivity& act, double days, double rotation);

    // Hauteur et luminosité d'une protubérance (éruption : elle monte et pâlit).
    double heightAt(const Prominence& p, double days) const;
    double fadeAt(const Prominence& p, double days) const;
    // 0 : posée sur la surface, 1 : décollée (éruption).
    double liftAt(const Prominence& p, double days) const;
    // Rayon de la bulle d'une CME et sa luminosité (0 : finie).
    double cmeRadius(const Cme& c, double days) const;
    double cmeBrightness(const Cme& c, double days) const;

    const std::array<Prominence, kMaxProminences>& prominences() const { return proms_; }
    int prominenceCount() const { return promCount_; }
    const std::array<Cme, kMaxCmes>& cmes() const { return cmes_; }
    int cmeCount() const { return cmeCount_; }
    int cmeTotal() const { return cmeTotal_; }
    int cmeFailed() const { return cmeFailed_; }

private:
    double uniform();
    double gaussian();
    void spawnProminence(const Star& star, const StellarActivity& act, const CoronaModel& cor,
                         const CycleState& cyc, double days);
    bool recentCme(double days) const;
    void addCme(const Star& star, const StellarActivity& act, double lat, double lon, double days,
                double rotation);

    std::array<Prominence, kMaxProminences> proms_{};
    std::array<Cme, kMaxCmes> cmes_{};
    int promCount_ = 0, cmeCount_ = 0, cmeTotal_ = 0, cmeFailed_ = 0;
    bool started_ = false;
    double lastDays_ = 0.0;
    double nextCme_ = -1.0;
    std::uint64_t rng_ = 0xD1B54A32D192ED03ull;
};
