// Panneau de contrôle de la simulation, dessiné avec Dear ImGui par-dessus
// l'image du trou noir. F1 (ou Tab) l'affiche ou le cache.

#include "ui.hpp"

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <cmath>
#include <cstdio>

namespace {

constexpr float kPi = 3.14159265f;
constexpr float kRad2Deg = 180.0f / kPi;

// Rayon de Schwarzschild du Soleil (2GM/c²) et temps que met la lumière
// pour le parcourir (rs/c). Pour une masse M (en masses solaires), il suffit
// de multiplier par M.
constexpr double kSunRsKm = 2.9532;
constexpr double kSunRsOverCSeconds = 9.8510e-6;

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

// Petit "(?)" qui affiche une explication au survol.
void help(const char* text)
{
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::BeginItemTooltip()) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28.0f);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

void simulationSection(App& app)
{
    if (!ImGui::CollapsingHeader("Simulation", ImGuiTreeNodeFlags_DefaultOpen)) return;

    ImGui::Checkbox("Pause (P)", &app.paused);
    ImGui::SliderFloat("Vitesse du temps", &app.simSpeed, 0.25f, 200.0f, "x%.2f",
                       ImGuiSliderFlags_Logarithmic);
    help("Unités de temps rs/c simulées par seconde à l'écran. "
         "Touches + et - pour accélérer ou ralentir.");

    char real[32];
    if (app.starMode) {
        ImGui::Text("Temps écoulé : %.0f s", app.simTime / 10.0);
    } else {
        formatDuration(app.simTime * kSunRsOverCSeconds * app.massSolar, real, sizeof(real));
        ImGui::Text("Temps écoulé : %.0f rs/c  (%s pour ce trou noir)", app.simTime, real);
    }
    if (ImGui::Button("Revenir à t = 0"))
        app.simTime = 0.0;
}

void blackHoleSection(App& app)
{
    if (!ImGui::CollapsingHeader("Trou noir", ImGuiTreeNodeFlags_DefaultOpen)) return;

    ImGui::SliderFloat("Masse", &app.massSolar, 1.0f, 1.0e10f, "%.3g x Soleil",
                       ImGuiSliderFlags_Logarithmic);
    help("Toute la simulation est calculée en rayons de Schwarzschild (rs) : "
         "l'image est la même pour toutes les masses. La masse fixe l'échelle "
         "réelle : 10 soleils = trou noir stellaire, 4 millions = Sagittarius A*, "
         "6,5 milliards = M87*.");

    const double rsKm = kSunRsKm * app.massSolar;
    const double unitS = kSunRsOverCSeconds * app.massSolar;
    // Période orbitale à l'ISCO (r = 3 rs) : 2π / Ω avec Ω = sqrt(M / r³), M = 0,5.
    const double iscoPeriod = 2.0 * kPi / std::sqrt(0.5 / 27.0);

    char a[32], b[32], c[32], d[32];
    formatLength(rsKm, a, sizeof(a));
    formatLength(rsKm * 1.5, b, sizeof(b));
    formatLength(rsKm * 3.0, c, sizeof(c));
    formatDuration(iscoPeriod * unitS, d, sizeof(d));
    ImGui::Text("Horizon (rs = 2GM/c²) : %s", a);
    ImGui::Text("Sphère de photons (1,5 rs) : %s", b);
    ImGui::Text("Dernière orbite stable (3 rs) : %s", c);
    ImGui::Text("Un tour à 3 rs : %s", d);
}

