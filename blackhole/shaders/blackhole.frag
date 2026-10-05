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
//  Dynamique du gaz dans le disque
//
//  Le gaz tourne à la vitesse angulaire képlérienne. En Schwarzschild, vue
//  depuis l'infini (temps coordonnée t), elle garde la forme newtonienne :
//      Ω(r) = dφ/dt = sqrt(M / r³)        avec M = rs / 2
//  Le centre tourne donc beaucoup plus vite que le bord : le gaz se cisaille
//  en longues traînées spirales. uTime est ce temps t, en unités rs/c.
// -----------------------------------------------------------------------------
const float M          = 0.5 * RS;
const int   NUM_CLUMPS = 14;      // amas de gaz chaud qui spiralent vers le trou noir
const float CLUMP_LIFE = 900.0;   // durée moyenne de la chute, de 12 rs à l'ISCO
const float FLOW_CYCLE = 60.0;    // période de renouvellement de la texture

float keplerOmega(float r)
{
    return sqrt(M / (r * r * r));
}

float fbm4(vec3 p)
{
    float v = 0.0, a = 0.5;
    for (int i = 0; i < 4; ++i) {
        v += a * noise3(p);
        p = p * 2.03 + 17.1;
        a *= 0.5;
    }
    return v;
}

// Turbulence transportée par l'écoulement képlérien. Au bout d'un moment le
// cisaillement étirerait la texture en anneaux infiniment fins ; on mélange
// donc deux couches décalées d'une demi-période qui se renouvellent tour à
// tour (technique du "flow map"), invisible à l'œil.
float diskTurbulence(float r, float phi, float t)
{
    float omega = keplerOmega(r);
    float c0 = t / FLOW_CYCLE;
    float c1 = c0 + 0.5;
    float a0 = phi - omega * fract(c0) * FLOW_CYCLE;
    float a1 = phi - omega * fract(c1) * FLOW_CYCLE;
    float n0 = fbm4(vec3(r * 1.6, cos(a0) * 3.0, sin(a0) * 3.0) + floor(c0) * 7.31);
    float n1 = fbm4(vec3(r * 1.6, cos(a1) * 3.0, sin(a1) * 3.0) + floor(c1) * 7.31 + 3.7);
    float w0 = 1.0 - abs(2.0 * fract(c0) - 1.0);   // w0 + w1 = 1
    float n = mix(n1, n0, w0);
    // Le mélange adoucit le contraste : on le rétablit.
    return clamp((n - 0.5) * 1.6 + 0.5, 0.0, 1.0);
}

// Amas de gaz chaud. Chacun orbite à Ω(r) tout en perdant lentement du
// moment cinétique (viscosité) : son rayon diminue de r0 à l'ISCO à vitesse
// radiale constante v_r. L'angle s'obtient en intégrant Ω exactement :
//     φ(t) = φ0 + ∫ Ω dt = φ0 + (2 sqrt(M) / v_r) (1/sqrt(r(t)) - 1/sqrt(r0))
// En tombant, l'amas chauffe : sa température suit T(r) du disque.
float diskClumps(float r, float phi, float t)
{
    float sum = 0.0;
    for (int i = 0; i < NUM_CLUMPS; ++i) {
        vec3 h = hash33(vec3(float(i) * 1.73 + 0.5, 3.1, 9.2));
        float life = CLUMP_LIFE * (0.7 + 0.6 * h.x);
        float age  = mod(t + h.y * life, life);
        float r0   = DISK_OUT - 1.0 - 2.5 * h.z;
        float vr   = (r0 - DISK_IN) / life;
        float rc   = r0 - vr * age;
        float ang  = h.x * 6.2831853 + 2.0 * sqrt(M) / vr * (inversesqrt(rc) - inversesqrt(r0));

        float dPhi = mod(phi - ang + PI, 2.0 * PI) - PI;   // écart angulaire dans [-π, π]
        float dr   = r - rc;
        float arc  = dPhi * rc;                             // distance le long de l'orbite
        // Forme allongée le long de l'orbite (cisaillement).
        float blob = exp(-dr * dr * 8.0 - arc * arc * 0.35);

        float s = age / life;
        float fade = smoothstep(0.0, 0.08, s) * smoothstep(1.0, 0.92, s);
        sum += blob * fade * (0.7 + 0.6 * h.y);
    }
    return sum;
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
    float phi = atan(p.z, p.x);

    // Profil de température d'un disque mince (Shakura-Sunyaev) :
    //   T(r) ∝ r^(-3/4) * (1 - sqrt(r_in / r))^(1/4)
    float x = DISK_IN / r;
    float profile = pow(x, 0.75) * pow(max(1.0 - sqrt(x), 0.0), 0.25);
    float T = 4500.0 * profile / 0.488;  // normalisé pour ~4500 K au maximum

    // Gaz turbulent et amas chauds : plus de matière = plus de chaleur.
    float turb   = diskTurbulence(r, phi, uTime);
    float clumps = diskClumps(r, phi, uTime);
    float rings  = 0.75 + 0.25 * sin(r * 5.0 + turb * 6.0);
    float heat   = (0.35 + 1.1 * turb * rings) + 2.2 * clumps;
    T *= 0.9 + 0.15 * turb + 0.35 * clumps;

    // Vitesse orbitale circulaire (vue par un observateur statique) :
    //   v = sqrt(M / (r - 2M)) = sqrt(0.5 / (r - 1))   avec rs = 2M = 1
    float beta  = sqrt(M / (r - RS));
    float gamma = 1.0 / sqrt(1.0 - beta * beta);
    vec3 orbitDir = vec3(-sin(phi), 0.0, cos(phi));    // sens de rotation du gaz (φ croissant)

    // Effet Doppler relativiste : le côté qui vient vers nous est plus
    // lumineux et plus bleu. La lumière va du disque vers la caméra = -rayDir.
    float cosTheta = dot(orbitDir, -normalize(rayDir));
    float doppler  = 1.0 / (gamma * (1.0 - beta * cosTheta));

    // Décalage gravitationnel vers le rouge : la lumière perd de l'énergie
    // en sortant du puits de potentiel.
    float gravShift = sqrt(1.0 - RS / r);

    float g = doppler * gravShift;
    vec3 color = blackbody(T * g);
    float intensity = pow(g, 4.0) * profile * 1.6 * heat;   // I_obs = g^4 * I_émis

    // Bords adoucis.
    float edge = smoothstep(DISK_IN, DISK_IN + 0.3, r) * smoothstep(DISK_OUT, DISK_OUT - 4.0, r);
    float alpha = clamp(edge * (0.55 + 0.6 * turb + 0.5 * clumps), 0.0, 1.0);

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
