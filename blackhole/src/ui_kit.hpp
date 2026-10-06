// Kit d'interface du panneau de contrôle : thème, widgets et sections.
//
// Ajouter une section au panneau, depuis n'importe quel fichier .cpp :
//
//     #include "ui_kit.hpp"
//
//     static void drawActivity(App& app)
//     {
//         ui::slider("Activité", &app.star.activity, 0.0, 1.0, "%.2f",
//                    "Taches et éruptions : 0 = calme, 1 = très active.");
//         ui::toggle("Éruptions", &app.flares, "Affiche les éruptions.");
//         ui::value("Taches visibles", "%d", spotCount);
//     }
//     UI_SECTION(ui::Tab::Object, 50, "Activité", drawActivity,
//                [](const App& a) { return a.starMode; });
//
// La section apparaît dans l'onglet choisi, triée par "order" (petit = en
// haut), dans une carte titrée. Le dernier argument (facultatif) dit quand
// l'afficher. Une nouvelle scène s'ajoute au sélecteur du haut avec
//     UI_SCENE("Pulsar", isPulsar, selectPulsar);
// isPulsar(const App&) dit si elle est active, selectPulsar(App&) l'active.

#pragma once

#include "app.hpp"

#include <imgui.h>

#include <functional>
#include <vector>

struct GLFWwindow;

namespace ui {

enum class Tab { Object, View, Render };

using DrawFn = std::function<void(App&)>;
using VisibleFn = std::function<bool(const App&)>;
using SelectFn = std::function<void(App&)>;

struct Section {
    Tab tab;
    int order;
    const char* title;
    DrawFn draw;
    VisibleFn visible;   // vide = toujours visible
};

struct Scene {
    const char* name;
    VisibleFn isActive;
    SelectFn select;
};

// Enregistrement (utilisé par les macros ; renvoie true pour pouvoir
// initialiser une variable statique).
bool registerSection(Tab tab, int order, const char* title, DrawFn draw, VisibleFn visible = {});
bool registerScene(const char* name, VisibleFn isActive, SelectFn select);
const std::vector<Section>& sections();   // triées par onglet puis ordre
const std::vector<Scene>& scenes();

// Thème et polices (appelé une fois par uiInit).
void applyTheme(float scale);
void loadFonts(float scale);
ImFont* boldFont();   // Inter SemiBold, ou la police par défaut

// Couleurs du thème.
namespace color {
inline constexpr ImU32 accent = IM_COL32(255, 159, 67, 255);     // orange chaud
inline constexpr ImU32 good = IM_COL32(80, 200, 120, 255);
inline constexpr ImU32 warn = IM_COL32(255, 196, 70, 255);
inline constexpr ImU32 bad = IM_COL32(240, 90, 80, 255);
inline constexpr ImU32 muted = IM_COL32(140, 148, 165, 255);
}

// Widgets. Tous prennent toute la largeur, avec l'étiquette au-dessus ;
// "help" (facultatif) ajoute une bulle d'aide au survol de l'étiquette.
// Renvoient true quand la valeur change.
bool slider(const char* label, float* v, float lo, float hi, const char* fmt = "%.2f",
            const char* help = nullptr, bool log = false);
bool slider(const char* label, double* v, double lo, double hi, const char* fmt = "%.3g",
            const char* help = nullptr, bool log = false);
bool slider(const char* label, int* v, int lo, int hi, const char* fmt = "%d",
            const char* help = nullptr);
bool toggle(const char* label, bool* v, const char* help = nullptr);
bool button(const char* label, bool primary = false);   // pleine largeur
bool segmented(const char* id, int* current, const char* const* items, int count);

// Ligne "étiquette ........ valeur" pour les résultats calculés.
void value(const char* label, const char* fmt, ...) IM_FMTARGS(2);
// Sous-titre discret à l'intérieur d'une carte.
void subheading(const char* text);
// Texte d'aide grisé, sur plusieurs lignes.
void note(const char* text);
// Pastille de couleur de corps noir.
ImVec4 blackbodyColor(double temperature);

// Carte titrée (utilisée par le panneau pour chaque section).
void beginCard(const char* title);
void endCard();

} // namespace ui

#define UI_CONCAT_(a, b) a##b
#define UI_CONCAT(a, b) UI_CONCAT_(a, b)
#define UI_SECTION(tab, order, title, ...) \
    static const bool UI_CONCAT(uiSection_, __LINE__) = ::ui::registerSection(tab, order, title, __VA_ARGS__)
#define UI_SCENE(name, isFn, selectFn) \
    static const bool UI_CONCAT(uiScene_, __LINE__) = ::ui::registerScene(name, isFn, selectFn)
