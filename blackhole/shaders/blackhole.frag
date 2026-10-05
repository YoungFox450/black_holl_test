#version 330 core

// =============================================================================
//  Ray tracing d'un trou noir de Schwarzschild
//
//  Un rayon par pixel part de la caméra. On ne suit pas les étoiles : on suit
//  la LUMIÈRE (le photon) en remontant le temps, de l'œil vers la scène.
//  La gravité courbe sa trajectoire ; à la fin on regarde où il a fini :
//    - dans l'horizon des événements  -> noir (la lumière ne ressort jamais)
//    - à travers le disque d'accrétion -> on ajoute la lumière du gaz chaud
//    - parti à l'infini               -> on lit le fond de galaxie dans la
//                                        direction FINALE (déviée) du rayon
//
//  Unités : G = c = 1 et rayon de Schwarzschild rs = 2GM/c² = 1.
// =============================================================================

in vec2 vUV;
out vec4 FragColor;

uniform vec2  uResolution;
uniform float uTime;
uniform vec3  uCamPos;
uniform vec3  uCamRight;
uniform vec3  uCamUp;
uniform vec3  uCamForward;
uniform float uFovY;      // champ de vision vertical (radians)
uniform int   uDisk;      // 1 = disque d'accrétion visible
uniform int   uMaxSteps;  // nombre max de pas d'intégration par rayon

const float RS         = 1.0;   // rayon de Schwarzschild (horizon)
const float DISK_IN    = 3.0;   // ISCO = 6GM/c² = 3 rs : dernière orbite stable
const float DISK_OUT   = 12.0;
const float ESCAPE_R   = 60.0;  // au-delà, le rayon est considéré libre
const float PI         = 3.14159265359;

// -----------------------------------------------------------------------------
//  Bruit procédural (pour le fond de galaxie et la texture du disque)
// -----------------------------------------------------------------------------
float hash13(vec3 p)
{
    p = fract(p * 0.1031);
    p += dot(p, p.zyx + 31.32);
    return fract((p.x + p.y) * p.z);
}

vec3 hash33(vec3 p)
{
    p = fract(p * vec3(0.1031, 0.1030, 0.0973));
    p += dot(p, p.yxz + 33.33);
    return fract((p.xxy + p.yxx) * p.zyx);
}

float noise3(vec3 p)
{
    vec3 i = floor(p);
    vec3 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(mix(hash13(i + vec3(0, 0, 0)), hash13(i + vec3(1, 0, 0)), f.x),
                   mix(hash13(i + vec3(0, 1, 0)), hash13(i + vec3(1, 1, 0)), f.x), f.y),
               mix(mix(hash13(i + vec3(0, 0, 1)), hash13(i + vec3(1, 0, 1)), f.x),
                   mix(hash13(i + vec3(0, 1, 1)), hash13(i + vec3(1, 1, 1)), f.x), f.y), f.z);
}

float fbm(vec3 p)
{
    float v = 0.0, a = 0.5;
    for (int i = 0; i < 5; ++i) {
        v += a * noise3(p);
        p = p * 2.03 + 17.1;
        a *= 0.5;
    }
    return v;
}

// -----------------------------------------------------------------------------
//  Fond de l'univers : champ d'étoiles + bande de galaxie (type Voie lactée).
//  C'est une fonction de la DIRECTION seulement (fond "à l'infini").
// -----------------------------------------------------------------------------
vec3 starLayer(vec3 dir, float scale, float density)
{
    vec3 p = dir * scale;
    vec3 cell = floor(p);
    vec3 col = vec3(0.0);
    // On regarde la cellule et ses voisines pour éviter les étoiles coupées.
    for (int x = -1; x <= 1; ++x)
    for (int y = -1; y <= 1; ++y)
    for (int z = -1; z <= 1; ++z) {
        vec3 c = cell + vec3(x, y, z);
        vec3 h = hash33(c);
        if (h.x > density) continue;
        vec3 starPos = c + h;                      // position de l'étoile dans la cellule
        float d = length(p - starPos);
        float size = 0.06 + 0.10 * h.y;
        float b = smoothstep(size, 0.0, d);
        float temp = h.z;                           // couleur : rouge -> bleu
        vec3 tint = mix(vec3(1.0, 0.75, 0.55), vec3(0.65, 0.8, 1.0), temp);
        col += tint * b * b * (0.6 + 2.0 * h.y);
    }
    return col;
}

