// Panneau de contrôle de la simulation, dessiné avec Dear ImGui par-dessus
// l'image. F1 (ou Tab) l'affiche ou le cache.
//
// Mise en page : en-tête, lecture / vitesse du temps, choix de la scène,
// onglets (Objet, Vue, Rendu, Aide), puis les sections de l'onglet dans des
// cartes, et une ligne d'état en bas. Les sections sont enregistrées avec
// UI_SECTION (voir ui_kit.hpp) : d'autres fichiers peuvent en ajouter.

#include "ui.hpp"
#include "ui_kit.hpp"

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace {

constexpr float kPi = 3.14159265f;
constexpr float kRad2Deg = 180.0f / kPi;

// Rayon de Schwarzschild du Soleil (2GM/c²) et temps que met la lumière
// pour le parcourir (rs/c). Pour une masse M (en masses solaires), il suffit
// de multiplier par M.
constexpr double kSunRsKm = 2.9532;
constexpr double kSunRsOverCSeconds = 9.8510e-6;

UiStats gStats;   // mesures de l'image en cours, pour les sections
int gTab = 0;     // onglet affiché : 0 Objet, 1 Vue, 2 Rendu, 3 Aide

// "3,2 ms", "4,1 h", "12 ans"...
void formatDuration(double seconds, char* out, size_t size)
{
    struct Unit { double s; const char* name; };
    static const Unit units[] = {
        {365.25 * 86400.0, "ans"}, {86400.0, "jours"}, {3600.0, "h"}, {60.0, "min"},
        {1.0, "s"}, {1e-3, "ms"}, {1e-6, "µs"}, {1e-9, "ns"},
    };
    for (const Unit& u : units) {
        if (seconds >= u.s || u.s == 1e-9) {
            std::snprintf(out, size, "%.3g %s", seconds / u.s, u.name);
            return;
        }
    }
}

void formatLength(double km, char* out, size_t size)
{
    const double au = 1.495978707e8;
    if (km >= 0.1 * au) std::snprintf(out, size, "%.3g UA", km / au);
    else if (km >= 1.0) std::snprintf(out, size, "%.4g km", km);
    else std::snprintf(out, size, "%.3g m", km * 1000.0);
}

bool isBlackHole(const App& app) { return !app.starMode; }
bool isStar(const App& app) { return app.starMode; }

// --- Scènes du sélecteur ------------------------------------------------------

UI_SCENE("Trou noir", isBlackHole, [](App& app) { setStarMode(app, false); });
UI_SCENE("Étoile", isStar, [](App& app) { setStarMode(app, true); });

// --- Onglet Objet : trou noir ---------------------------------------------------

void blackHoleCard(App& app)
{
    ui::slider("Masse", &app.massSolar, 1.0f, 1.0e10f, "%.3g × Soleil",
               "L'image est la même pour toutes les masses : tout est calculé en "
               "rayons de Schwarzschild (rs). La masse fixe seulement l'échelle "
               "réelle. 10 soleils : trou noir stellaire. 4 millions : "
               "Sagittarius A*. 6,5 milliards : M87*.", true);

    const double rsKm = kSunRsKm * app.massSolar;
    const double unitS = kSunRsOverCSeconds * app.massSolar;
    // Période orbitale à l'ISCO (r = 3 rs) : 2π / Ω avec Ω = sqrt(M / r³), M = 0,5.
    const double iscoPeriod = 2.0 * kPi / std::sqrt(0.5 / 27.0);

    char buf[32];
    ui::subheading("Tailles réelles");
    formatLength(rsKm, buf, sizeof(buf));
    ui::value("Horizon", "%s", buf);
    formatLength(rsKm * 1.5, buf, sizeof(buf));
    ui::value("Sphère de photons", "%s", buf);
    formatLength(rsKm * 3.0, buf, sizeof(buf));
    ui::value("Dernière orbite stable", "%s", buf);
    formatDuration(iscoPeriod * unitS, buf, sizeof(buf));
    ui::value("Un tour sur cette orbite", "%s", buf);
}
UI_SECTION(ui::Tab::Object, 10, "Trou noir", blackHoleCard, isBlackHole);

void diskCard(App& app)
{
    DiskSettings& disk = app.disk;
    ui::toggle("Afficher le disque", &app.showDisk, "Touche H.");
    if (!app.showDisk) return;

    ui::slider("Bord intérieur", &disk.innerRadius, 1.5f, 10.0f, "%.2f rs",
               "3 rs est la dernière orbite circulaire stable (ISCO). Plus près, "
               "le gaz ne peut plus tourner : il tombe en chute libre.");
    disk.outerRadius = std::max(disk.outerRadius, disk.innerRadius + 0.5f);
    ui::slider("Bord extérieur", &disk.outerRadius, disk.innerRadius + 0.5f, 30.0f, "%.1f rs");
    ui::slider("Température", &disk.maxTemperature, 1500.0f, 30000.0f, "%.0f K",
               "Couleur du gaz le plus chaud (vers 4 rs). Un vrai disque atteint "
               "~10 millions de K et brille surtout en rayons X ; ici on choisit "
               "la couleur visible.", true);
    ui::slider("Luminosité", &disk.brightness, 0.1f, 6.0f, "%.2f", nullptr, true);
    ui::slider("Amas de gaz chaud", &disk.clumpCount, 0, 32, "%d",
               "Amas qui spiralent vers le trou noir à la vitesse képlérienne "
               "et chauffent en tombant.");
    ui::toggle("Effet Doppler", &disk.doppler,
               "Le gaz qui vient vers nous paraît plus brillant et plus bleu, "
               "celui qui s'éloigne plus sombre et plus rouge.");
    ui::toggle("Décalage gravitationnel", &disk.gravitationalShift,
               "La lumière perd de l'énergie en sortant du puits de gravité : "
               "facteur sqrt(1 - rs/r), fort près du bord intérieur.");
    if (ui::button("Valeurs par défaut"))
        disk = DiskSettings{};
}
UI_SECTION(ui::Tab::Object, 20, "Disque d'accrétion", diskCard, isBlackHole);

// --- Onglet Objet : étoile ------------------------------------------------------

void starCard(App& app)
{
    const Star& star = app.star;
    const auto& presets = starPresets();

    // Pastille de la couleur de l'étoile, nom et type.
    const float d = ImGui::GetFontSize() * 2.6f;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const ImVec4 col = ui::blackbodyColor(star.temperature);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 c(p.x + d * 0.5f, p.y + d * 0.5f);
    dl->AddCircleFilled(c, d * 0.62f, ImGui::ColorConvertFloat4ToU32(ImVec4(col.x, col.y, col.z, 0.15f)));
    dl->AddCircleFilled(c, d * 0.5f, ImGui::ColorConvertFloat4ToU32(col));
    ImGui::Dummy(ImVec2(d, d));
    ImGui::SameLine(0.0f, ImGui::GetFontSize() * 0.9f);
    ImGui::BeginGroup();
    ImGui::PushFont(ui::boldFont());
    ImGui::TextUnformatted(star.name.c_str());
    ImGui::PopFont();
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::color::muted), "%s · classe %s",
                       star.kind.c_str(), star.spectralClass().c_str());
    ImGui::EndGroup();

    ImGui::SetNextItemWidth(-FLT_MIN);
    const char* current = app.starIndex >= 0 ? presets[app.starIndex].name.c_str() : "Sur mesure";
    if (ImGui::BeginCombo("##connue", current)) {
        for (int i = 0; i < int(presets.size()); ++i) {
            if (ImGui::Selectable(presets[i].name.c_str(), i == app.starIndex))
                selectPreset(app, i);
            ImGui::SetItemTooltip("%s", presets[i].kind.c_str());
        }
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltip("Étoiles réelles. Touches N / B : suivante / précédente.");
}
UI_SECTION(ui::Tab::Object, 10, "Étoile", starCard, isStar);

