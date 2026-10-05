#version 330 core

// Ray tracing d'un trou noir de Schwarzschild (sans rotation).
//
// Unités : le rayon de Schwarzschild vaut 1 (rs = 2GM/c² = 1).
//   - horizon des événements : r = 1
//   - sphère de photons      : r = 1.5
//   - dernière orbite stable : r = 3 (bord intérieur du disque)
//
// Pour un photon, l'équation des géodésiques de Schwarzschild peut s'écrire
// comme un mouvement dans l'espace euclidien soumis à une force centrale :
//     d²x/dt² = -1.5 * h² * x / r^5      avec h = |x × v| (moment cinétique)
// C'est exact pour la forme de la trajectoire r(φ), ce qui suffit pour
// l'image. On l'intègre pas à pas (RK4) pour chaque pixel.

in vec2 vUV;
out vec4 fragColor;

uniform vec2  uResolution;
uniform float uTime;
uniform vec3  uCamPos;
uniform mat3  uCamBasis;   // colonnes : droite, haut, avant
uniform float uFov;        // champ de vision vertical (radians)

const float HORIZON     = 1.0;
const float DISK_INNER  = 3.0;
const float DISK_OUTER  = 12.0;
const float ESCAPE_R    = 60.0;
const int   MAX_STEPS   = 400;

// ---------------------------------------------------------------------------
// Bruit et utilitaires
// ---------------------------------------------------------------------------

float hash13(vec3 p)
{
    p = fract(p * 0.1031);
    p += dot(p, p.zyx + 31.32);
    return fract((p.x + p.y) * p.z);
}

float hash12(vec2 p)
{
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

float noise2(vec2 p)
{
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash12(i), hash12(i + vec2(1, 0)), u.x),
               mix(hash12(i + vec2(0, 1)), hash12(i + vec2(1, 1)), u.x), u.y);
}

float fbm(vec2 p)
{
    float v = 0.0, a = 0.5;
    for (int i = 0; i < 5; ++i) {
        v += a * noise2(p);
        p *= 2.03;
        a *= 0.5;
    }
    return v;
}

// Couleur approximative d'un corps noir (température en kelvins).
vec3 blackbody(float t)
{
    t = clamp(t, 1000.0, 40000.0) / 100.0;
    vec3 c;
    c.r = t <= 66.0 ? 1.0 : clamp(1.29293618606 * pow(t - 60.0, -0.1332047592), 0.0, 1.0);
    c.g = t <= 66.0 ? clamp(0.39008157876 * log(t) - 0.63184144378, 0.0, 1.0)
                    : clamp(1.12989086089 * pow(t - 60.0, -0.0755148492), 0.0, 1.0);
    c.b = t >= 66.0 ? 1.0 : (t <= 19.0 ? 0.0 : clamp(0.54320678911 * log(t - 10.0) - 1.19625408914, 0.0, 1.0));
    return c;
}

// ---------------------------------------------------------------------------
// Fond étoilé (vu à travers la lentille gravitationnelle)
// ---------------------------------------------------------------------------

vec3 starLayer(vec3 dir, float scale, float density)
{
    vec3 p = dir * scale;
    vec3 cell = floor(p);
    vec3 f = fract(p) - 0.5;
    float h = hash13(cell);
    if (h > density)
        return vec3(0.0);
    vec3 offset = vec3(hash13(cell + 1.7), hash13(cell + 4.3), hash13(cell + 9.1)) - 0.5;
    float d = length(f - offset * 0.7);
    float brightness = pow(hash13(cell + 2.9), 6.0) * 3.0 + 0.2;
    vec3 tint = blackbody(mix(3000.0, 12000.0, hash13(cell + 5.5)));
    return tint * brightness * smoothstep(0.08, 0.0, d);
}

vec3 sky(vec3 dir)
{
    vec3 col = vec3(0.0);
    col += starLayer(dir, 90.0, 0.06);
    col += starLayer(dir, 220.0, 0.04) * 0.6;
    // Une vague bande de nébuleuse pour bien voir la distorsion.
    float band = exp(-pow(dir.y * 3.0 - 0.6 * dir.x, 2.0));
    float neb = fbm(vec2(atan(dir.z, dir.x) * 3.0, dir.y * 6.0));
    col += band * neb * neb * vec3(0.05, 0.03, 0.08);
    return col;
}

// ---------------------------------------------------------------------------
// Disque d'accrétion
// ---------------------------------------------------------------------------