vec3 galaxyBackground(vec3 dir)
{
    dir = normalize(dir);

    // Plan galactique incliné et direction du centre galactique.
    vec3 galN      = normalize(vec3(0.25, 1.0, -0.35));
    vec3 galCenter = normalize(cross(galN, vec3(0.0, 0.0, 1.0)));
    float lat      = dot(dir, galN);                 // "latitude" galactique
    float toCenter = dot(dir, galCenter) * 0.5 + 0.5;

    // Bande lumineuse diffuse, plus épaisse et brillante vers le centre.
    float width = mix(0.05, 0.16, pow(toCenter, 3.0));
    float band  = exp(-lat * lat / (width * width));
    float clouds = fbm(dir * 6.0);
    float dust   = smoothstep(0.45, 0.75, fbm(dir * 11.0 + 3.0));  // bandes de poussière
    float glow   = band * (0.35 + 0.9 * clouds) * (1.0 - 0.8 * dust * band);
    glow *= 0.4 + 1.6 * pow(toCenter, 4.0);

    vec3 bandCol = mix(vec3(0.35, 0.45, 0.9), vec3(1.0, 0.8, 0.55), pow(toCenter, 2.0));
    vec3 col = bandCol * glow * 0.35;

    // Nébuleuses colorées très diffuses.
    float neb = fbm(dir * 2.5 + 11.0);
    col += vec3(0.5, 0.15, 0.35) * pow(neb, 6.0) * 0.25;
    col += vec3(0.1, 0.3, 0.5)   * pow(fbm(dir * 3.0 - 7.0), 6.0) * 0.3;

    // Étoiles : plus nombreuses dans la bande galactique.
    col += starLayer(dir, 80.0, 0.12 + 0.3 * band);
    col += starLayer(dir, 220.0, 0.10 + 0.3 * band) * 0.7;

    return col;
}

// -----------------------------------------------------------------------------
//  Couleur approchée d'un corps noir de température T (Kelvin).
// -----------------------------------------------------------------------------
vec3 blackbody(float T)
{
    T = clamp(T, 1000.0, 40000.0) / 100.0;
    vec3 c;
    c.r = T <= 66.0 ? 1.0 : clamp(1.29293618 * pow(T - 60.0, -0.1332047592), 0.0, 1.0);
    c.g = T <= 66.0 ? clamp(0.39008157 * log(T) - 0.63184144, 0.0, 1.0)
                    : clamp(1.12989086 * pow(T - 60.0, -0.0755148492), 0.0, 1.0);
    c.b = T >= 66.0 ? 1.0 : (T <= 19.0 ? 0.0 : clamp(0.54320678 * log(T - 10.0) - 1.19625408, 0.0, 1.0));
    return c;
}

// -----------------------------------------------------------------------------
//  Disque d'accrétion mince dans le plan y = 0.
//  p      : point où le rayon traverse le plan
//  rayDir : direction de propagation du rayon (de la caméra vers la scène)
//  Retourne couleur (rgb) et opacité (a).
// -----------------------------------------------------------------------------
vec4 accretionDisk(vec3 p, vec3 rayDir)
{
    float r = length(p.xz);
    if (r < DISK_IN || r > DISK_OUT) return vec4(0.0);

    // Profil de température d'un disque mince (Shakura-Sunyaev) :
    //   T(r) ∝ r^(-3/4) * (1 - sqrt(r_in / r))^(1/4)
    float x = DISK_IN / r;
    float profile = pow(x, 0.75) * pow(max(1.0 - sqrt(x), 0.0), 0.25);
    float T = 4500.0 * profile / 0.488;  // normalisé pour ~4500 K au maximum

    // Vitesse orbitale circulaire (vue par un observateur statique) :
    //   v = sqrt(M / (r - 2M)) = sqrt(0.5 / (r - 1))   avec rs = 2M = 1
    float beta  = sqrt(0.5 / (r - RS));
    float gamma = 1.0 / sqrt(1.0 - beta * beta);
    vec3 orbitDir = normalize(vec3(-p.z, 0.0, p.x));   // rotation du gaz

    // Effet Doppler relativiste : le côté qui vient vers nous est plus
    // lumineux et plus bleu. La lumière va du disque vers la caméra = -rayDir.
    float cosTheta = dot(orbitDir, -normalize(rayDir));
    float doppler  = 1.0 / (gamma * (1.0 - beta * cosTheta));

    // Décalage gravitationnel vers le rouge : la lumière perd de l'énergie
    // en sortant du puits de potentiel.
    float gravShift = sqrt(1.0 - RS / r);

    float g = doppler * gravShift;
    vec3 color = blackbody(T * g);
    float intensity = pow(g, 4.0) * profile * 1.6;   // I_obs = g^4 * I_émis

    // Texture turbulente qui tourne à la vitesse képlérienne ω ∝ r^(-3/2).
    float omega = 0.5 * pow(r, -1.5) * 4.0;
    float phi   = atan(p.z, p.x) - omega * uTime;
    float turb  = fbm(vec3(r * 1.6, cos(phi) * 3.0, sin(phi) * 3.0));
    float rings = 0.75 + 0.25 * sin(r * 5.0 + turb * 6.0);
    intensity *= 0.35 + 1.1 * turb * rings;

    // Bords adoucis.
    float edge = smoothstep(DISK_IN, DISK_IN + 0.3, r) * smoothstep(DISK_OUT, DISK_OUT - 4.0, r);
    float alpha = clamp(edge * (0.55 + 0.6 * turb), 0.0, 1.0);

    return vec4(color * intensity * edge, alpha);
}

