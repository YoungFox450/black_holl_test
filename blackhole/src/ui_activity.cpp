// Cartes « activité de l'étoile » du panneau : dynamo, cycle et taches,
// éruptions, oscillations. Les grandeurs viennent de src/activity.cpp.

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
    char buf[256];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    ImVec2 start = ImGui::GetCursorScreenPos();
    float width = ImGui::GetContentRegionAvail().x;
    ui::value(label, "%s", buf);
    if (help && ImGui::IsMouseHoveringRect(start, ImVec2(start.x + width, ImGui::GetItemRectMax().y))) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 26.0f);
        ImGui::TextUnformatted(help);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

// "5,4 min", "105 jours", "11 ans"...
void formatTime(double seconds, char* out, size_t size)
{
    if (seconds >= 2.0 * 365.25 * 86400.0) std::snprintf(out, size, "%.3g ans", seconds / (365.25 * 86400.0));
    else if (seconds >= 2.0 * 86400.0) std::snprintf(out, size, "%.3g jours", seconds / 86400.0);
    else if (seconds >= 7200.0) std::snprintf(out, size, "%.3g h", seconds / 3600.0);
    else std::snprintf(out, size, "%.3g min", seconds / 60.0);
}

bool isStar(const App& app) { return app.starMode; }

bool hasDynamo(const App& app)
{
    return app.starMode && computeActivity(app.star).active();
}

bool oscillates(const App& app)
{
    return app.starMode && computeActivity(app.star).oscAmplitude > 0.0;
}

void dynamoCard(App& app)
{
    const Star& star = app.star;
    const StellarActivity act = computeActivity(star);

    char t[32];
    formatTime(starDays(app) * 86400.0, t, sizeof(t));
    info("Temps de l'étoile", "Une seconde affichée vaut 2 jours (vitesse x10).", "%s", t);

    if (star.radius < 0.02) {
        ui::note("Objet dégénéré : plus de fusion ni de convection, donc ni taches, "
                 "ni cycle, ni éruptions de type solaire.");
        return;
    }
    if (act.convective <= 0.0) {
        ui::note("Pas de dynamo : au-dessus de ~6 700 K l'enveloppe est radiative "
                 "(cassure de Kraft). Ni taches, ni cycle, ni éruptions.");
        return;
    }
    info("Retournement convectif", "Temps que met une cellule de convection pour remonter. "
         "Naines : log tau_c = 2,33 - 1,50 M + 0,31 M² (Wright et al. 2018). "
         "Géantes : ~150 jours (Gunn et al. 1998).", "%.3g jours", act.turnoverDays);
    info("Nombre de Rossby", "Ro = P_rot / tau_c. Petit = rotation rapide devant la "
         "convection = dynamo efficace. Sous 0,13 l'activité plafonne (saturée).",
         "%.3g%s", act.rossby, act.saturated ? " (saturé)" : "");
    info("Rayons X  L_X / L_bol", "Relation rotation-activité (Wright et al. 2011) : "
         "10^-3,13 si Ro < 0,13, sinon 10^-3,13 (Ro / 0,13)^-2,7.", "%.2g", act.xrayRatio);
    const double pole = star.rotationDays / std::max(1.0 - act.shearRatio, 1e-6);
    info("Rotation équateur / pôles", "Rotation différentielle Omega(lat) = Omega_eq "
         "(1 - alpha sin² lat). Cisaillement 0,073 rad/jour x (T / 5772 K)^8,6 "
         "(Collier Cameron 2007) : fort pour les étoiles F, quasi nul pour les naines M.",
         "%.3g / %.3g j", star.rotationDays, pole);
}