// Renvoie la couleur émise (rgb) et l'opacité (a) au point p du disque,
// vu depuis un rayon de direction rayDir.
vec4 diskSample(vec3 p, vec3 rayDir)
{
    float r = length(p.xz);
    if (r < DISK_INNER || r > DISK_OUTER)
        return vec4(0.0);

    // Profil de température d'un disque mince (Shakura-Sunyaev).
    float x = DISK_INNER / r;
    float temp = 11000.0 * pow(x, 0.75) * pow(max(1.0 - sqrt(x), 0.0), 0.25);

    // Vitesse orbitale locale (fraction de c) et direction du mouvement.
    float beta = sqrt(0.5 / (r - 1.0));
    vec3 vel = normalize(vec3(-p.z, 0.0, p.x)) * beta;
    float gamma = 1.0 / sqrt(1.0 - beta * beta);

    // Décalage Doppler relativiste + décalage gravitationnel.
    vec3 toObserver = -rayDir;
    float doppler = 1.0 / (gamma * (1.0 - dot(vel, toObserver)));
    float gravShift = sqrt(1.0 - HORIZON / r);
    float g = doppler * gravShift;

    // Turbulence qui tourne avec le disque (rotation différentielle).
    float angle = atan(p.z, p.x);
    float omega = 0.5 / pow(r, 1.5);
    float swirl = fbm(vec2(angle * 4.0 + uTime * omega * 6.0, r * 1.6));
    swirl = mix(0.35, 1.25, swirl);

    vec3 color = blackbody(temp * g) * pow(g, 4.0) * swirl;

    // Bords adoucis.
    float edge = smoothstep(DISK_INNER, DISK_INNER + 0.4, r) * (1.0 - smoothstep(DISK_OUTER - 3.0, DISK_OUTER, r));
    float alpha = clamp(edge * swirl * 0.9, 0.0, 1.0);
    return vec4(color * 0.9 * edge, alpha);
}

// ---------------------------------------------------------------------------
// Intégration de la trajectoire du photon
// ---------------------------------------------------------------------------

vec3 acceleration(vec3 pos, float h2)
{
    float r2 = dot(pos, pos);
    float r5 = r2 * r2 * sqrt(r2);
    return -1.5 * h2 * pos / r5;
}

void rk4(inout vec3 pos, inout vec3 vel, float h2, float dt)
{
    vec3 k1v = acceleration(pos, h2);
    vec3 k1x = vel;
    vec3 k2v = acceleration(pos + 0.5 * dt * k1x, h2);
    vec3 k2x = vel + 0.5 * dt * k1v;
    vec3 k3v = acceleration(pos + 0.5 * dt * k2x, h2);
    vec3 k3x = vel + 0.5 * dt * k2v;
    vec3 k4v = acceleration(pos + dt * k3x, h2);
    vec3 k4x = vel + dt * k3v;
    pos += dt / 6.0 * (k1x + 2.0 * k2x + 2.0 * k3x + k4x);
    vel += dt / 6.0 * (k1v + 2.0 * k2v + 2.0 * k3v + k4v);
}

vec3 trace(vec3 origin, vec3 dir)
{
    vec3 pos = origin;
    vec3 vel = dir;
    vec3 L = cross(pos, vel);
    float h2 = dot(L, L);

    vec3 color = vec3(0.0);
    float transmittance = 1.0;

    for (int i = 0; i < MAX_STEPS; ++i) {
        float r = length(pos);

        // Absorbé par l'horizon : noir.
        if (r < HORIZON)
            return color;

        // Échappé vers l'infini : on regarde le ciel dans la direction finale.
        if (r > ESCAPE_R && dot(pos, vel) > 0.0)
            return color + transmittance * sky(normalize(vel));

        // Pas adaptatif : petit près du trou noir, grand loin de lui.
        float dt = clamp(0.06 * r, 0.02, 2.0);
        vec3 prev = pos;
        rk4(pos, vel, h2, dt);

        // Traversée du plan du disque (y = 0) entre deux pas.
        if (prev.y * pos.y < 0.0) {
            float t = prev.y / (prev.y - pos.y);
            vec3 hit = mix(prev, pos, t);
            vec4 d = diskSample(hit, normalize(vel));
            color += transmittance * d.rgb * d.a;
            transmittance *= 1.0 - d.a;
            if (transmittance < 0.01)
                return color;
        }
    }
    return color;
}

// ---------------------------------------------------------------------------

vec3 tonemapACES(vec3 x)
{
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}

void main()
{
    vec2 ndc = vUV * 2.0 - 1.0;
    float aspect = uResolution.x / uResolution.y;
    float tanHalf = tan(uFov * 0.5);
    vec3 dirCam = normalize(vec3(ndc.x * aspect * tanHalf, ndc.y * tanHalf, 1.0));
    vec3 dir = normalize(uCamBasis * dirCam);

    vec3 col = trace(uCamPos, dir);
    col = tonemapACES(col);
    col = pow(col, vec3(1.0 / 2.2));
    fragColor = vec4(col, 1.0);
}