void createStarCard(App& app)
{
    static int family = 0;          // 0 = séquence principale, 1 = naine blanche
    static double msMass = 1.0;
    static double wdMass = 0.6, wdTemp = 20000.0;
    static const char* const families[] = {"Comme le Soleil", "Naine blanche"};
    ui::segmented("famille", &family, families, 2);

    if (family == 0) {
        if (ui::slider("Masse##ms", &msMass, 0.08, 100.0, "%.3g × Soleil",
                       "Étoile qui brûle son hydrogène. La masse suffit : elle donne "
                       "la luminosité (L ~ M^3,5) et le rayon, puis la température. "
                       "0,1 soleil : naine rouge. 20 soleils : géante bleue.", true))
            selectMainSequence(app, msMass);
    } else {
        bool c = ui::slider("Masse##wd", &wdMass, 0.2, 1.42, "%.3g × Soleil",
                            "Cœur d'étoile éteinte, de la taille de la Terre. Plus "
                            "elle est lourde, plus elle est PETITE ; au-delà de "
                            "1,44 soleil elle s'effondre.");
        c |= ui::slider("Température##wd", &wdTemp, 4000.0, 100000.0, "%.0f K", nullptr, true);
        if (c) {
            app.starIndex = -1;
            app.star = whiteDwarf(wdMass, wdTemp);
        }
    }
}
UI_SECTION(ui::Tab::Object, 20, "Créer une étoile", createStarCard, isStar);

