#pragma once

// Panneau de contrôle de la simulation (Dear ImGui).

#include "app.hpp"

struct GLFWwindow;

// Mesures affichées dans le panneau.
struct UiStats {
    int fps = 0;
    int traceWidth = 0, traceHeight = 0;   // taille de l'image de ray tracing
    float gpuMs = 0.0f;                    // temps GPU du ray tracing par image
};

// À appeler après avoir installé les callbacks GLFW de l'application :
// ImGui les enchaîne (il appelle les nôtres puis traite l'événement).
void uiInit(GLFWwindow* window);
void uiShutdown();

// Vrai quand la souris / le clavier sont sur le panneau : la caméra et les
// raccourcis clavier doivent alors les ignorer.
bool uiWantsMouse();
bool uiWantsKeyboard();

// Construit et dessine le panneau dans le framebuffer actuellement lié.
void uiDraw(App& app, const UiStats& stats);
