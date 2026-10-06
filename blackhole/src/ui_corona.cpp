// Cartes du panneau pour la couronne, le vent, les protubérances et les
// éjections de masse coronale (src/corona.cpp).

#include "ui_kit.hpp"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>

namespace {

// Ligne étiquette / valeur, avec une explication au survol de la ligne.
void info(const char* label, const char* help, const char* fmt, ...) IM_FMTARGS(3);
void info(const char* label, const char* help, const char* fmt, ...)
{
    char buf[160];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    ui::value(label, "%s", buf);
    if (help && ImGui::IsMouseHoveringRect(start, ImVec2(start.x + width, ImGui::GetItemRectMax().y))) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 26.0f);
        ImGui::TextUnformatted(help);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

bool isNormalStar(const App& app) { return app.starMode && app.star.radius >= 0.02; }

bool isActiveStar(const App& app)
{
    return app.starMode && computeCorona(app.star, computeActivity(app.star)).active;
}

void coronaWindCard(App& app)
{
    const StellarActivity act = computeActivity(app.star);
    const CoronaModel cor = computeCorona(app.star, act);

    if (cor.active) {
        ui::toggle("Afficher la couronne", &app.showCorona,
                   "Lumière de l'étoile diffusée par les électrons de la couronne. "
                   "En vrai un million de fois plus faible que la surface : on ne la "
                   "voit que pendant une éclipse totale. Ici elle est renforcée.");
        info("Température de la couronne",
             "T = 0,11 F_X^0,26 millions de K (Johnstone & Güdel 2015), avec F_X le flux "
             "de rayons X à la surface. Bien plus chaude que la surface : elle est "
             "chauffée par le champ magnétique.", "%.2g millions de K", cor.temperatureMK);
        info("Flux X à la surface", "F_X = (L_X / L_bol) x sigma T^4.", "%.2g erg/cm²/s", cor.xrayFlux);
    } else {
        if (act.active())
            ui::note("Géante froide, au-delà de la « ligne de partage coronale » "
                     "(Linsky & Haisch 1979) : plus de couronne chaude, ni "
                     "protubérances, ni éjections. À la place, un vent lent et très "
                     "massif qui forme de la poussière autour de l'étoile.");
        else
            ui::note("Pas de couronne chaude : sans enveloppe convective il n'y a pas "
                     "de dynamo pour la chauffer, ni protubérances, ni éjections.");
    }

    ui::subheading("Vent stellaire");
    info("Mécanisme", "Naines actives : la couronne chaude s'évapore (vent de Parker). "
         "Géantes froides : loi de Reimers. Étoiles chaudes : la lumière pousse le "
         "gaz par ses raies d'absorption.", "%s", cor.windKind);
    info("Vitesse", "Loin de l'étoile. Soleil : 400 (vent lent) à 750 km/s (vent rapide).",
         "%.0f km/s", cor.windKms);
    info("Perte de masse", "Soleil : 2e-14 masse solaire par an. Naines : proportionnelle "
         "à R² F_X^1,34 (Wood et al. 2005).",
         "%.2g Soleil/an  (%.2g × Soleil)", cor.massLoss, cor.massLoss / 2.0e-14);
    if (cor.windKms > 0.0) {
        const double days = 1.496e8 / cor.windKms / 86400.0;
        info("Temps pour parcourir 1 UA", nullptr, "%.3g jours", days);
    }
}

void prominenceCard(App& app)
{
    const StellarActivity act = computeActivity(app.star);
    const CoronaModel cor = computeCorona(app.star, act);
    const CoronaSimulator& sim = app.corona;

    ui::toggle("Protubérances", &app.showProminences,
               "Nuages de gaz froid (~8 000 K) tenus par le champ magnétique au-dessus "
               "de la surface, cent fois plus denses que la couronne. Roses au bord "
               "(raie H-alpha), sombres devant le disque (filaments).");
    ui::toggle("Éjections de masse coronale", &app.showCmes,
               "Une protubérance se déstabilise, ou une grosse éruption éclate : des "
               "milliards de tonnes de plasma partent dans l'espace. Front brillant, "
               "cavité sombre, cœur rose (la protubérance).");

    info("Protubérances visibles", "De 1 (étoile calme) à 6 (étoile couverte de taches). "
         "Dans les bandes de taches, et vers les pôles (« couronne polaire »).",
         "%d", sim.prominenceCount());
    info("Rayon de corotation", "Orbite qui tourne avec l'étoile : r = (G M P² / 4 pi²)^1/3. "
         "Quand il est proche (rotation rapide), du gaz froid y reste piégé et tourne "
         "avec l'étoile (« slingshot prominences », AB Doradus).",
         "%.3g rayons%s", cor.corotationRadius, cor.slingshot ? "  (nuages piégés)" : "");
    info("Éjections", "Soleil : ~0,5 par jour au minimum, ~5 au maximum (Yashiro et al. "
         "2004). Autres étoiles : proportionnel à F_X^0,7.",
         "%.3g par jour", cor.cmePerDay);
    info("Qui s'échappent", "Sur les étoiles très actives, le champ magnétique retient une "
         "partie des éjections, qui retombent (Alvarado-Gómez et al. 2018).",
         "%.0f %%", 100.0 * cor.cmeEscape);
    info("Lancées / retenues", nullptr, "%d / %d", sim.cmeTotal(), sim.cmeFailed());
    if (ui::button("Lancer une éjection (M)", true)) {
        const double days = starDays(app);
        app.corona.launchCme(app.star, act, days, starRotationAngle(app.star, days));
    }
    ui::note("Le temps est accéléré : une éjection met en vrai des heures à "
             "quitter l'étoile, ici quelques secondes. Seules les plus grosses sont "
             "montrées (au plus une toutes les 8 s).");
}

} // namespace

UI_SECTION(ui::Tab::Object, 72, "Couronne et vent", coronaWindCard, isNormalStar);
UI_SECTION(ui::Tab::Object, 75, "Protubérances et éjections", prominenceCard, isActiveStar);
