// Sections du panneau pour les objets compacts : étoiles à neutrons
// (pulsar, magnétar) et modèles de trou noir (quasar et ses jets).

#include "ui_kit.hpp"

#include <cmath>
#include <cstdio>

namespace {

bool isBlackHoleScene(const App& app) { return !app.starMode; }
bool isStarScene(const App& app) { return app.starMode; }
bool isNeutronStar(const App& app)
{
    return app.starMode && app.star.compact != Compact::None && app.star.magneticField > 0.0;
}

// --- Trou noir : modèles et jets ---------------------------------------------

void blackHoleModelsCard(App& app)
{
    static const char* const models[] = {"Stellaire", "Sgr A*", "Quasar"};
    int current = app.jets ? 2 : (app.massSolar > 1.0e5f ? 1 : 0);
    if (ui::segmented("modele", &current, models, 3)) {
        if (current == 0) {
            applyStellarBlackHole(app);
        } else if (current == 1) {
            applyStellarBlackHole(app);
            app.massSolar = 4.3e6f;
        } else {
            applyQuasar(app);
        }
    }
    ui::note("Quasar : trou noir de près d'un milliard de soleils au centre "
             "d'une galaxie (comme 3C 273), qui avale plusieurs soleils de gaz "
             "par an. Son disque brille plus que toute sa galaxie.");
    ui::toggle("Jets relativistes", &app.jets,
               "Touche J. Plasma éjecté le long de l'axe à 80 % de la vitesse de "
               "la lumière. L'effet Doppler amplifie le jet qui vient vers nous "
               "et éteint presque l'autre : beaucoup de quasars ne montrent "
               "qu'un seul jet.");
    if (app.jets) {
        ui::slider("Puissance des jets", &app.jetPower, 0.1f, 4.0f, "%.2f", nullptr, true);
        ui::slider("Vitesse du plasma", &app.jetBeta, 0.1f, 0.99f, "%.2f c",
                   "Plus le plasma est rapide, plus le jet qui vient vers nous est "
                   "amplifié et l'autre éteint (facteur Doppler au cube).");
        ui::slider("Largeur des jets", &app.jetWidth, 0.4f, 4.0f, "× %.2f", nullptr, true);
    }
}
UI_SECTION(ui::Tab::Object, 15, "Modèles et jets", blackHoleModelsCard, isBlackHoleScene);

// --- Étoile à neutrons -----------------------------------------------------------

void createNeutronStarCard(App& app)
{
    static double mass = 1.4, period = 0.5, field = 1.0e8, tilt = 40.0;
    bool c = ui::slider("Masse##ns", &mass, 1.1, 2.3, "%.2f × Soleil",
                        "Noyau effondré d'une étoile massive : 1,4 soleil dans une "
                        "boule de 12 km.");
    c |= ui::slider("Période de rotation", &period, 0.0014, 20.0, "%.4g s", nullptr, true);
    c |= ui::slider("Champ magnétique", &field, 1.0e4, 1.0e12, "%.2g T",
                    "Un aimant de frigo fait 0,005 T, un appareil d'IRM 3 T.", true);
    c |= ui::slider("Inclinaison du champ", &tilt, 0.0, 90.0, "%.0f°",
                    "Angle entre l'axe magnétique et l'axe de rotation : c'est lui "
                    "qui fait balayer les faisceaux.");
    if (c) {
        app.starIndex = -1;
        app.star = neutronStar(mass, period, field, tilt);
    }
    ui::note("Le type dépend de P et B. B > 4,4e9 T (champ critique quantique) : "
             "magnétar. B / P² > 1,7e7 T/s² : pulsar. Sinon : étoile à neutrons "
             "éteinte.");
}
UI_SECTION(ui::Tab::Object, 25, "Créer une étoile à neutrons", createNeutronStarCard, isStarScene);

void neutronStarCard(App& app)
{
    const Star& star = app.star;
    const double p = star.spinPeriod();
    ui::value("Rotation", "%.4g s (%.3g tours/s)", p, 1.0 / p);
    ui::value("Champ magnétique", "%.2g T", star.magneticField);
    ui::value("Puissance rayonnée", "%.3g × Soleil", star.spinDownPower() / 3.828e26);
    const double perYear = star.periodDerivative() * 365.25 * 86400.0;
    ui::value("Ralentissement", "%.3g s par an", perYear);
    ui::note("Un aimant qui tourne rayonne : L = B² R^6 w^4 (1 + sin² a) / 4c³. "
             "Cette énergie est prise à la rotation, qui ralentit.");
    if (star.compact == Compact::Magnetar)
        ui::note("Magnétar : la croûte craque sous la tension du champ, d'où les "
                 "sursauts de rayons X et gamma (éclairs violets).");
    else if (star.compact == Compact::Pulsar)
        ui::note("Pulsar : à chaque tour, un faisceau balaie la caméra. La "
                 "rotation est ralentie à l'écran pour qu'on la voie.");
}
UI_SECTION(ui::Tab::Object, 45, "Étoile à neutrons", neutronStarCard, isNeutronStar);

} // namespace
