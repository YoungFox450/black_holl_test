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
    formatDuration(app.simTime * kSunRsOverCSeconds * app.massSolar, real, sizeof(real));
    ImGui::Text("Temps écoulé : %.0f rs/c  (%s pour ce trou noir)", app.simTime, real);
    if (ImGui::Button("Revenir à t = 0"))
        app.simTime = 0.0;
}

void blackHoleSection(App& app)
{
    if (!ImGui::CollapsingHeader("Trou noir", ImGuiTreeNodeFlags_DefaultOpen)) return;

    ImGui::SliderFloat("Masse (soleils)", &app.massSolar, 1.0f, 1.0e10f, "%.3g",
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

// Étoiles : choix d'une étoile connue ou réglage libre de ses paramètres.
// Tout ce qui est affiché en dessous est calculé par le modèle (star.cpp).
void starSection(App& app)
{
    if (!ImGui::CollapsingHeader("Étoiles", ImGuiTreeNodeFlags_DefaultOpen)) return;

    bool on = app.starMode;
    if (ImGui::Checkbox("Simuler une étoile (E)", &on))
        setStarMode(app, on);
    help("Remplace le trou noir par une étoile. Les distances de la caméra "
         "sont alors en rayons de l'étoile.");
    if (!app.starMode) return;

    // Liste des étoiles connues.
    const auto& presets = starPresets();
    const char* current = app.starIndex >= 0 ? presets[app.starIndex].name.c_str() : "Personnalisée";
    if (ImGui::BeginCombo("Étoile (N / B)", current)) {
        for (int i = 0; i < int(presets.size()); ++i) {
            if (ImGui::Selectable(presets[i].name.c_str(), i == app.starIndex))
                selectPreset(app, i);
        }
        ImGui::EndCombo();
    }

    // Création à partir de la masse seule.
    static float msMass = 1.0f;
    ImGui::SliderFloat("##masse_sp", &msMass, 0.08f, 100.0f, "%.3g soleils", ImGuiSliderFlags_Logarithmic);
    ImGui::SameLine();
    if (ImGui::Button("Séquence principale"))
        selectMainSequence(app, msMass);
    help("Crée une étoile « normale » (qui brûle son hydrogène, comme le Soleil) "
         "à partir de sa seule masse : relations masse-luminosité et "
         "masse-rayon, puis température par la loi de Stefan-Boltzmann.");
    static float wdMass = 0.6f;
    const float wdTemp = 20000.0f;
    ImGui::SliderFloat("##masse_nb", &wdMass, 0.2f, 1.4f, "%.2f soleils");
    ImGui::SameLine();
    if (ImGui::Button("Naine blanche")) {
        app.starIndex = -1;
        app.star = whiteDwarf(wdMass, wdTemp);
    }
    help("Cœur d'étoile morte soutenu par la pression des électrons : plus elle "
         "est lourde, plus elle est petite (relation de Nauenberg), jusqu'à la "
         "limite de Chandrasekhar (1,44 soleil).");

    // Réglage libre des paramètres.
    ImGui::SeparatorText("Paramètres");
    Star& s = app.star;
    float mass = float(s.mass), radius = float(s.radius), temp = float(s.temperature);
    float rot = float(s.rotationDays), act = float(s.activity);
    bool changed = false;
    changed |= ImGui::SliderFloat("Masse", &mass, 0.08f, 100.0f, "%.3g M sol", ImGuiSliderFlags_Logarithmic);
    changed |= ImGui::SliderFloat("Rayon", &radius, 1e-5f, 1000.0f, "%.3g R sol", ImGuiSliderFlags_Logarithmic);
    changed |= ImGui::SliderFloat("Température", &temp, 2300.0f, 40000.0f, "%.0f K", ImGuiSliderFlags_Logarithmic);
    changed |= ImGui::SliderFloat("Rotation", &rot, 0.01f, 1000.0f, "%.3g jours", ImGuiSliderFlags_Logarithmic);
    changed |= ImGui::SliderFloat("Activité", &act, 0.0f, 1.0f, "%.2f");
    help("Taches stellaires : fortes sur les petites étoiles froides, absentes "
         "sur les étoiles chaudes.");
    if (changed) {
        s.mass = mass; s.radius = radius; s.temperature = temp;
        s.rotationDays = rot; s.activity = act;
        if (app.starIndex >= 0) {
            app.starIndex = -1;
            s.name = "Personnalisée";
        }
    }

    // Grandeurs calculées.
    ImGui::SeparatorText("Calculé par le modèle");
    ImGui::Text("Type : %s, classe %s", s.kind.c_str(), s.spectralClass().c_str());
    ImGui::Text("Luminosité : %.3g fois le Soleil", s.luminosity());
    help("Loi de Stefan-Boltzmann : L = R² (T / 5772 K)⁴.");
    ImGui::Text("Gravité de surface : %.3g fois celle du Soleil", s.surfaceGravity());
    ImGui::Text("Compacité rs/R : %.3g", s.compactness());
    help("Rapport entre le rayon de Schwarzschild de la masse et le rayon de "
         "l'étoile. Proche de 0 : lumière quasi pas déviée. Étoile à neutrons "
         "(~0,35) : la lumière est assez courbée pour montrer une partie de "
         "l'arrière de l'étoile.");
}

void cameraSection(App& app)
{
    if (!ImGui::CollapsingHeader("Caméra")) return;
    OrbitCamera& cam = app.camera;

    ImGui::SliderFloat("Distance", &cam.targetDistance, kMinDistance, kMaxDistance, "%.1f rs",
                       ImGuiSliderFlags_Logarithmic);
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
    if (ImGui::Button("Recentrer (C)")) {
        bool autoOrbit = cam.autoOrbit;
        cam = OrbitCamera{};
        cam.autoOrbit = autoOrbit;
    }
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
        "E : étoile   N / B : étoile suivante / précédente\n"
        "I / U : étoile plus / moins massive\n"
        "K / L / O : résolution\n"
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
            simulationSection(app);
            starSection(app);
            if (!app.starMode) {
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
