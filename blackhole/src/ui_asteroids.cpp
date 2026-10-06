// Section du panneau pour les astéroïdes (src/asteroids.*) : un astéroïde,
// un champ, et le bilan de ce que le corps central leur fait.

#include "ui_kit.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {

void formatPeriod(double seconds, char* out, size_t size)
{
    struct Unit { double s; const char* name; };
    static const Unit units[] = {
        {365.25 * 86400.0, "ans"}, {86400.0, "jours"}, {3600.0, "h"}, {60.0, "min"},
        {1.0, "s"}, {1e-3, "ms"}, {1e-6, "µs"},
    };
    for (const Unit& u : units) {
        if (seconds >= u.s || u.s == 1e-6) {
            std::snprintf(out, size, "%.3g %s", seconds / u.s, u.name);
            return;
        }
    }
}

void asteroidCard(App& app)
{
    AsteroidSystem& ast = app.asteroids;
    const CentralBody body = centralBody(app);
    const char* unit = app.starMode ? "R" : "rs";
    char fmt[32], buf[48];
    std::snprintf(fmt, sizeof(fmt), "%%.2f %s", unit);

    ui::toggle("Afficher les astéroïdes", &app.showAsteroids);
    static int material = 1;
    static const char* const materials[] = {"Gravats", "Roche", "Fer"};
    const double densities[] = {1500.0, 2500.0, 7800.0};
    ui::segmented("matiere", &material, materials, 3);
    ast.density = densities[material];

    // Ce que le corps central fait aux astéroïdes.
    ui::subheading("Ce que le corps central leur fait");
    double roche = body.rocheRadius(ast.density);
    std::snprintf(buf, sizeof(buf), roche > 1.0e4 ? "%.2g %s" : "%.3g %s", roche, unit);
    ui::value("Limite de Roche", "%s", buf);
    if (!body.blackHole) {
        // Équilibre radiatif T = T* sqrt(R / 2d) = 1500 K.
        ui::value("La roche fond en dessous de", "%.3g R", 0.5 * std::pow(body.temperature / 1500.0, 2.0));
    } else {
        ui::value("Dernière orbite stable", "3 rs");
    }
    ui::toggle("Marées (dislocation)", &ast.tides,
               "Coupé : les astéroïdes ne se disloquent plus sous la limite de "
               "Roche. Utile autour d'une étoile à neutrons, où cette limite "
               "est à ~70 000 rayons : tout astéroïde y est brisé aussitôt.");
    if (!body.blackHole) {
        ui::toggle("Chaleur (fonte)", &ast.heating,
                   "Coupé : les astéroïdes chauffent (ils rougeoient) mais ne "
                   "fondent plus.");
        if (ast.heating)
            ui::slider("Vitesse de fonte", &ast.meltSpeed, 0.05, 5.0, "× %.2f",
                       "Ralentit ou accélère la fonte affichée.", true);
    }
    ui::note("Sous la limite de Roche, les marées dépassent la gravité propre de "
             "l'astéroïde : il se disloque en morceaux qui s'étalent sur l'orbite. "
             "Elle est immense autour d'un trou noir stellaire ou d'une étoile à "
             "neutrons, vers 10 rs pour Sgr A*, et sous l'horizon pour un quasar "
             "(avalé entier). Près d'une étoile, la roche rougeoie puis fond.");

    ui::subheading("Un astéroïde (G)");
    static float radius = 8.0f, speed = 1.0f, inclination = 0.0f, size = 5.0f, azimuth = 0.0f;
    ui::slider("Distance##ast", &radius, 1.1f, 40.0f, fmt, nullptr, true);
    ui::slider("Vitesse", &speed, 0.0f, 1.6f, "%.2f × circulaire",
               "1 : orbite circulaire. Moins : il tombe vers le centre sur une "
               "ellipse (0 = chute droite). 1,41 : vitesse de libération.");
    ui::slider("Inclinaison##ast", &inclination, -90.0f, 90.0f, "%.0f°");
    ui::slider("Position", &azimuth, 0.0f, 360.0f, "%.0f°");
    ui::slider("Diamètre", &size, 0.1f, 100.0f, "%.1f km", nullptr, true);
    formatPeriod(body.realPeriod(std::max(double(radius), 1.05)), buf, sizeof(buf));
    ui::value("Période réelle (orbite circulaire)", "%s", buf);
    if (ui::button("Ajouter l'astéroïde", true))
        ast.addOrbit(body, radius, speed, inclination, azimuth, size);

    ui::subheading("Champ d'astéroïdes (F)");
    FieldSettings& f = app.field;
    ui::slider("Nombre", &f.count, 10, 2000);
    ui::slider("Bord intérieur##champ", &f.innerRadius, 1.1f, 40.0f, fmt, nullptr, true);
    f.outerRadius = std::max(f.outerRadius, f.innerRadius + 0.2f);
    ui::slider("Bord extérieur##champ", &f.outerRadius, f.innerRadius + 0.2f, 50.0f, fmt, nullptr, true);
    ui::slider("Épaisseur", &f.inclination, 0.0f, 30.0f, "%.1f°",
               "Dispersion des inclinaisons autour du plan du disque.");
    ui::slider("Excentricité", &f.eccentricity, 0.0f, 0.6f, "%.2f",
               "Dispersion des vitesses : orbites plus ou moins allongées.");
    if (ui::button("Ajouter le champ", true)) {
        ast.addField(body, f);
        // Recule la caméra pour voir tout le champ.
        app.camera.targetDistance =
            std::clamp(std::max(app.camera.targetDistance, f.outerRadius * 1.8f), kMinDistance, kMaxDistance);
    }
    if (ui::button("Tout retirer (X)"))
        ast.clear();

    ui::subheading("Bilan");
    const AsteroidStats& st = ast.stats;
    ui::value("En orbite", "%d", int(ast.items.size()));
    if (body.blackHole) {
        ui::value("Avalés par le trou noir", "%d", st.swallowed);
    } else {
        ui::value("Écrasés sur l'étoile", "%d", st.impacts);
        ui::value("Fondus", "%d", st.vaporized);
    }
    ui::value("Disloqués par les marées", "%d", st.disrupted);
    ui::value("Partis à l'infini", "%d", st.ejected);
}
UI_SECTION(ui::Tab::Object, 60, "Astéroïdes", asteroidCard);

} // namespace
