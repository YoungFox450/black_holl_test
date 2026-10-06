// =============================================================================
//  Ray tracing d'une etoile
//
//  Meme principe que blackhole.frag : un rayon par pixel, courbe par la
//  gravite (equation des geodesiques de Schwarzschild). Ici l'unite de
//  longueur est le RAYON DE L'ETOILE (R = 1) et uCompact = rs / R :
//    - Soleil         : rs / R ~ 4e-6  -> rayons pratiquement droits
//    - naine blanche  : rs / R ~ 3e-4
//    - etoile a neutrons : rs / R ~ 0,35 -> on voit une partie de l'arriere
//  Quand un rayon touche la surface, on calcule la lumiere emise :
//    - corps noir a la temperature effective T (decalee vers le rouge par la
//      gravite : T_obs = T sqrt(1 - rs/R))
//    - assombrissement centre-bord : I(mu) = 1 - u (1 - mu)
//    - granulation : cellules de convection, dont la taille suit la gravite
//    - activite magnetique (src/activity.cpp) : taches (ombre + penombre)
//      dans les bandes de latitude du cycle (loi de Sporer), facules
//      brillantes pres du bord, rotation differentielle, eruptions
//    - oscillations de luminosite (geantes rouges)
//  Les parametres viennent du modele physique (src/star.cpp, src/activity.cpp).
// =============================================================================

out vec4 FragColor;

uniform vec2  uResolution;
uniform float uTime;        // temps de simulation (secondes)
uniform vec3  uCamPos;
uniform vec3  uCamRight;
uniform vec3  uCamUp;
uniform vec3  uCamForward;
uniform float uFovY;
uniform int   uMaxSteps;
uniform samplerCube uSky;

uniform float uStarTemp;    // temperature effective (K)
uniform float uCompact;     // rs / R
uniform float uLimb;        // coefficient d'assombrissement centre-bord u
uniform float uGranScale;   // cellules de convection par rayon
uniform float uRotation;    // angle de rotation de l'equateur (rad)
uniform float uPulse;       // variation relative de luminosite (oscillations)

// Taches. La probabilite qu'un point soit dans une tache depend de sa
// latitude : deux bandes gaussiennes (cycle en cours, cycle precedent).
uniform int   uSpotsOn;
uniform vec4  uBand;        // (latitude 1, amplitude 1, latitude 2, amplitude 2)
uniform float uBandWidth;   // ecart-type des bandes (rad)
uniform float uFaculaRatio; // aire des facules / aire des taches
uniform float uUmbraRatio;  // T_ombre / T
uniform float uPenumbraRatio;
// Deux generations de taches qui se relaient (naissance puis disparition) :
// (poids, cisaillement accumule depuis la naissance en rad, graine).
uniform vec3  uSpotLayer[2];

// Eruptions : direction du centre (repere fixe), s = 2 x aire relative,
// intensite du profil temporel (0..1).
const int MAX_FLARES = 4;
uniform int   uFlareCount;
uniform vec4  uFlarePos[MAX_FLARES];
uniform float uFlareAmp[MAX_FLARES];
const float FLARE_TEMP = 9000.0;

// Seuils du bruit fbm(3 octaves) depasses par une fraction 10^-k des points,
// k = 0 ; 0,5 ; ... ; 4 (mesures, voir kSpotNoiseQuantiles dans activity.hpp).
const float SPOT_Q[9] = float[9](0.0826, 0.4808, 0.5599, 0.6222, 0.6624,
                                 0.6875, 0.7012, 0.7118, 0.7226);

const float ESCAPE_R = 80.0;
const float STEP     = 0.2;
const float EXPOSURE = 0.8;