void tuneStarCard(App& app)
{
    Star& star = app.star;
    bool edited = false;
    edited |= ui::slider("Masse", &star.mass, 0.05, 150.0, "%.3g × Soleil", nullptr, true);
    edited |= ui::slider("Rayon", &star.radius, 1e-5, 2000.0, "%.3g × Soleil",
                         "1 = 696 000 km. Étoile à neutrons : ~0,00002 ; "
                         "Bételgeuse : ~760.", true);
    edited |= ui::slider("Température", &star.temperature, 2000.0, 1.0e6, "%.0f K", nullptr, true);
    edited |= ui::slider("Rotation", &star.rotationDays, 1e-5, 40000.0, "%.3g jours",
                         "Période à l'équateur. Pour une étoile froide, elle fixe "
                         "l'activité magnétique (carte Activité).", true);
    if (edited && app.starIndex >= 0) {
        app.starIndex = -1;
        star.name = "Sur mesure";
    }
}
UI_SECTION(ui::Tab::Object, 30, "Réglages fins", tuneStarCard, isStar);

void starPhysicsCard(App& app)
{
    const Star& star = app.star;
    ui::value("Luminosité", "%.3g × Soleil", star.luminosity());
    ui::value("Gravité de surface", "%.3g × Soleil", star.surfaceGravity());
    ui::value("Compacité rs/R", "%.2g", star.compactness());
    ui::note("Compacité : quasi nulle pour le Soleil (la lumière va tout droit), "
             "~0,35 pour une étoile à neutrons (on voit une partie de sa face "
             "cachée), 1 pour un trou noir.");
}
UI_SECTION(ui::Tab::Object, 40, "Ce que la physique en déduit", starPhysicsCard, isStar);

// --- Onglet Vue ------------------------------------------------------------------

void cameraCard(App& app)
{
    OrbitCamera& cam = app.camera;
    ui::slider("Distance", &cam.targetDistance, kMinDistance, kMaxDistance,
               app.starMode ? "%.1f rayons" : "%.1f rs", "Molette de la souris.", true);
    float yawDeg = std::remainder(cam.yaw, 2.0f * kPi) * kRad2Deg;
    if (ui::slider("Angle horizontal", &yawDeg, -180.0f, 180.0f, "%.0f°")) {
        cam.yaw = yawDeg / kRad2Deg;
        cam.yawVel = 0.0f;
    }
    float pitchDeg = cam.pitch * kRad2Deg;
    if (ui::slider("Hauteur", &pitchDeg, -kMaxPitch * kRad2Deg, kMaxPitch * kRad2Deg, "%.0f°",
                   "0° : dans le plan du disque. 86° : vue de dessus.")) {
        cam.pitch = pitchDeg / kRad2Deg;
        cam.pitchVel = 0.0f;
    }
    float fovDeg = cam.fovY * kRad2Deg;
    if (ui::slider("Champ de vision", &fovDeg, 20.0f, 120.0f, "%.0f°"))
        cam.fovY = fovDeg / kRad2Deg;
    ui::toggle("Orbite automatique", &cam.autoOrbit, "Touche Espace.");
    if (ui::button("Recentrer la vue"))
        resetCamera(app);
}
UI_SECTION(ui::Tab::View, 10, "Caméra", cameraCard);