void cycleCard(App& app)
{
    const StellarActivity act = computeActivity(app.star);
    const CycleState cyc = cycleState(act, cyclePhase(app, act));

    if (act.cycleYears > 0.0) {
        info("Période du cycle", "Branche « inactive » de Böhm-Vitense (2007) : "
             "P_cyc = 158 P_rot (11 ans pour le Soleil). Forme du cycle : Hathaway "
             "(1994), montée rapide puis descente lente.", "%.3g ans", act.cycleYears);
        double phase = cyclePhase(app, act);
        if (ui::slider("Phase du cycle", &phase, 0.0, 0.999, "%.2f", "0 = minimum, maximum vers 0,4.")) {
            double p = starDays(app) / (act.cycleYears * 365.25);
            app.cyclePhaseOffset = phase - (p - std::floor(p));
        }
    } else {
        ui::note("Pas de cycle régulier : rotation trop rapide (régime saturé), "
                 "l'activité reste au maximum.");
    }
    info("Niveau d'activité", nullptr, "%.2f x la moyenne", cyc.level);
    info("Surface tachée", "Fraction moyenne calée entre le Soleil (~0,1 %) et les "
         "naines M saturées (~40 %, O'Neal et al. 2004) : f = 0,4 (L_X / L_X,sat)^0,84.",
         "%.2g %%  (moy. %.2g %%)", 100.0 * act.spotCoverage * cyc.level, 100.0 * act.spotCoverage);
    info("Latitude des taches", "Loi de Spörer : les taches naissent vers 28° et migrent "
         "vers l'équateur au fil du cycle, lat = 28° exp(-t / 90 mois) (diagramme "
         "papillon). Les rotateurs rapides ont des taches polaires (Schüssler & "
         "Solanki 1992). Entre parenthèses : fin du cycle précédent.",
         "%.0f°  (%.0f°)", cyc.bandLatDeg[0], cyc.bandLatDeg[1]);
    info("Ombre / pénombre", "Écart de température mesuré sur les taches stellaires "
         "(Berdyugina 2005) : ~1 700 K pour le Soleil, ~200 K pour les naines M. "
         "Brillance (T_tache / T)^4.", "%.0f / %.0f K", act.umbraTemp, act.penumbraTemp);
    info("Facules", "Régions magnétiques brillantes, surtout visibles près du bord. "
         "Aire 0,55 racine(f) (Shapiro et al. 2014).", "%.2g %%",
         100.0 * act.faculaCoverage * cyc.level);
    info("Vie d'une tache", "Règle de Gnevyshev-Waldmeier : durée = aire maximale / "
         "10 MSH par jour.", "%.0f jours", act.spotLifetimeDays);
}

void flareCard(App& app)
{
    const StellarActivity act = computeActivity(app.star);
    const CycleState cyc = cycleState(act, cyclePhase(app, act));
    info("Fréquence", "Éruptions d'énergie > 1 s x L_bol. Proportionnelle à l'émission X, "
         "calée sur la naine M GJ 1243 (Hawley et al. 2014). Énergies en loi de "
         "puissance dN/dE prop. à E^-2, profil de Davenport (2014), plasma à ~9 000 K.",
         "%.3g par jour", act.flaresPerDay * cyc.level);
    info("Seuil affiché", "Les éruptions durent quelques minutes : elles sont montrées "
         "au ralenti, et seules les plus grosses quand l'étoile en produit beaucoup.",
         "E > %.3g s x L_bol", app.flares.visibleThresholdSeconds());
    info("En cours / total", nullptr, "%d / %d", app.flares.count(), app.flares.total());
}

void oscillationCard(App& app)
{
    const StellarActivity act = computeActivity(app.star);
    char t[32];
    formatTime(1.0 / (act.numaxMicroHz * 1e-6), t, sizeof(t));
    info("Période", "Ondes sonores excitées par la convection : nu_max = 3 090 µHz "
         "(g / g_sol) (T / T_sol)^-1/2 (Brown et al. 1991). Trop rapides pour être "
         "vues sur le Soleil (5 min).", "%s", t);
    info("Amplitude", "dL/L = 4,7 ppm (L / M)^0,8 (Kjeldsen & Bedding 1995).",
         "%.2g %%", 100.0 * act.oscAmplitude);
}

} // namespace

UI_SECTION(ui::Tab::Object, 50, "Activité magnétique", dynamoCard, isStar);
UI_SECTION(ui::Tab::Object, 60, "Cycle et taches", cycleCard, hasDynamo);
UI_SECTION(ui::Tab::Object, 70, "Éruptions", flareCard, hasDynamo);
UI_SECTION(ui::Tab::Object, 80, "Oscillations", oscillationCard, oscillates);
