// =============================================================================
//  Asteroides : un point par asteroide, dessine en "sprite" rond par-dessus
//  l'image du ray tracing (avant le tone mapping).
//  Positions calculees par le CPU (src/asteroids.cpp).
//
//  Simplification : la lumiere de l'asteroide va en ligne droite jusqu'a la
//  camera (pas de lentille gravitationnelle sur les asteroides). On cache
//  quand meme ceux qui sont derriere le corps central : derriere l'ombre du
//  trou noir (rayon critique 2,6 rs) ou derriere l'etoile.
// =============================================================================

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aInfo;   // diametre (km), temperature (K), fragment (0/1)

uniform vec3  uCamPos;
uniform vec3  uCamRight;
uniform vec3  uCamUp;
uniform vec3  uCamForward;
uniform float uTanHalf;
uniform float uAspect;
uniform float uResY;
uniform float uOccluder;   // rayon apparent du corps central
uniform float uBlackHole;  // 1 = trou noir, 0 = etoile
uniform int   uDisk;
uniform float uDiskIn;
uniform float uDiskOut;
uniform float uLightScale;  // eclairement a r = 1 (decroit en 1/r^2)
uniform float uSizeScale;   // taille d'affichage choisie dans le panneau

out vec3  vLightDir;   // vers le corps central, repere de la camera
out float vHeat;
out float vDim;
out float vFall;

void main()
{
    vec3 rel = aPos - uCamPos;
    float x = dot(rel, uCamRight);
    float y = dot(rel, uCamUp);
    float z = dot(rel, uCamForward);
    gl_Position = vec4(x / (uTanHalf * uAspect), y / uTanHalf, 0.0, z);

    // Taille a l'ecran : lisible plutot qu'a l'echelle (un astéroide de 1 km
    // a 10 rs d'un trou noir de 10 soleils serait mille fois plus petit
    // qu'un pixel).
    float size = (1.5 + 0.9 * log2(1.0 + aInfo.x)) * (uResY / 360.0) * 12.0 / max(z, 0.1);
    gl_PointSize = clamp(size * uSizeScale, 1.5, 14.0 * uSizeScale);

    float dim = 1.0;
    // Cache derriere le corps central.
    float dist = length(rel);
    vec3 dir = rel / dist;
    float tc = -dot(uCamPos, dir);                    // point du rayon le plus proche du centre
    float b = length(uCamPos + tc * dir);
    if (tc > 0.0 && tc < dist && b < uOccluder) dim = 0.0;

    // Derriere le disque d'accretion (semi-transparent).
    if (uDisk == 1 && uCamPos.y * aPos.y < 0.0) {
        float t = uCamPos.y / (uCamPos.y - aPos.y);
        float rd = length((uCamPos + t * rel).xz);
        if (rd > uDiskIn && rd < uDiskOut) dim *= 0.3;
    }

    // Pres de l'horizon, la lumiere perd son energie (decalage vers le rouge).
    float r = length(aPos);
    if (uBlackHole > 0.5) dim *= sqrt(max(1.0 - 1.0 / r, 0.0));

    vec3 L = -normalize(aPos);
    vLightDir = vec3(dot(L, uCamRight), dot(L, uCamUp), -dot(L, uCamForward));
    vFall = clamp(uLightScale / (r * r), 0.03, 2.5);
    vHeat = aInfo.y;
    vDim = dim * (aInfo.z > 0.5 ? 0.8 : 1.0);
    if (dim <= 0.0) gl_Position = vec4(2.0, 2.0, 2.0, 1.0);   // hors de l'ecran
}