// --- Onglet Rendu ----------------------------------------------------------------

void performanceCard(App& app)
{
    ui::toggle("Résolution automatique", &app.autoScale,
               "Baisse ou monte la résolution du ray tracing pour tenir le "
               "nombre d'images par seconde visé. Touche O.");
    if (ui::slider("Résolution", &app.renderScale, kMinScale, kMaxScale, "%.2f × fenêtre",
                   "Touches K / L."))
        app.autoScale = false;
    ui::slider("Images par seconde visées", &app.targetFps, 15.0f, 120.0f, "%.0f");
    ui::slider("Pas max par rayon", &app.maxSteps, 50, 600, "%d",
               "Garde-fou. Un rayon qui s'échappe prend environ 20 pas, un rayon "
               "qui tourne autour de la sphère de photons environ 30.");
    ui::subheading("Mesures");
    ui::value("Image calculée", "%d × %d", gStats.traceWidth, gStats.traceHeight);
    ui::value("Temps GPU par image", "%.1f ms", gStats.gpuMs);
}
UI_SECTION(ui::Tab::Render, 10, "Performance", performanceCard);

void imageCard(App& app)
{
    ui::slider("Exposition", &app.exposure, 0.2f, 4.0f, "%.2f", nullptr, true);
    if (ui::button("Recharger les shaders"))
        app.reloadRequested = true;
    ImGui::SetItemTooltip("Touche R : relit les fichiers .frag sans relancer.");
}
UI_SECTION(ui::Tab::Render, 20, "Image", imageCard);

// --- Onglet Aide -----------------------------------------------------------------

// Touche dessinée comme une petite touche de clavier.
void keycap(const char* key)
{
    ImGui::PushFont(ui::boldFont());
    const ImVec2 ts = ImGui::CalcTextSize(key);
    const float padX = ImGui::GetFontSize() * 0.4f;
    const float h = ImGui::GetTextLineHeight() + 4.0f;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const ImVec2 size(std::max(ts.x + 2.0f * padX, h), h);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + size.x, p.y + size.y), ImGui::GetColorU32(ImGuiCol_FrameBg), 5.0f);
    dl->AddRect(p, ImVec2(p.x + size.x, p.y + size.y), ImGui::GetColorU32(ImGuiCol_Border), 5.0f);
    dl->AddText(ImVec2(p.x + (size.x - ts.x) * 0.5f, p.y + (size.y - ts.y) * 0.5f),
                ImGui::GetColorU32(ImGuiCol_Text), key);
    ImGui::Dummy(size);
    ImGui::PopFont();
}

void shortcutRow(std::initializer_list<const char*> keys, const char* action)
{
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    bool first = true;
    for (const char* k : keys) {
        if (!first) ImGui::SameLine(0.0f, 4.0f);
        keycap(k);
        first = false;
    }
    ImGui::TableNextColumn();
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2.0f);   // centré sur la touche
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::color::muted), "%s", action);
}

