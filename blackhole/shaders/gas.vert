// =============================================================================
//  Jet de gaz d'un systeme double : un point par paquet de gaz arrache a
//  l'etoile compagne, dessine en lueur additive par-dessus le ray tracing
//  (avant le tone mapping). Positions calculees par le CPU (src/binary.cpp).
//
//  Comme pour les asteroides, la lumiere va en ligne droite jusqu'a la
//  camera ; on cache le gaz qui est derriere l'ombre du trou noir ou derriere
//  l'etoile compagne, et on l'attenue derriere le disque.
// =============================================================================

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec4 aInfo;   // vitesse (fraction de c), temperature (K)

uniform vec3  uCamPos;
uniform vec3  uCamRight;
uniform vec3  uCamUp;
uniform vec3  uCamForward;
uniform float uTanHalf;
uniform float uAspect;
uniform float uResY;
uniform int   uDisk;
uniform float uDiskIn;
uniform float uDiskOut;
uniform float uDoppler;
uniform vec4  uCompanion;    // centre, rayon englobant de l'etoile
uniform float uGlow;

out vec3 vColor;

vec3 blackbody(float T)
{
    T = clamp(T, 1000.0, 40000.0) / 100.0;
    vec3 c;
    c.r = T <= 66.0 ? 1.0 : clamp(1.29293618 * pow(T - 60.0, -0.1332047592), 0.0, 1.0);
    c.g = T <= 66.0 ? clamp(0.39008157 * log(T) - 0.63184144, 0.0, 1.0)
                    : clamp(1.12989086 * pow(T - 60.0, -0.0755148492), 0.0, 1.0);
    c.b = T >= 66.0 ? 1.0 : (T <= 19.0 ? 0.0 : clamp(0.54320678 * log(T - 10.0) - 1.19625408, 0.0, 1.0));
    return pow(c, vec3(2.2));
}

// Le segment camera -> p passe-t-il dans la sphere (c, R) ?
bool behind(vec3 p, vec3 c, float R)
{
    vec3 rel = p - uCamPos;
    float dist = length(rel);
    vec3 dir = rel / dist;
    float tc = dot(c - uCamPos, dir);
    vec3 closest = uCamPos + tc * dir;
    return tc > 0.0 && tc < dist && length(closest - c) < R;
}

void main()
{
    vec3 rel = aPos - uCamPos;
    float x = dot(rel, uCamRight);
    float y = dot(rel, uCamUp);
    float z = dot(rel, uCamForward);
    gl_Position = vec4(x / (uTanHalf * uAspect), y / uTanHalf, 0.0, z);

    // Taille a l'echelle : un paquet de gaz fait ~0,5 rs de large.
    float size = 0.8 * uResY / (2.0 * uTanHalf * max(z, 0.1));
    gl_PointSize = clamp(size, 2.0, 24.0);

    float dim = 1.0;
    // Ombre du trou noir (parametre d'impact critique 2,6 rs) et etoile compagne.
    if (behind(aPos, vec3(0.0), 2.598)) dim = 0.0;
    if (uCompanion.w > 0.0 && behind(aPos, uCompanion.xyz, uCompanion.w)) dim = 0.0;
    // Derriere le disque d'accretion.
    if (uDisk == 1 && uCamPos.y * aPos.y < 0.0) {
        float t = uCamPos.y / (uCamPos.y - aPos.y);
        float rd = length((uCamPos + t * rel).xz);
        if (rd > uDiskIn && rd < uDiskOut) dim *= 0.25;
    }

    // Doppler relativiste (le gaz tombe a une fraction notable de c) et
    // decalage gravitationnel.
    vec3 v = aInfo.xyz;
    vec3 toCam = -normalize(rel);
    float dop = 1.0 / (sqrt(max(1.0 - dot(v, v), 1e-4)) * (1.0 - dot(v, toCam)));
    float r = length(aPos);
    float g = mix(1.0, dop, uDoppler) * sqrt(max(1.0 - 1.0 / r, 0.0));

    // L'eclat par pixel ne depend pas de la taille du point a l'ecran.
    float area = gl_PointSize * gl_PointSize;
    vColor = blackbody(aInfo.w * g) * pow(g, 3.0) * dim * uGlow * clamp(size * size / area, 0.0, 1.0);
    if (dim <= 0.0) gl_Position = vec4(2.0, 2.0, 2.0, 1.0);   // hors de l'ecran
}
