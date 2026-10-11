#pragma once

// Régions actives : les taches naissent par groupes bipolaires (deux
// taches de polarités magnétiques opposées, reliées par des boucles de
// champ). Chaque groupe suit des lois mesurées sur le Soleil :
//
//   - latitude de naissance : bandes du cycle (loi de Spörer, activity.cpp) ;
//   - loi de Joy : l'axe du groupe est incliné de γ = 32,1° sin|λ|
//     (Stenflo & Kosovichev 2012), la tache de tête (vers l'ouest, dans le
//     sens de la rotation) plus près de l'équateur ;
//   - loi de Hale : la tache de tête a une polarité opposée dans les deux
//     hémisphères, et la polarité s'inverse à chaque cycle ;
//   - aires : loi log-normale (Bogdan et al. 1988), tache de tête plus
//     grosse (~60 % de l'aire) ;
//   - vie : croissance rapide puis aire qui décroît linéairement à
//     ~10 MSH par jour (règle de Gnevyshev-Waldmeier) ;
//   - rotation différentielle : chaque tache tourne à la vitesse de sa
//     latitude, Ω(λ) = Ω_eq - ΔΩ sin²λ, et le groupe se déforme.
//
// L'aire totale suit la fraction tachée du modèle d'activité (f × niveau du
// cycle) : quelques groupes sur le Soleil, de grandes taches sur les naines
// très actives. Les éruptions, les arches et les éjections partent de ces
// régions (corona.cpp, activity.cpp).

#include "activity.hpp"
#include "star.hpp"

#include <array>
#include <cstdint>

struct ActiveRegion {
    // Taches de tête (0) et de queue (1). Longitude : même convention que
    // les éruptions, angle affiché = lon + (Ω_eq - ΔΩ sin²λ) × jours.
    double lat[2] = {0.0, 0.0};
    double lon[2] = {0.0, 0.0};
    double bornDays = 0.0;
    double growDays = 1.0;       // émergence (montée de l'aire)
    double lifeDays = 30.0;
    double peakArea = 0.0;       // aire des deux taches, fraction de la surface
    double tilt = 0.0;           // angle de Joy (rad)
    int leadingPolarity = 1;     // +1 : champ sortant, -1 : rentrant
    float seed = 0.0f;
};

class ActiveRegionSimulator {
public:
    static constexpr int kMax = 12;
    static constexpr double kLeadShare = 0.6;   // part de l'aire dans la tache de tête

    // cycleNumber : numéro du cycle en cours (sa parité fixe les polarités).
    void update(const Star& star, const StellarActivity& act, const CycleState& cyc,
                int cycleNumber, double days);
    void clear();

    // Aire (fraction de la surface) des deux taches au temps `days`.
    double areaAt(const ActiveRegion& r, double days) const;
    // Rayon angulaire (rad) d'une tache d'aire a (calotte : a = sin²(ρ/2)).
    static double spotRadius(double area);

    // Région tirée au hasard, d'autant plus souvent qu'elle est grande
    // (les grosses régions produisent les grosses éruptions). u dans [0, 1).
    // Renvoie -1 s'il n'y a aucune région.
    int pick(double u, double days) const;
    // Milieu du groupe (latitude, longitude) et angle de son axe sur la
    // surface (0 = est-ouest), dans la convention des éruptions.
    void center(const ActiveRegion& r, double& lat, double& lon) const;
    // Longitude affichée (repère de l'étoile qui tourne à la vitesse de
    // l'équateur, comme les protubérances) d'un point (lat, lon) au temps
    // `days`, entraîné par la rotation différentielle.
    double equatorFrameLongitude(double lat, double lon, double days) const;
    // Angle de l'axe tête -> queue sur la surface (0 = vers l'est, pi/2 =
    // vers le nord) au temps `days` : la rotation différentielle le fait
    // tourner.
    double axisAngle(const ActiveRegion& r, double days) const;
    double shear() const { return shear_; }   // ΔΩ affiché (rad/jour)

    const std::array<ActiveRegion, kMax>& regions() const { return regions_; }
    int count() const { return count_; }
    int target() const { return target_; }
    int total() const { return total_; }
    double totalArea(double days) const;

private:
    double uniform();
    double gaussian();
    void spawn(const Star& star, const StellarActivity& act, const CycleState& cyc, int cycleNumber,
               double days);

    std::array<ActiveRegion, kMax> regions_{};
    int count_ = 0;
    int target_ = 0;
    int total_ = 0;
    bool started_ = false;
    double lastDays_ = 0.0;
    double meanArea_ = 0.0;
    double omega_ = 0.0, shear_ = 0.0;
    double maxLife_ = 120.0;
    std::uint64_t rng_ = 0xA0761D6478BD642Full;
};