void helpTab()
{
    ui::beginCard("Souris");
    ui::value("Clic gauche + glisser", "tourner autour");
    ui::value("Molette", "zoom");
    ui::endCard();

    ui::beginCard("Clavier");
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(ImGui::GetFontSize() * 0.5f, 2.0f));
    if (ImGui::BeginTable("##touches", 2, ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("touches", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("action", ImGuiTableColumnFlags_WidthStretch);
        shortcutRow({"F1"}, "cacher ce panneau");
        shortcutRow({"Espace"}, "orbite automatique");
        shortcutRow({"P"}, "pause");
        shortcutRow({"+", "-"}, "vitesse du temps");
        shortcutRow({"C"}, "recentrer");
        shortcutRow({"E"}, "trou noir / étoile");
        shortcutRow({"N", "B"}, "étoile suivante / préc.");
        shortcutRow({"I", "U"}, "étoile plus / moins lourde");
        shortcutRow({"H"}, "disque d'accrétion");
        shortcutRow({"J"}, "jets du quasar");
        shortcutRow({"F", "G", "X"}, "champ / astéroïde / retirer");
        shortcutRow({"K", "L", "O"}, "résolution");
        shortcutRow({"R"}, "recharger les shaders");
        shortcutRow({"Échap"}, "quitter");
        ImGui::EndTable();
    }
    ImGui::PopStyleVar();
    ui::note("Flèches ou ZQSD : tourner. Page haut / bas : zoom.");
    ui::endCard();
}

// --- Éléments fixes du panneau ---------------------------------------------------

ImU32 fpsColor(int fps, float target)
{
    if (fps >= target * 0.9f) return ui::color::good;
    if (fps >= target * 0.6f) return ui::color::warn;
    return ui::color::bad;
}

void header(App& app)
{
    ImGui::PushFont(ui::boldFont());
    ImGui::SetWindowFontScale(1.25f);
    ImGui::TextUnformatted("Simulateur");
    ImGui::SetWindowFontScale(1.0f);
    ImGui::PopFont();
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::color::muted), "Relativité générale · ray tracing");

    // Bouton × pour cacher le panneau.
    const float s = ImGui::GetFrameHeight();
    const ImVec2 cursor = ImGui::GetCursorPos();
    ImGui::SetCursorPos(ImVec2(ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x - s,
                               ImGui::GetStyle().WindowPadding.y));
    if (ImGui::InvisibleButton("##fermer", ImVec2(s, s)))
        app.showUi = false;
    const bool hovered = ImGui::IsItemHovered();
    ImGui::SetItemTooltip("Cacher le panneau (F1)");
    const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (hovered) dl->AddRectFilled(a, b, ImGui::GetColorU32(ImGuiCol_FrameBgHovered), s * 0.5f);
    const float m = s * 0.32f;
    const ImU32 xc = hovered ? ImGui::GetColorU32(ImGuiCol_Text) : ui::color::muted;
    dl->AddLine(ImVec2(a.x + m, a.y + m), ImVec2(b.x - m, b.y - m), xc, 1.6f);
    dl->AddLine(ImVec2(b.x - m, a.y + m), ImVec2(a.x + m, b.y - m), xc, 1.6f);
    ImGui::SetCursorPos(cursor);
    ImGui::Dummy(ImVec2(0.0f, 2.0f));
}

void playBar(App& app)
{
    // Gros bouton lecture / pause.
    const float d = ImGui::GetFontSize() * 2.8f;
    if (ImGui::InvisibleButton("##lecture", ImVec2(d, d)))
        app.paused = !app.paused;
    ImGui::SetItemTooltip(app.paused ? "Reprendre (P)" : "Pause (P)");
    const bool hovered = ImGui::IsItemHovered();
    const ImVec2 a = ImGui::GetItemRectMin();
    const ImVec2 c(a.x + d * 0.5f, a.y + d * 0.5f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddCircleFilled(c, d * 0.5f, hovered ? IM_COL32(255, 178, 102, 255) : ui::color::accent);
    const ImU32 ic = IM_COL32(24, 16, 8, 255);
    const float k = d * 0.17f;
    if (app.paused) {
        dl->AddTriangleFilled(ImVec2(c.x - k * 0.8f, c.y - k * 1.15f), ImVec2(c.x - k * 0.8f, c.y + k * 1.15f),
                              ImVec2(c.x + k * 1.25f, c.y), ic);
    } else {
        dl->AddRectFilled(ImVec2(c.x - k * 0.95f, c.y - k), ImVec2(c.x - k * 0.25f, c.y + k), ic, 1.5f);
        dl->AddRectFilled(ImVec2(c.x + k * 0.25f, c.y - k), ImVec2(c.x + k * 0.95f, c.y + k), ic, 1.5f);
    }

    ImGui::SameLine(0.0f, ImGui::GetFontSize() * 0.9f);
    ImGui::BeginGroup();
    ui::slider("Vitesse du temps", &app.simSpeed, 0.25f, 200.0f, "× %.3g",
               "Temps simulé (en rs/c) par seconde d'écran. Touches + et -.", true);
    ImGui::EndGroup();

    // Temps écoulé et remise à zéro.
    char buf[64];
    if (app.starMode) {
        std::snprintf(buf, sizeof(buf), "t = %.0f s", app.simTime / 10.0);
    } else {
        char real[32];
        formatDuration(app.simTime * kSunRsOverCSeconds * app.massSolar, real, sizeof(real));
        std::snprintf(buf, sizeof(buf), "t = %.0f rs/c  ·  %s réels", app.simTime, real);
    }
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::color::muted), "%s", buf);
    const char* reset = "Remettre à 0";
    const float bw = ImGui::CalcTextSize(reset).x + ImGui::GetStyle().FramePadding.x * 2.0f;
    ImGui::SameLine(ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x - bw);
    if (ImGui::Button(reset))
        app.simTime = 0.0;
}