void diskSection(App& app)
{
    if (!ImGui::CollapsingHeader("Disque d'accrétion", ImGuiTreeNodeFlags_DefaultOpen)) return;
    DiskSettings& disk = app.disk;

    ImGui::Checkbox("Afficher le disque (H)", &app.showDisk);
    ImGui::SliderFloat("Rayon intérieur", &disk.innerRadius, 1.5f, 10.0f, "%.2f rs");
    help("3 rs est la dernière orbite circulaire stable (ISCO). En dessous, "
         "aucune orbite stable n'existe : le gaz tombe en chute libre.");
    disk.outerRadius = std::max(disk.outerRadius, disk.innerRadius + 0.5f);
    ImGui::SliderFloat("Rayon extérieur", &disk.outerRadius, disk.innerRadius + 0.5f, 30.0f, "%.1f rs");
    ImGui::SliderFloat("Température max", &disk.maxTemperature, 1500.0f, 30000.0f, "%.0f K",
                       ImGuiSliderFlags_Logarithmic);
    help("Couleur de corps noir du gaz le plus chaud (vers 4 rs). Un vrai disque "
         "autour d'un trou noir stellaire atteint environ 10 millions de K et "
         "brille surtout en rayons X ; ici on choisit la couleur visible.");
    ImGui::SliderFloat("Luminosité", &disk.brightness, 0.1f, 6.0f, "%.2f",
                       ImGuiSliderFlags_Logarithmic);
    ImGui::SliderInt("Amas de gaz chaud", &disk.clumpCount, 0, 32);
    help("Amas qui spiralent vers le trou noir en suivant la vitesse képlérienne "
         "et chauffent en tombant.");
    ImGui::Checkbox("Effet Doppler", &disk.doppler);
    help("Le gaz qui vient vers nous paraît plus brillant et plus bleu, celui "
         "qui s'éloigne plus sombre et plus rouge. Décochez pour comparer.");
    ImGui::Checkbox("Décalage gravitationnel", &disk.gravitationalShift);
    help("La lumière perd de l'énergie en sortant du puits de gravité : "
         "facteur sqrt(1 - rs/r), fort près du bord intérieur.");
    if (ImGui::Button("Disque par défaut"))
        disk = DiskSettings{};
}

// Couleur approchée d'un corps noir (même formule que blackbody() du shader).
ImVec4 blackbodyColor(double temperature)
{
    double t = std::clamp(temperature, 1000.0, 40000.0) / 100.0;
    auto c01 = [](double v) { return float(std::clamp(v, 0.0, 1.0)); };
    float r = t <= 66.0 ? 1.0f : c01(1.29293618 * std::pow(t - 60.0, -0.1332047592));
    float g = t <= 66.0 ? c01(0.39008157 * std::log(t) - 0.63184144)
                        : c01(1.12989086 * std::pow(t - 60.0, -0.0755148492));
    float b = t >= 66.0 ? 1.0f : (t <= 19.0 ? 0.0f : c01(0.54320678 * std::log(t - 10.0) - 1.19625408));
    return ImVec4(r, g, b, 1.0f);
}

bool sliderDouble(const char* label, double* v, double lo, double hi, const char* fmt, bool log = false)
{
    return ImGui::SliderScalar(label, ImGuiDataType_Double, v, &lo, &hi, fmt,
                               log ? ImGuiSliderFlags_Logarithmic : ImGuiSliderFlags_None);
}

void sceneSection(App& app)
{
    int scene = app.starMode ? 1 : 0;
    ImGui::TextUnformatted("Scène :");
    ImGui::SameLine();
    bool changed = ImGui::RadioButton("Trou noir", &scene, 0);
    ImGui::SameLine();
    changed |= ImGui::RadioButton("Étoile (E)", &scene, 1);
    if (changed)
        setStarMode(app, scene == 1);
}

