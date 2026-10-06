// Onglet "Touches" du panneau : chaque touche du simulateur, rangée par
// thème, avec une recherche. Les touches qui ne font rien dans la scène
// affichée (par exemple J autour d'une étoile) sont grisées, avec la scène
// où elles servent. Aussi : le message bref affiché en bas de l'écran quand
// on appuie sur une touche (app.toast, rempli par main.cpp).
//
// Pour ajouter une touche : la gérer dans onChar() (main.cpp), puis
// l'ajouter au tableau kBindings ci-dessous.
//
// Les polices ne contiennent que l'alphabet latin (Latin-1) : pas de
// flèches Unicode dans les libellés.

#include "ui_kit.hpp"

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>

namespace {

// Dans quelle scène la touche a un effet.
enum class Scope { All, BlackHole, Star, Paused };

struct Binding {
    const char* group;
    const char* keys[4];   // terminé par nullptr
    const char* action;
    const char* detail;    // bulle d'aide (facultatif)
    Scope scope = Scope::All;
};

const Binding kBindings[] = {
    {"Caméra", {"Clic gauche", nullptr}, "glisser pour tourner", nullptr},
    {"Caméra", {"Molette", nullptr}, "zoom", nullptr},
    {"Caméra", {"Flèches", nullptr}, "tourner autour", "Gauche / droite : autour de l'axe. Haut / bas : hauteur."},
    {"Caméra", {"Z", "Q", "S", "D", }, "tourner (AZERTY)", "Lues par position : W A S D sur un clavier QWERTY."},
    {"Caméra", {"Page haut", "Page bas", nullptr}, "zoom avant / arrière", nullptr},
    {"Caméra", {"Espace", nullptr}, "orbite automatique", "Vitesse réglable dans l'onglet Vue."},
    {"Caméra", {"C", nullptr}, "recentrer la vue", nullptr},

    {"Temps", {"P", nullptr}, "pause / lecture", nullptr},
    {"Temps", {"T", nullptr}, "avancer d'un pas", "Un pas = 1/30 s à la vitesse du temps choisie.", Scope::Paused},
    {"Temps", {"+", "-", nullptr}, "accélérer / ralentir le temps", "De × 0,25 à × 200."},

    {"Scène", {"E", nullptr}, "trou noir / étoile", nullptr},
    {"Scène", {"N", "B", nullptr}, "étoile suivante / précédente", "Soleil, Proxima, Sirius, Rigel, Bételgeuse, pulsars...", Scope::Star},
    {"Scène", {"I", "U", nullptr}, "étoile plus / moins massive", "Étoile de la séquence principale, masse × 1,25 ou / 1,25.", Scope::Star},
    {"Scène", {"M", nullptr}, "éjection de masse coronale", "La plus haute protubérance éclate et une bulle de plasma part dans l'espace. Étoiles actives seulement (taches, éruptions).", Scope::Star},

    {"Trou noir", {"H", nullptr}, "disque d'accrétion", nullptr, Scope::BlackHole},
    {"Trou noir", {"J", nullptr}, "jets relativistes", "Plasma éjecté le long de l'axe (quasar). Seulement autour du trou noir.", Scope::BlackHole},
    {"Trou noir", {"V", nullptr}, "lentille gravitationnelle", "Coupe la déviation de la lumière pour comparer : les rayons vont tout droit. Marche aussi pour une étoile à neutrons."},

    {"Astéroïdes", {"F", nullptr}, "ajouter un champ", "Réglages du champ : onglet Objet, carte Astéroïdes."},
    {"Astéroïdes", {"G", nullptr}, "ajouter un astéroïde", "Orbite circulaire au milieu du champ."},
    {"Astéroïdes", {"X", nullptr}, "tout retirer", nullptr},

    {"Rendu", {"K", "L", nullptr}, "baisser / monter la résolution", "Coupe la résolution automatique."},
    {"Rendu", {"O", nullptr}, "résolution automatique", "Ajuste la résolution pour tenir les images par seconde visées."},
    {"Rendu", {"R", nullptr}, "recharger les shaders", "Après une modification des fichiers .frag."},

    {"Application", {"F1", "Tab", nullptr}, "afficher / cacher le panneau", nullptr},
    {"Application", {"Échap", nullptr}, "quitter", nullptr},
};

const char* const kGroups[] = {"Caméra", "Temps", "Scène", "Trou noir", "Astéroïdes", "Rendu", "Application"};

bool applies(Scope s, const App& app)
{
    switch (s) {
    case Scope::BlackHole: return !app.starMode;
    case Scope::Star: return app.starMode;
    case Scope::Paused: return app.paused;
    default: return true;
    }
}

const char* scopeHint(Scope s)
{
    switch (s) {
    case Scope::BlackHole: return "trou noir";
    case Scope::Star: return "étoile";
    case Scope::Paused: return "en pause";
    default: return "";
    }
}

std::string lower(const char* s)
{
    std::string out(s ? s : "");
    for (char& c : out) c = char(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

bool matches(const Binding& b, const std::string& query)
{
    if (query.empty()) return true;
    if (lower(b.action).find(query) != std::string::npos) return true;
    if (lower(b.group).find(query) != std::string::npos) return true;
    for (const char* k : b.keys) {
        if (!k) break;
        if (lower(k) == query) return true;
    }
    return false;
}

// Touche dessinée comme une petite touche de clavier.
void keycap(const char* key, bool enabled)
{
    ImGui::PushFont(ui::boldFont());
    const ImVec2 ts = ImGui::CalcTextSize(key);
    const float padX = ImGui::GetFontSize() * 0.4f;
    const float h = ImGui::GetTextLineHeight() + 4.0f;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const ImVec2 size(std::max(ts.x + 2.0f * padX, h), h);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + size.x, p.y + size.y), ImGui::GetColorU32(ImGuiCol_FrameBg), 5.0f);
    dl->AddRect(p, ImVec2(p.x + size.x, p.y + size.y),
                enabled ? ui::color::accent : ImGui::GetColorU32(ImGuiCol_Border), 5.0f);
    dl->AddText(ImVec2(p.x + (size.x - ts.x) * 0.5f, p.y + (size.y - ts.y) * 0.5f),
                enabled ? ImGui::GetColorU32(ImGuiCol_Text) : ui::color::muted, key);
    ImGui::Dummy(size);
    ImGui::PopFont();
}

void bindingRow(const Binding& b, const App& app)
{
    const bool on = applies(b.scope, app);
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    bool first = true;
    for (const char* k : b.keys) {
        if (!k) break;
        if (!first) ImGui::SameLine(0.0f, 4.0f);
        keycap(k, on);
        first = false;
    }
    ImGui::TableNextColumn();
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2.0f);   // centré sur la touche
    const ImVec4 text = on ? ImGui::GetStyleColorVec4(ImGuiCol_Text)
                           : ImGui::ColorConvertU32ToFloat4(ui::color::muted);
    ImGui::PushStyleColor(ImGuiCol_Text, text);
    if (on || b.scope == Scope::All)
        ImGui::TextWrapped("%s", b.action);
    else
        ImGui::TextWrapped("%s (%s)", b.action, scopeHint(b.scope));
    ImGui::PopStyleColor();
    if (b.detail) ImGui::SetItemTooltip("%s", b.detail);
}

} // namespace