vec3 blackbody(float T)
{
    T = clamp(T, 1000.0, 40000.0) / 100.0;
    vec3 c;
    c.r = T <= 66.0 ? 1.0 : clamp(1.29293618 * pow(T - 60.0, -0.1332047592), 0.0, 1.0);
    c.g = T <= 66.0 ? clamp(0.39008157 * log(T) - 0.63184144, 0.0, 1.0)
                    : clamp(1.12989086 * pow(T - 60.0, -0.0755148492), 0.0, 1.0);
    c.b = T >= 66.0 ? 1.0 : (T <= 19.0 ? 0.0 : clamp(0.54320678 * log(T - 10.0) - 1.19625408, 0.0, 1.0));
    // Cette approximation donne une couleur sRGB (déjà corrigée gamma). Le rendu
    // travaille en lumière linéaire et present.frag réapplique le gamma : sans
    // cette conversion, les couleurs sont délavées (une étoile à 3000 K paraît
    // crème au lieu d'orangée).
    return pow(c, vec3(2.2));
}

vec3 geodesicAccel(vec3 x, float h2)
{
    float r2 = dot(x, x);
    float r5 = r2 * r2 * sqrt(r2);
    return -1.5 * uCompact * h2 * x / r5;
}

// Rotation autour de l'axe y (axe de rotation de l'etoile).
vec3 rotateY(vec3 p, float a)
{
    float c = cos(a), s = sin(a);
    return vec3(c * p.x + s * p.z, p.y, -s * p.x + c * p.z);
}

// Seuil de bruit au-dessus duquel se trouve une fraction p de la surface.
float noiseThreshold(float p)
{
    if (p < 1e-5) return 2.0;                 // jamais atteint
    float k = clamp(-log(p) / log(10.0), 0.0, 3.999) * 2.0;
    int i = int(k);
    return mix(SPOT_Q[i], SPOT_Q[i + 1], k - float(i));
}

// Couverture locale des taches selon la latitude (lat = |latitude|).
float spotProbability(float lat)
{
    float a = (lat - uBand.x) / uBandWidth;
    float b = (lat - uBand.z) / uBandWidth;
    return min(uBand.y * exp(-0.5 * a * a) + uBand.w * exp(-0.5 * b * b), 0.5);
}

// Lumiere emise par la surface au point n (|n| = 1), vue sous l'angle mu.
vec3 surface(vec3 n, float mu)
{
    vec3 q = rotateY(n, -uRotation);

    // Granulation : centres chauds des cellules, bords sombres. Elle est forte
    // chez les etoiles froides (enveloppe convective), quasi absente chez les
    // etoiles chaudes (energie transportee par rayonnement).
    float s = uGranScale;
    float cells = 1.0 - abs(2.0 * noise3(q * s + vec3(0.0, uTime * 0.03, 0.0)) - 1.0);
    float fine  = noise3(q * s * 2.7 - vec3(uTime * 0.05));
    float convective = clamp((8000.0 - uStarTemp) / 4000.0, 0.05, 1.0);
    float gran = 1.0 + convective * (0.45 * (cells - 0.6) + 0.12 * (fine - 0.5));

    // Taches et facules.
    float spot = 0.0, umbra = 0.0, facula = 0.0;
    if (uSpotsOn != 0) {
        float sinLat2 = n.y * n.y;
        float p = spotProbability(asin(abs(n.y)));
        float tSpot = noiseThreshold(p);
        float tUmbra = noiseThreshold(0.2 * p);            // ombre : ~20 % de la tache
        float tFac = noiseThreshold(p * (1.0 + uFaculaRatio));
        for (int k = 0; k < 2; ++k) {
            vec3 layer = uSpotLayer[k];
            if (layer.x <= 0.001) continue;
            // Rotation differentielle : Omega(lat) = Omega_eq (1 - alpha sin^2 lat).
            vec3 qs = rotateY(n, -uRotation + layer.y * sinLat2);
            float v = fbm(qs * 3.5 + vec3(11.0) + layer.z * vec3(7.13, 3.71, 5.29), 3);
            float sk = smoothstep(tSpot - 0.008, tSpot + 0.008, v);
            spot   += layer.x * sk;
            umbra  += layer.x * smoothstep(tUmbra - 0.008, tUmbra + 0.008, v);
            facula += layer.x * smoothstep(tFac - 0.008, tFac + 0.008, v) * (1.0 - sk);
        }
    }
    // Temperature locale : ombre et penombre plus froides que la photosphere.
    float tFactor = 1.0 - spot * (1.0 - uPenumbraRatio) - umbra * (uPenumbraRatio - uUmbraRatio);
    // Facules : contraste quasi nul au centre du disque, ~15 % pres du bord.
    float facBoost = 1.0 + facula * 0.15 * (1.0 - mu);

    // Decalage gravitationnel vers le rouge.
    float g = sqrt(max(1.0 - uCompact, 0.05));
    float pulse = 1.0 + uPulse;
    float T = uStarTemp * g * tFactor * mix(1.0, gran, 0.5) * sqrt(sqrt(facBoost * pulse));

    float limb = 1.0 - uLimb * (1.0 - mu);
    // Luminance bolometrique : proportionnelle a T^4 (Stefan-Boltzmann).
    float t2 = tFactor * tFactor;
    float intensity = limb * gran * t2 * t2 * facBoost * pulse;
    vec3 color = blackbody(T) * intensity;

    // Eruptions : plasma a ~9 000 K. Brillance relative (T_e / T)^4, sur une
    // aire qui donne la bonne luminosite totale (calculee dans activity.cpp).
    if (uFlareCount > 0) {
        vec3 flareColor = blackbody(FLARE_TEMP * g);
        float ratio = FLARE_TEMP / uStarTemp;
        float contrast = ratio * ratio * ratio * ratio;
        for (int i = 0; i < MAX_FLARES; ++i) {
            if (i >= uFlareCount) break;
            float d = 1.0 - dot(n, uFlarePos[i].xyz);
            color += flareColor * contrast * uFlareAmp[i] * exp(-d / uFlarePos[i].w);
        }
    }
    return color * EXPOSURE;
}

