#pragma once

// Fond de ciel à partir d'une vraie image de la Voie lactée.
//
// L'image est une carte équirectangulaire (longitude en x, latitude en y)
// en coordonnées galactiques ou équatoriales. Elle est lue une fois, puis
// « cuite » dans la même cubemap que le ciel procédural (shaders/sky.frag) :
// le ray tracer ne voit pas la différence et le coût par image est nul.
//
// Formats lus : .hdr (Radiance) et .exr (OpenEXR) en HDR, .rgbe.png (HDR
// encodé en RGBE dans un PNG, comme assets/sky/voie-lactee.rgbe.png), et
// .jpg / .png ordinaires (LDR, convertis en lumière linéaire).

#include <string>
#include <vector>

struct SkySettings {
    bool useImage = true;    // false : ciel procédural (shaders/sky.frag)
    std::string path;        // vide : assets/sky/voie-lactee.rgbe.png
    bool galactic = true;    // repère de l'image : galactique (sinon équatorial J2000)
    bool mirror = false;     // longitude croissante vers la droite au lieu de la gauche
    float yaw = 0.0f;        // degrés : rotation autour de l'axe du trou noir
    float tilt = 25.0f;      // degrés : inclinaison du plan de la Galaxie sur le disque
    bool rebuild = false;    // à recalculer (réglage changé dans le panneau)
    std::string status;      // message pour le panneau
};

// Image chargée (lumière linéaire RGB, normalisée).
struct SkyImage {
    std::vector<float> rgb;
    int width = 0, height = 0;
    std::string path;
};

std::string defaultSkyImagePath();

// Charge une carte équirectangulaire. maxWidth : réduite au-delà (une carte
// NASA en 16k ferait plusieurs Go en flottants). Normalise la luminosité
// moyenne pour qu'elle ressemble au ciel procédural.
bool loadSkyImage(const std::string& path, int maxWidth, SkyImage& out, std::string& error);

// Cuit l'image dans une nouvelle cubemap de faceSize² texels par face.
// Renvoie 0 si l'image ou le shader manque (settings.status dit pourquoi).
unsigned int bakeSkyFromImage(const std::string& shaderDir, unsigned int vao, int faceSize, SkySettings& settings);
