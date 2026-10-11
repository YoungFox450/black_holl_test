// Carte « Réalisme » du panneau (onglet Rendu) : effets relativistes en plus
// (retard de la lumière, ciel bleui près du trou noir, gaz sous l'ISCO,
// repères) et rendu photo (halo, exposition automatique, image lissée en
// pause, capture d'écran).

#include "ui_kit.hpp"

#include <algorithm>
#include <cmath>

namespace {

void realismCard(App& app)
{
    RealismSettings& r = app.realism;

    if (!app.starMode) {
        ui::subheading("Relativité");
        ui::toggle("Retard de la lumière", &r.lightDelay,
                   "La lumière met du temps à venir : on voit chaque morceau du disque tel "
                   "qu'il était quand elle est partie. L'image au-dessus de l'ombre (lumière "
                   "qui a fait le tour du trou noir) est donc en retard sur l'image principale.");
        ui::toggle("Ciel plus bleu près du trou noir", &r.observerShift,
                   "Une caméra immobile près de l'horizon reçoit une lumière plus énergétique : "
                   "le ciel et le disque deviennent plus brillants et plus bleus. Suit l'option "
                   "« décalage gravitationnel » du disque.");
        ui::toggle("Gaz qui plonge sous l'ISCO", &r.plunge,
                   "Sous la dernière orbite stable (3 rs), le gaz tombe en spirale vers "
                   "l'horizon : un voile pâle entre le disque et l'ombre.");
        ui::toggle("Repères", &r.guides,
                   "Cercles dans le plan du disque, courbés par la lentille comme le reste : "
                   "bleu = sphère de photons (1,5 rs), vert = ISCO (3 rs).");

        // Caméra immobile à la distance r : son temps propre avance moins vite
        // que celui d'un observateur lointain, d'un facteur sqrt(1 - rs / r).
        float d = std::max(app.camera.distance, 1.01f);
        float slow = 1.0f / std::sqrt(1.0f - 1.0f / d);
        ui::value("1 s pour la caméra", "%.3f s au loin", slow);
        ui::value("Lumière reçue", "× %.2f en énergie", slow);
    }

    ui::subheading("Image");
    ui::slider("Halo lumineux", &r.bloom, 0.0f, 1.5f, "%.2f",
               "La lumière très vive déborde autour d'elle, comme dans l'œil ou un objectif. "
               "0 = coupé. Presque gratuit pour la carte graphique.");
    ui::toggle("Exposition automatique", &r.autoExposure,
               "Ajuste la luminosité à la scène, comme l'œil qui s'adapte. Le réglage "
               "« Exposition » reste un multiplicateur.");
    ui::toggle("Image lissée en pause", &r.smoothPause,
               "En pause, caméra immobile : moyenne de 64 images décalées d'une fraction de "
               "pixel. Bords nets et sans escaliers, idéal avant une capture. Ensuite le "
               "ray tracing s'arrête : la carte graphique se repose.");
    if (r.smoothPause && app.paused && !app.starMode)
        ui::value("Images moyennées", "%d / 64", std::min(r.accumulated, 64));
    if (ui::button("Capture d'écran (F12)"))
        r.screenshotRequested = true;
}
UI_SECTION(ui::Tab::Render, 25, "Réalisme", realismCard);

} // namespace