void main()
{
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

    vec3 color = vec3(0.0);
    bool hit = false;
    float rMin = length(pos);

    // Etoile peu compacte (tout sauf les etoiles a neutrons) : la lumiere
    // est deviee de moins de 0,1 degre, invisible a l'ecran. On remplace
    // l'integration pas a pas par l'intersection exacte droite / sphere.
    bool straight = uCompact < 1.0e-3;
    if (straight) {
        float b = dot(pos, dir);
        float cc = dot(pos, pos) - 1.0;
        float disc = b * b - cc;
        rMin = b < 0.0 ? sqrt(max(cc + 1.0 - b * b, 0.0)) : length(pos);
        float t = -b - sqrt(max(disc, 0.0));
        if (disc > 0.0 && t > 0.0) {
            vec3 p = normalize(pos + t * dir);
            color = surface(p, clamp(dot(p, -dir), 0.0, 1.0));
            hit = true;
        }
    }

    for (int i = 0; i < (straight ? 0 : uMaxSteps); ++i) {
        float r = length(pos);
        float radial = dot(pos, vel);
        if (r > ESCAPE_R && radial > 0.0) break;

        float dt = clamp(STEP * r, 0.01, 8.0);
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

        // Le pas a-t-il traverse la surface ? Le segment est presque droit :
        // on resout |prev + t (pos - prev)| = 1 pour trouver le point d'impact.
        if (length(pos) < 1.0) {
            vec3 d = pos - prev;
            float a = dot(d, d);
            float b = 2.0 * dot(prev, d);
            float cc = dot(prev, prev) - 1.0;
            float t = (-b - sqrt(max(b * b - 4.0 * a * cc, 0.0))) / (2.0 * a);
            vec3 p = normalize(prev + clamp(t, 0.0, 1.0) * d);
            float mu = clamp(dot(p, -normalize(vel)), 0.0, 1.0);
            color = surface(p, mu);
            hit = true;
            break;
        }
        rMin = min(rMin, length(pos));
    }

    // Lecture du ciel hors de tout "if" (mipmaps, voir blackhole.frag).
    vec3 sky = texture(uSky, vel).rgb;

    if (!hit) {
        // Halo : lumiere diffusee par la couronne et l'oeil, qui decroit avec
        // la distance a la surface.
        float above = max(rMin - 1.0, 0.0);
        vec3 tint = blackbody(uStarTemp);
        vec3 glow = tint * (0.6 * exp(-above * 10.0) + 0.1 * exp(-above * 1.8)) * EXPOSURE;
        color = glow + sky * 0.6;
    }

    FragColor = vec4(color, 1.0);
}