void sceneSelector(App& app)
{
    const auto& scenes = ui::scenes();
    std::vector<const char*> names;
    int current = -1;
    for (int i = 0; i < int(scenes.size()); ++i) {
        names.push_back(scenes[i].name);
        if (current < 0 && scenes[i].isActive && scenes[i].isActive(app)) current = i;
    }
    int selected = current;
    if (ui::segmented("scene", &selected, names.data(), int(names.size())) && scenes[selected].select)
        scenes[selected].select(app);
}

// Onglets soulignés.
void tabBar()
{
    static const char* const tabs[] = {"Objet", "Vue", "Rendu", "Aide"};
    const int count = 4;
    const float w = ImGui::GetContentRegionAvail().x;
    const float h = ImGui::GetFrameHeight() + 2.0f;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddLine(ImVec2(p.x, p.y + h - 1.0f), ImVec2(p.x + w, p.y + h - 1.0f), ImGui::GetColorU32(ImGuiCol_Separator), 1.0f);
    const float tw = w / count;
    for (int i = 0; i < count; ++i) {
        const ImVec2 a(p.x + tw * i, p.y);
        ImGui::SetCursorScreenPos(a);
        ImGui::PushID(i);
        if (ImGui::InvisibleButton("##onglet", ImVec2(tw, h))) gTab = i;
        const bool hovered = ImGui::IsItemHovered();
        ImGui::PopID();
        const bool selected = gTab == i;
        if (selected) ImGui::PushFont(ui::boldFont());
        const ImVec2 ts = ImGui::CalcTextSize(tabs[i]);
        dl->AddText(ImVec2(a.x + (tw - ts.x) * 0.5f, a.y + (h - ts.y) * 0.5f - 1.0f),
                    selected ? ImGui::GetColorU32(ImGuiCol_Text)
                             : (hovered ? IM_COL32(200, 205, 215, 255) : ui::color::muted),
                    tabs[i]);
        if (selected) {
            ImGui::PopFont();
            dl->AddRectFilled(ImVec2(a.x + tw * 0.18f, a.y + h - 3.0f), ImVec2(a.x + tw * 0.82f, a.y + h),
                              ui::color::accent, 1.5f);
        }
    }
    ImGui::SetCursorScreenPos(p);
    ImGui::Dummy(ImVec2(w, h));
}

void tabContent(App& app)
{
    if (gTab == 3) {
        helpTab();
        return;
    }
    const ui::Tab tab = static_cast<ui::Tab>(gTab);
    bool any = false;
    for (const ui::Section& s : ui::sections()) {
        if (s.tab != tab || (s.visible && !s.visible(app))) continue;
        ImGui::PushID(s.title);
        ui::beginCard(s.title);
        s.draw(app);
        ui::endCard();
        ImGui::PopID();
        any = true;
    }
    if (!any) ui::note("Rien à régler ici pour cette scène.");
}

void footer(const App& app)
{
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x;
    ImGui::GetWindowDrawList()->AddLine(ImVec2(p.x, p.y), ImVec2(p.x + w, p.y),
                                        ImGui::GetColorU32(ImGuiCol_Separator), 1.0f);
    ImGui::Dummy(ImVec2(0.0f, 4.0f));

    const ImU32 c = fpsColor(gStats.fps, app.targetFps);
    const float r = ImGui::GetFontSize() * 0.28f;
    const ImVec2 q = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddCircleFilled(
        ImVec2(q.x + r, q.y + ImGui::GetTextLineHeight() * 0.5f), r, c);
    ImGui::Dummy(ImVec2(r * 2.0f, ImGui::GetTextLineHeight()));
    ImGui::SameLine();
    ImGui::PushFont(ui::boldFont());
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(c), "%d FPS", gStats.fps);
    ImGui::PopFont();
    ImGui::SameLine();
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::color::muted), "·  %d × %d  ·  GPU %.1f ms",
                       gStats.traceWidth, gStats.traceHeight, gStats.gpuMs);
}