// -----------------------------------------------------------------------------
//  Équation des géodésiques nulles (trajectoires de la lumière).
//
//  En Schwarzschild, la trajectoire d'un photon vérifie (équation de Binet) :
//      d²u/dφ² + u = (3/2) rs u²        avec u = 1/r
//  Le terme (3/2) rs u² est la correction de la relativité générale (sans lui
//  le rayon irait tout droit). Écrite en coordonnées cartésiennes 3D, cela
//  revient à une "force" centrale sur le photon :
//      d²x/dλ² = -(3/2) rs h² x / r^5      avec h = |x × dx/dλ| constant
//  (h = moment cinétique du photon, conservé car la force est centrale).
// -----------------------------------------------------------------------------
vec3 geodesicAccel(vec3 x, float h2)
{
    float r2 = dot(x, x);
    float r5 = r2 * r2 * sqrt(r2);
    return -1.5 * RS * h2 * x / r5;
}

void main()
{
    // Rayon primaire : de la caméra à travers le pixel.
    vec2 ndc = (gl_FragCoord.xy / uResolution) * 2.0 - 1.0;
    float aspect = uResolution.x / uResolution.y;
    float tanHalf = tan(uFovY * 0.5);
    vec3 dir = normalize(uCamForward
                       + ndc.x * aspect * tanHalf * uCamRight
                       + ndc.y * tanHalf * uCamUp);

    vec3 pos = uCamPos;
    vec3 vel = dir;
    vec3 c = cross(pos, vel);
    float h2 = dot(c, c);

    vec3  color = vec3(0.0);
    float alpha = 0.0;           // opacité accumulée (disque semi-transparent)
    bool  captured = false;

    for (int i = 0; i < uMaxSteps; ++i) {
        float r = length(pos);

        // Horizon des événements franchi : le photon ne ressortira jamais.
        if (r < RS) { captured = true; break; }

        // Assez loin et s'éloignant : la déviation restante est négligeable.
        if (r > ESCAPE_R && dot(pos, vel) > 0.0) break;

        // Pas adaptatif : petits pas près du trou noir, grands pas loin.
        float dt = clamp(0.06 * r * r / (r + 4.0), 0.01, 2.0);

        // Intégration Runge-Kutta d'ordre 4.
        vec3 k1v = geodesicAccel(pos, h2);
        vec3 k1x = vel;
        vec3 k2v = geodesicAccel(pos + 0.5 * dt * k1x, h2);
        vec3 k2x = vel + 0.5 * dt * k1v;
        vec3 k3v = geodesicAccel(pos + 0.5 * dt * k2x, h2);
        vec3 k3x = vel + 0.5 * dt * k2v;
        vec3 k4v = geodesicAccel(pos + dt * k3x, h2);
        vec3 k4x = vel + dt * k3v;

        vec3 prev = pos;
        pos += dt / 6.0 * (k1x + 2.0 * k2x + 2.0 * k3x + k4x);
        vel += dt / 6.0 * (k1v + 2.0 * k2v + 2.0 * k3v + k4v);

        // Le rayon a-t-il traversé le plan du disque pendant ce pas ?
        if (uDisk == 1 && prev.y * pos.y < 0.0) {
            float t = prev.y / (prev.y - pos.y);
            vec3 hit = mix(prev, pos, t);
            vec4 d = accretionDisk(hit, vel);
            color += (1.0 - alpha) * d.rgb * d.a;
            alpha += (1.0 - alpha) * d.a;
            if (alpha > 0.99) break;
        }
    }

    if (!captured && alpha < 0.99) {
        // Le rayon s'est échappé : on lit le ciel dans sa direction déviée.
        color += (1.0 - alpha) * galaxyBackground(normalize(vel));
    }

    // Tone mapping (ACES approché) + correction gamma.
    color = (color * (2.51 * color + 0.03)) / (color * (2.43 * color + 0.59) + 0.14);
    color = pow(clamp(color, 0.0, 1.0), vec3(1.0 / 2.2));
    FragColor = vec4(color, 1.0);
}