namespace ui {

void keysTab(App& app)
{
    static char search[48] = "";
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##chercher", "Chercher une touche ou une action", search, sizeof(search));
    const std::string query = lower(search);
    ui::note("Les touches de lettres suivent la disposition du clavier (AZERTY "
             "ou QWERTY). En orange : actives dans la scène affichée. Survolez "
             "une action pour plus de détails.");

    bool any = false;
    for (const char* group : kGroups) {
        bool found = false;
        for (const Binding& b : kBindings)
            found |= std::strcmp(b.group, group) == 0 && matches(b, query);
        if (!found) continue;
        any = true;
        ui::beginCard(group);
        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(ImGui::GetFontSize() * 0.5f, 2.0f));
        if (ImGui::BeginTable(group, 2, ImGuiTableFlags_SizingFixedFit)) {
            ImGui::TableSetupColumn("touches", ImGuiTableColumnFlags_WidthFixed);
            ImGui::TableSetupColumn("action", ImGuiTableColumnFlags_WidthStretch);
            for (const Binding& b : kBindings)
                if (std::strcmp(b.group, group) == 0 && matches(b, query)) bindingRow(b, app);
            ImGui::EndTable();
        }
        ImGui::PopStyleVar();
        ui::endCard();
    }
    if (!any) ui::note("Aucune touche ne correspond.");
}

void toastOverlay(App& app)
{
    app.toastAge += ImGui::GetIO().DeltaTime;
    const float kShow = 1.8f, kFade = 0.4f;
    if (app.toast.empty() || app.toastAge > kShow + kFade) return;
    const float alpha = app.toastAge < kShow ? 1.0f : 1.0f - (app.toastAge - kShow) / kFade;

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImFont* font = ui::boldFont();
    const float fs = ImGui::GetFontSize();
    const ImVec2 ts = font->CalcTextSizeA(fs, FLT_MAX, 0.0f, app.toast.c_str());
    const ImVec2 pad(fs * 0.9f, fs * 0.5f);
    const ImVec2 size(ts.x + 2.0f * pad.x, ts.y + 2.0f * pad.y);
    // En bas, centré sur la partie de l'image que le panneau ne couvre pas.
    const float left = app.showUi ? std::min(fs * 23.0f, vp->WorkSize.x * 0.5f) : 0.0f;
    const ImVec2 a(vp->WorkPos.x + left + (vp->WorkSize.x - left - size.x) * 0.5f,
                   vp->WorkPos.y + vp->WorkSize.y - size.y - fs * 1.5f);
    const ImVec2 b(a.x + size.x, a.y + size.y);
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    dl->AddRectFilled(a, b, IM_COL32(20, 22, 30, int(215 * alpha)), size.y * 0.5f);
    dl->AddRect(a, b, IM_COL32(255, 159, 67, int(180 * alpha)), size.y * 0.5f);
    dl->AddText(font, fs, ImVec2(a.x + pad.x, a.y + pad.y), IM_COL32(240, 242, 248, int(255 * alpha)),
                app.toast.c_str());
}

} // namespace ui
