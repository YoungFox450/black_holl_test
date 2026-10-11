// Section du panneau pour le système double (src/binary.*) : une étoile
// compagne dont le trou noir arrache le gaz.

#include "ui_kit.hpp"

#include <cmath>
#include <cstdio>

namespace {

bool isBlackHoleScene(const App& app) { return !app.starMode; }

struct BinaryPreset {
    const char* name;
    float massRatio, separation, fill, temperature;
};

// Deux binaires X connues. Les distances sont rapprochées (voir binary.hpp),
// les rapports de masse et les températures sont les vrais.
const BinaryPreset kPresets[] = {
    // Supergéante bleue de 40 soleils autour d'un trou noir de 21 soleils.
    {"Cygnus X-1", 1.9f, 34.0f, 0.99f, 31000.0f},
    // Petite étoile orange de 0,4 soleil autour d'un trou noir de 6,6 soleils.
    {"A0620-00", 0.06f, 20.0f, 1.0f, 4500.0f},
};

void formatDuration(double seconds, char* out, size_t size)
{
    if (seconds >= 86400.0) std::snprintf(out, size, "%.3g jours", seconds / 86400.0);
    else if (seconds >= 3600.0) std::snprintf(out, size, "%.3g h", seconds / 3600.0);
    else if (seconds >= 60.0) std::snprintf(out, size, "%.3g min", seconds / 60.0);
    else if (seconds >= 1.0) std::snprintf(out, size, "%.3g s", seconds);
    else std::snprintf(out, size, "%.3g ms", seconds * 1e3);
}

void binaryCard(App& app)
{
    BinarySystem& bin = app.binary;
    BinarySettings& s = bin.settings;
    bool wasOn = s.enabled;
    ui::toggle("Étoile compagne", &s.enabled,
               "Une étoile en orbite autour du trou noir. Quand elle remplit son "
               "lobe de Roche, le trou noir lui arrache son gaz.");
    if (s.enabled && !wasOn) applyBinary(app);
    if (!s.enabled) {
        ui::note("Binaire X : un trou noir et une étoile qui tournent l'un autour "
                 "de l'autre, comme Cygnus X-1, le premier trou noir découvert (1971).");
        return;
    }

    static int preset = -1;
    static const char* const names[] = {kPresets[0].name, kPresets[1].name};
    if (ui::segmented("binaire", &preset, names, 2)) {
        const BinaryPreset& p = kPresets[preset];
        s.massRatio = p.massRatio;
        s.separation = p.separation;
        s.fill = p.fill;
        s.temperature = p.temperature;
        app.disk.outerRadius = 12.0f;
        fitDiskToBinary(app);
    }

    bool changed = false;
    changed |= ui::slider("Distance##bin", &s.separation, 14.0f, 40.0f, "%.1f rs",
                          "Rapprochée pour l'affichage : les vraies binaires X sont des "
                          "millions de fois plus grandes que l'horizon.");
    changed |= ui::slider("Masse de l'étoile", &s.massRatio, 0.03f, 3.0f, "%.2f × trou noir",
                          "q = m / M. Fixe la taille des lobes de Roche et la position de L1.", true);
    changed |= ui::slider("Remplissage du lobe", &s.fill, 0.5f, 1.05f, "%.2f",
                          "Rayon de l'étoile / rayon de son lobe de Roche. À 1, la marée "
                          "l'étire en goutte et le gaz s'échappe par L1. Une étoile qui "
                          "vieillit gonfle et finit par remplir son lobe.");
    if (ui::slider("Température##bin", &s.temperature, 3000.0f, 40000.0f, "%.0f K", nullptr, true))
        preset = -1;
    ui::slider("Débit du gaz", &s.streamRate, 0.2f, 4.0f, "%.2f", nullptr, true);
    ui::slider("Luminosité##bin", &s.brightness, 0.2f, 3.0f, "%.2f", nullptr, true);
    if (changed) {
        preset = -1;
        fitDiskToBinary(app);
    }

    ui::subheading("Orbite");
    // rs/c pour un trou noir de M soleils : 2 G M / c³ = 9,85 µs × M.
    const double unit = 9.85e-6 * app.massSolar;
    char buf[48];
    formatDuration(bin.period() * unit, buf, sizeof(buf));
    ui::value("Période", "%s", buf);
    ui::value("Vitesse de l'étoile", "%.1f %% de c", 100.0 * s.separation * bin.omega());
    ui::value("Lobe de l'étoile", "%.1f rs", bin.rocheLobe());
    ui::value("Lobe du trou noir", "%.1f rs", bin.holeLobe());
    ui::value("Point L1", "à %.1f rs du trou noir", bin.l1Distance());

    ui::subheading("Transfert de masse");
    if (bin.overflowing())
        ui::value("État", "le gaz s'échappe par L1");
    else
        ui::value("État", "étoile détachée");
    ui::value("Gaz en vol", "%d paquets", int(bin.gas.size()));
    ui::value("Arrivés sur le disque", "%d", bin.intoDisk);
    ui::value("Avalés directement", "%d", bin.swallowed);
    ui::note("Le gaz ne tombe pas tout droit : il garde la rotation de l'orbite, "
             "s'enroule autour du trou noir et frappe le bord du disque (point "
             "chaud). Le disque, chauffé par les frottements, émet des rayons X "
             "qui chauffent à leur tour la face de l'étoile tournée vers le trou "
             "noir. Sa lumière est déviée comme celle du disque : quand elle "
             "passe derrière le trou noir, on en voit une seconde image.");
}
UI_SECTION(ui::Tab::Object, 17, "Système double", binaryCard, isBlackHoleScene);

} // namespace
