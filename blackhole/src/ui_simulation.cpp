// Section "Simulation" du panneau : lois physiques qu'on peut couper pour
// comparer, et avance pas à pas pendant la pause.

#include "ui_kit.hpp"

namespace {

void simulationCard(App& app)
{
    ui::toggle("Lentille gravitationnelle", &app.lensing,
               "Touche V. Coupée, la lumière va tout droit : plus d'anneau "
               "d'Einstein ni d'image du disque au-dessus de l'ombre. Montre ce "
               "que la relativité générale ajoute à l'image.");
    if (!app.paused) {
        if (ui::button("Pause (P)"))
            app.paused = true;
    } else {
        if (ui::button("Avancer d'un pas (T)", true))
            ++app.stepRequests;
        if (ui::button("Reprendre (P)"))
            app.paused = false;
    }
    ui::note("Un pas = 1/30 s à la vitesse du temps choisie en haut du panneau. "
             "Pratique pour suivre un astéroïde qui se disloque.");
}
UI_SECTION(ui::Tab::View, 20, "Simulation", simulationCard);

} // namespace
