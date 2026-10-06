// Section du panneau pour le fond de ciel (src/sky_image.*) : vraie Voie
// lactée ou ciel procédural, image à charger et orientation.

#include "ui_kit.hpp"

#include <cstdio>
#include <cstring>

namespace {

void skyCard(App& app)
{
    SkySettings& s = app.sky;
    if (ui::toggle("Vraie Voie lactée", &s.useImage,
                   "Carte du ciel réel : 118 000 étoiles des catalogues Hipparcos "
                   "et Tycho-2 avec leurs vraies couleurs, contours de la Voie "
                   "lactée, Nuages de Magellan, Andromède, nébuleuses. Sinon, "
                   "ciel procédural."))
        s.rebuild = true;
    if (!s.useImage) return;

    bool c = ui::slider("Inclinaison de la Galaxie", &s.tilt, -90.0f, 90.0f, "%.0f°",
                        "Angle entre le plan de la Voie lactée et le disque du trou noir.");
    c |= ui::slider("Orientation", &s.yaw, -180.0f, 180.0f, "%.0f°",
                    "0 : le centre de la Galaxie (Sagittaire) est derrière le trou noir "
                    "vu de la position de départ.");
    static const char* const frames[] = {"Galactique", "Équatorial"};
    int frame = s.galactic ? 0 : 1;
    if (ui::segmented("repere", &frame, frames, 2)) {
        s.galactic = frame == 0;
        c = true;
    }
    c |= ui::toggle("Miroir", &s.mirror, "Si les constellations apparaissent à l'envers.");
    if (c) s.rebuild = true;

    ui::subheading("Autre image");
    static char path[512] = "";
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##skypath", "chemin d'un .hdr, .exr, .jpg", path, sizeof(path));
    if (ui::button("Charger l'image")) {
        s.path = path;
        s.rebuild = true;
    }
    if (!s.path.empty() && ui::button("Revenir à la carte fournie")) {
        s.path.clear();
        s.galactic = true;
        s.rebuild = true;
    }
    ui::note(s.status.c_str());
    ui::note("Images conseillées : les Deep Star Maps de la NASA (domaine public, "
             "svs.gsfc.nasa.gov/4851), par exemple starmap_2020_4k_gal.exr. Une "
             "image équirectangulaire 2:1, en coordonnées galactiques (\"_gal\") ou "
             "équatoriales.");
}
UI_SECTION(ui::Tab::Render, 15, "Fond de ciel", skyCard);

} // namespace