void starSection(App& app)
{
    if (!ImGui::CollapsingHeader("Étoile", ImGuiTreeNodeFlags_DefaultOpen)) return;
    Star& star = app.star;
    const auto& presets = starPresets();

    // Étoiles réelles (paramètres mesurés).
    const char* current = app.starIndex >= 0 ? presets[app.starIndex].name.c_str() : "Sur mesure";
    if (ImGui::BeginCombo("Étoile connue (N / B)", current)) {
        for (int i = 0; i < int(presets.size()); ++i) {
            if (ImGui::Selectable(presets[i].name.c_str(), i == app.starIndex))
                selectPreset(app, i);
            if (ImGui::IsItemHovered())
                ImGui::SetItemTooltip("%s", presets[i].kind.c_str());
        }
        ImGui::EndCombo();
    }

    // Créer une étoile à partir de sa seule masse.
    ImGui::SeparatorText("Créer une étoile");
    static int family = 0;          // 0 = séquence principale, 1 = naine blanche
    static double msMass = 1.0;
    static double wdMass = 0.6, wdTemp = 20000.0;
    ImGui::RadioButton("Séquence principale", &family, 0);
    ImGui::SameLine();
    ImGui::RadioButton("Naine blanche", &family, 1);
    if (family == 0) {
        if (sliderDouble("Masse##ms", &msMass, 0.08, 100.0, "%.3g x Soleil", true))
            selectMainSequence(app, msMass);
        help("Étoile qui brûle son hydrogène, comme le Soleil. La masse suffit : "
             "la relation masse-luminosité (L ~ M^3,5 à M^4) et la relation "
             "masse-rayon donnent L et R, puis Stefan-Boltzmann donne la "
             "température. 0,1 soleil : naine rouge. 20 soleils : étoile bleue.");
    } else {
        bool c = sliderDouble("Masse##wd", &wdMass, 0.2, 1.42, "%.3g x Soleil");
        c |= sliderDouble("Température##wd", &wdTemp, 4000.0, 100000.0, "%.0f K", true);
        if (c) {
            app.starIndex = -1;
            star = whiteDwarf(wdMass, wdTemp);
        }
        help("Cœur d'étoile éteinte, de la taille de la Terre. Plus elle est "
             "lourde, plus elle est PETITE (relation de Chandrasekhar) ; "
             "au-delà de 1,44 soleil elle s'effondre.");
    }

    // Réglage libre de chaque grandeur.
    ImGui::SeparatorText("Réglages fins");
    bool edited = false;
    edited |= sliderDouble("Masse", &star.mass, 0.05, 150.0, "%.3g x Soleil", true);
    edited |= sliderDouble("Rayon", &star.radius, 1e-5, 2000.0, "%.3g x Soleil", true);
    help("En rayons du Soleil (696 000 km). Étoile à neutrons : ~0,00002 ; "
         "Bételgeuse : ~760.");
    edited |= sliderDouble("Température", &star.temperature, 2000.0, 1.0e6, "%.0f K", true);
    edited |= sliderDouble("Rotation", &star.rotationDays, 1e-5, 40000.0, "%.3g jours", true);
    edited |= sliderDouble("Activité", &star.activity, 0.0, 1.0, "%.2f");
    help("Taches et éruptions : 0 = étoile calme, 1 = très active.");
    if (edited && app.starIndex >= 0) {
        app.starIndex = -1;
        star.name = "Sur mesure";
    }

    // Grandeurs déduites par le modèle physique (src/star.cpp).
    ImGui::SeparatorText("Ce que la physique en déduit");
    ImGui::ColorButton("##couleur", blackbodyColor(star.temperature),
                       ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoPicker,
                       ImVec2(ImGui::GetFontSize() * 1.6f, ImGui::GetFontSize() * 1.6f));
    ImGui::SameLine();
    ImGui::Text("%s, classe %s", star.kind.c_str(), star.spectralClass().c_str());
    ImGui::Text("Luminosité : %.3g fois le Soleil", star.luminosity());
    help("Stefan-Boltzmann : L = R² (T / 5772 K)^4.");
    ImGui::Text("Gravité de surface : %.3g fois le Soleil", star.surfaceGravity());
    ImGui::Text("Compacité rs/R : %.2g", star.compactness());
    help("Rapport entre le rayon de Schwarzschild de l'étoile et son rayon. "
         "Quasi nul pour le Soleil (la lumière va tout droit), ~0,35 pour une "
         "étoile à neutrons : on voit alors une partie de sa face cachée. "
         "1 = elle devient un trou noir.");
}

void cameraSection(App& app)
{
    if (!ImGui::CollapsingHeader("Caméra")) return;
    OrbitCamera& cam = app.camera;

    ImGui::SliderFloat("Distance", &cam.targetDistance, kMinDistance, kMaxDistance,
                       app.starMode ? "%.1f rayons d'étoile" : "%.1f rs", ImGuiSliderFlags_Logarithmic);
    float yawDeg = std::remainder(cam.yaw, 2.0f * kPi) * kRad2Deg;
    if (ImGui::SliderFloat("Angle horizontal", &yawDeg, -180.0f, 180.0f, "%.0f°")) {
        cam.yaw = yawDeg / kRad2Deg;
        cam.yawVel = 0.0f;
    }
    float pitchDeg = cam.pitch * kRad2Deg;
    if (ImGui::SliderFloat("Hauteur", &pitchDeg, -kMaxPitch * kRad2Deg, kMaxPitch * kRad2Deg, "%.0f°")) {
        cam.pitch = pitchDeg / kRad2Deg;
        cam.pitchVel = 0.0f;
    }
    help("0° : dans le plan du disque. 86° : vue de dessus.");
    float fovDeg = cam.fovY * kRad2Deg;
    if (ImGui::SliderFloat("Champ de vision", &fovDeg, 20.0f, 120.0f, "%.0f°"))
        cam.fovY = fovDeg / kRad2Deg;
    ImGui::Checkbox("Orbite automatique (Espace)", &cam.autoOrbit);
    if (ImGui::Button("Recentrer (C)"))
        resetCamera(app);
}