void panel(App& app)
{
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float width = std::min(ImGui::GetFontSize() * 23.0f, vp->WorkSize.x * 0.5f);
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(ImVec2(width, vp->WorkSize.y));
    ImGui::Begin("##panneau", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                 ImGuiWindowFlags_NoBringToFrontOnFocus);
    // Liseré à droite du panneau.
    const ImVec2 wp = ImGui::GetWindowPos(), ws = ImGui::GetWindowSize();
    ImGui::GetWindowDrawList()->AddLine(ImVec2(wp.x + ws.x - 1.0f, wp.y), ImVec2(wp.x + ws.x - 1.0f, wp.y + ws.y),
                                        ImGui::GetColorU32(ImGuiCol_Border), 1.0f);

    header(app);
    playBar(app);
    ImGui::Dummy(ImVec2(0.0f, 2.0f));
    sceneSelector(app);
    tabBar();

    const float footerH = ImGui::GetTextLineHeightWithSpacing() + 12.0f;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
    ImGui::BeginChild("##contenu", ImVec2(0.0f, -footerH));
    ImGui::PopStyleColor();
    ImGui::Dummy(ImVec2(0.0f, 2.0f));
    tabContent(app);
    ImGui::EndChild();
    footer(app);
    ImGui::End();
}

// Panneau caché : petite pastille cliquable en haut à gauche.
void collapsedPill(App& app)
{
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float m = ImGui::GetFontSize() * 0.8f;
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + m, vp->WorkPos.y + m));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, ImGui::GetFontSize());
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(ImGui::GetFontSize() * 0.8f, ImGui::GetFontSize() * 0.4f));
    ImGui::SetNextWindowBgAlpha(0.75f);
    ImGui::Begin("##pastille", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);
    const ImVec2 start = ImGui::GetCursorScreenPos();
    ImGui::BeginGroup();
    const ImU32 c = fpsColor(gStats.fps, app.targetFps);
    const float r = ImGui::GetFontSize() * 0.28f;
    const ImVec2 q = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(q.x + r, q.y + ImGui::GetTextLineHeight() * 0.5f), r, c);
    ImGui::Dummy(ImVec2(r * 2.0f, ImGui::GetTextLineHeight()));
    ImGui::SameLine();
    ImGui::Text("%d FPS", gStats.fps);
    ImGui::SameLine(0.0f, ImGui::GetFontSize());
    ImGui::PushFont(ui::boldFont());
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::color::accent), "F1");
    ImGui::PopFont();
    ImGui::SameLine();
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::color::muted), "Panneau");
    ImGui::EndGroup();
    const ImVec2 end = ImGui::GetItemRectMax();
    ImGui::SetCursorScreenPos(start);
    if (ImGui::InvisibleButton("##ouvrir", ImVec2(end.x - start.x, end.y - start.y)))
        app.showUi = true;
    ImGui::End();
    ImGui::PopStyleVar(2);
}

} // namespace

void uiInit(GLFWwindow* window)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;   // pas de fichier imgui.ini à côté du programme

    // Écrans haute densité : on agrandit police et marges.
    float xs = 1.0f, ys = 1.0f;
    glfwGetWindowContentScale(window, &xs, &ys);
    const float scale = std::max(1.0f, xs);

    ui::applyTheme(scale);
    ui::loadFonts(scale);

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330 core");
}

void uiShutdown()
{
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

bool uiWantsMouse() { return ImGui::GetIO().WantCaptureMouse; }
bool uiWantsKeyboard() { return ImGui::GetIO().WantCaptureKeyboard; }

void uiDraw(App& app, const UiStats& stats)
{
    gStats = stats;
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    if (app.showUi) panel(app);
    else collapsedPill(app);

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}