void renderSection(App& app, const UiStats& stats)
{
    if (!ImGui::CollapsingHeader("Rendu")) return;

    ImGui::Text("%d FPS   image %dx%d   GPU %.1f ms", stats.fps, stats.traceWidth,
                stats.traceHeight, stats.gpuMs);
    ImGui::Checkbox("Résolution automatique (O)", &app.autoScale);
    help("Baisse ou monte la résolution du ray tracing pour tenir le nombre "
         "d'images par seconde visé.");
    if (ImGui::SliderFloat("Résolution", &app.renderScale, kMinScale, kMaxScale, "%.2f x fenêtre"))
        app.autoScale = false;
    ImGui::SliderFloat("FPS visé", &app.targetFps, 15.0f, 120.0f, "%.0f");
    ImGui::SliderInt("Pas max par rayon", &app.maxSteps, 50, 600);
    help("Garde-fou. Un rayon qui s'échappe prend environ 20 pas, un rayon qui "
         "fait le tour de la sphère de photons environ 30.");
    ImGui::SliderFloat("Exposition", &app.exposure, 0.2f, 4.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
    if (ImGui::Button("Recharger les shaders (R)"))
        app.reloadRequested = true;
}

void helpSection()
{
    if (!ImGui::CollapsingHeader("Commandes")) return;
    ImGui::TextUnformatted(
        "Clic gauche + glisser : tourner autour\n"
        "Flèches / ZQSD : tourner autour\n"
        "Molette / Page haut-bas : zoom\n"
        "Espace : orbite automatique\n"
        "C : recentrer   P : pause\n"
        "+ / - : vitesse du temps\n"
        "H : disque   R : recharger les shaders\n"
        "K / L / O : résolution\n"
        "E : trou noir / étoile\n"
        "N / B : étoile suivante / précédente\n"
        "I / U : étoile plus / moins massive\n"
        "F1 ou Tab : cacher ce panneau\n"
        "Échap : quitter");
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

    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 6.0f;
    style.FrameRounding = 4.0f;
    style.GrabRounding = 4.0f;
    style.Colors[ImGuiCol_WindowBg].w = 0.85f;
    style.ScaleAllSizes(scale);

    ImFontConfig font;
    font.SizePixels = 13.0f * scale;
    io.Fonts->AddFontDefault(&font);

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
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float margin = 10.0f;
    if (app.showUi) {
        ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + margin, vp->WorkPos.y + margin), ImGuiCond_FirstUseEver);
        // Largeur fixe, hauteur ajustée au contenu sans dépasser la fenêtre.
        const float width = ImGui::GetFontSize() * 30.0f;
        ImGui::SetNextWindowSizeConstraints(ImVec2(width, 0.0f),
                                            ImVec2(width, vp->WorkSize.y - 2.0f * margin));
        if (ImGui::Begin("Contrôle de la simulation (F1)", &app.showUi,
                         ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::PushItemWidth(ImGui::GetFontSize() * 13.0f);
            sceneSection(app);
            simulationSection(app);
            if (app.starMode) {
                starSection(app);
            } else {
                blackHoleSection(app);
                diskSection(app);
            }
            cameraSection(app);
            renderSection(app, stats);
            helpSection();
            ImGui::PopItemWidth();
        }
        ImGui::End();
    } else {
        // Rappel discret quand le panneau est caché.
        ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + margin, vp->WorkPos.y + margin));
        ImGui::SetNextWindowBgAlpha(0.4f);
        ImGui::Begin("##aide", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                     ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);
        ImGui::Text("F1 : panneau de contrôle   %d FPS", stats.fps);
        ImGui::End();
    }

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}
