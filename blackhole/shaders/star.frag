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
//
//  Autour des etoiles a dynamo (src/corona.cpp), calcule sans pas
//  d'integration (rayons droits) :
//    - couronne : lumiere de la photosphere diffusee par les electrons
//      (couronne K), densite en r^-4 donc colonne en b^-3 (b : distance du
//      rayon au centre), jets coronaux pres de l'equateur au minimum du
//      cycle, structures radiales ;
//    - protuberances : rideaux de gaz froid dans un plan vertical, arches
//      au-dessus des regions actives, nuages en corotation. Roses (raie
//      H-alpha) au bord, filaments sombres devant le disque ;
//    - ejections de masse coronale : coquille brillante qui s'etend,
//      cavite sombre, coeur rose (la protuberance qui a eclate).
//
//  Etoiles a neutrons (pulsar, magnetar) : le champ magnetique est un dipole
//  incline de uMagTilt sur l'axe de rotation, qui tourne avec l'etoile.
//    - pulsar   : deux faisceaux coniques le long de l'axe magnetique. Le
//                 rayon les traverse et accumule leur lumiere (rendu
//                 volumique) : quand un faisceau passe face a la camera, on
//                 voit l'eclair, c'est l'impulsion du pulsar ;
//    - magnetar : lignes de champ du dipole, r = L sin^2(theta), tordues par
//                 les courants de la magnetosphere, et sursauts aleatoires ;
//    - les deux : calottes polaires chauffees par les particules qui
//                 retombent le long des lignes de champ.
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
uniform float uLensing;     // 1 = la gravite courbe la lumiere, 0 = lignes droites
uniform float uSkyGain;     // luminosite du fond de galaxie
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

// Couronne, protuberances, ejections (src/corona.cpp, repere du monde).
const int MAX_PROMS = 6;
const int MAX_CMES = 3;
uniform float uCorona;          // intensite de la couronne (0 = aucune)
uniform float uStreamers;       // 1 = minimum du cycle (jets coronaux equatoriaux)
uniform vec3  uCoronaTint;      // couleur de la couronne (lumiere de l'etoile diffusee)
uniform int   uPromCount;
uniform vec4  uPromA[MAX_PROMS]; // milieu au pied (unitaire), demi-longueur (rad ou taille)
uniform vec4  uPromB[MAX_PROMS]; // direction le long de la protuberance, hauteur
uniform vec4  uPromC[MAX_PROMS]; // type (0 rideau, 1 arche, 2 nuage), graine, eclat, decollage (0..1)
uniform int   uCmeCount;
uniform vec4  uCmeA[MAX_CMES];   // direction, distance parcourue par le front
uniform vec4  uCmeB[MAX_CMES];   // eclat, graine
const vec3 HALPHA = vec3(1.0, 0.2, 0.28);   // raie H-alpha (656 nm) + un peu de bleu (He, Ca)

uniform float uMagTilt;     // angle axe magnetique / axe de rotation (radians)
uniform float uBeam;        // intensite des faisceaux du pulsar (0 = aucun)
uniform float uField;       // visibilite des lignes de champ (0 a 1)
uniform float uBursts;      // 1 = sursauts de magnetar
uniform float uCaps;        // 1 = calottes polaires chaudes

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
    return -1.5 * uCompact * uLensing * h2 * x / r5;
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

// Axe magnetique dans le repere de l'etoile (fixe) et dans le repere du monde.
vec3 magAxisBody()
{
    return vec3(sin(uMagTilt), cos(uMagTilt), 0.0);
}

vec3 magAxisWorld()
{
    return rotateY(magAxisBody(), uRotation);
}

// Intensite des sursauts du magnetar a l'instant t : la plupart du temps
// nulle, parfois un pic bref (la croute craque sous la tension du champ).
float burstLevel()
{
    float n = valueNoise(vec3(uTime * 0.6, 1.7, 3.1));
    return uBursts * smoothstep(0.68, 0.9, n) * 5.0;
}

// Faisceaux du pulsar au point p : cone autour de l'axe magnetique, de plus
// en plus large en s'eloignant (ouverture ~10 degres).
vec3 pulsarBeams(vec3 p, vec3 axis)
{
    float s = dot(p, axis);
    float as = abs(s);
    float d = length(p - s * axis);
    float w = 0.08 + 0.17 * as;
    float core = exp(-d * d / (w * w)) * smoothstep(1.0, 1.8, as) / (1.0 + 0.12 * as);
    float flicker = 0.8 + 0.2 * sin(as * 1.3 - uTime * 9.0 + sign(s));
    return vec3(0.55, 0.72, 1.0) * core * flicker * uBeam * 0.9;
}

// Lignes de champ du dipole au point p (repere du monde) : 3 coquilles
// L = 1,5 ; 2,4 ; 3,8 rayons, 6 lignes chacune, tordues autour de l'axe.
vec3 fieldLines(vec3 p, float burst)
{
    float r = length(p);
    if (r < 1.0 || r > 7.0) return vec3(0.0);
    vec3 q = rotateY(p, -uRotation);        // repere de l'etoile
    vec3 m = magAxisBody();
    vec3 e1 = vec3(cos(uMagTilt), -sin(uMagTilt), 0.0);
    vec3 e2 = vec3(0.0, 0.0, 1.0);
    float cosT = dot(q, m) / r;
    float sin2 = max(1.0 - cosT * cosT, 1e-4);

    // Coquille la plus proche : L_k = 1,5 x 1,6^k.
    float k = clamp(floor(log(r / sin2 / 1.5) / log(1.6) + 0.5), 0.0, 2.0);
    float lk = 1.5 * pow(1.6, k);
    float dr = r - lk * sin2;

    // Ligne la plus proche en azimut, avec une torsion qui grandit vers
    // l'equateur magnetique (courants de la magnetosphere).
    float phi = atan(dot(q, e2), dot(q, e1)) + 0.6 * uField * cosT;
    float n = 6.0;
    float dphi = (fract(phi * n / 6.2831853 + 0.5) - 0.5) * 6.2831853 / n;
    float daz = r * sqrt(sin2) * dphi;

    float w = 0.07 + 0.03 * r;
    float tube = exp(-(dr * dr + daz * daz) / (w * w));
    float fade = exp(-(r - 1.0) * 0.35);
    return vec3(0.75, 0.45, 1.0) * tube * fade * uField * (1.0 + burst) * 1.6;
}

// Lumiere emise par la surface au point n (|n| = 1), vue sous l'angle mu.
vec3 surface(vec3 n, float mu)
{
    vec3 q = rotateY(n, -uRotation);

    // Granulation : centres chauds des cellules, bords sombres. Elle est forte
    // chez les etoiles froides (enveloppe convective), quasi absente chez les
    // etoiles chaudes (energie transportee par rayonnement).
    float s = uGranScale;
    float cells = 1.0 - abs(2.0 * valueNoise(q * s + vec3(0.0, uTime * 0.03, 0.0)) - 1.0);
    float fine  = valueNoise(q * s * 2.7 - vec3(uTime * 0.05));
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

    // Calottes polaires des etoiles a neutrons.
    float cap = uCaps * smoothstep(0.9, 0.985, abs(dot(q, magAxisBody())));
    T *= 1.0 + 0.8 * cap;

    float limb = 1.0 - uLimb * (1.0 - mu);
    // Luminance bolometrique : proportionnelle a T^4 (Stefan-Boltzmann).
    float t2 = tFactor * tFactor;
    float intensity = limb * gran * t2 * t2 * facBoost * pulse * (1.0 + 2.5 * cap);
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

// Couronne vue a cote de l'etoile : b = distance du rayon au centre,
// m = direction du point le plus proche (repere du monde).
vec3 coronaLight(float b, vec3 m)
{
    if (uCorona <= 0.0 || b <= 1.0 || b > 7.0) return vec3(0.0);
    vec3 q = rotateY(m, -uRotation);
    // Jets coronaux : au minimum du cycle, de longs "casques" pres de
    // l'equateur ; au maximum, une couronne ronde, herissee partout.
    // (q.y = sinus de la latitude ; ~ latitude pres de l'equateur.)
    float belt = exp(-q.y * q.y / 0.12);
    float n = valueNoise(q * 7.0);
    float rays = 0.45 + 0.9 * n * n;
    float shape = mix(1.0, 0.25 + 1.6 * belt, uStreamers) * rays;
    float reach = 1.0 + 1.5 * (b - 1.0) * mix(0.3, belt, uStreamers);   // les jets portent plus loin
    float col = shape * reach / (b * b * b);
    return uCoronaTint * col * uCorona * 0.35;
}

// Protuberances et ejections le long d'un rayon droit (pos + t dir).
// tHit : distance a la surface (hit = le rayon touche l'etoile). Renvoie
// la lumiere emise ; trans : fraction de la lumiere de la surface qui
// traverse les filaments ; cover : quantite de gaz froid au bord (il
// cache la lueur blanche autour de l'etoile).
vec3 activityLight(vec3 pos, vec3 dir, bool hit, float tHit, out float trans, out float cover)
{
    vec3 light = vec3(0.0);
    trans = 1.0;
    cover = 0.0;
    for (int i = 0; i < MAX_PROMS; ++i) {
        if (i >= uPromCount) break;
        vec3 c = uPromA[i].xyz;
        vec3 tg = uPromB[i].xyz;
        float h = uPromB[i].w;
        float type = uPromC[i].x;
        float seed = uPromC[i].y;
        float fade = uPromC[i].z;
        float col = 0.0;
        float t = 0.0;
        // Rejet rapide : sphere qui englobe la protuberance.
        float ext = type > 1.5 ? h : 0.5 * h;
        float rad = type > 1.5 ? 2.5 * uPromA[i].w : max(uPromA[i].w, 0.6 * h) + 0.6 * h;
        vec3 bc = c * (1.0 + ext);
        vec3 bo = pos + dot(bc - pos, dir) * dir - bc;
        if (dot(bo, bo) > rad * rad) continue;
        if (type > 1.5) {
            // Nuage en corotation : boule floue au-dessus de la surface.
            vec3 center = c * (1.0 + h);
            t = dot(center - pos, dir);
            vec3 d = pos + t * dir - center;
            float w = uPromA[i].w;
            col = exp(-dot(d, d) / (w * w)) * (0.7 + 0.8 * valueNoise(d * 6.0 / w + seed)) * 1.2;
        } else {
            // Rideau ou arche dans le plan vertical (c, tg).
            vec3 n = cross(c, tg);
            float dn = dot(dir, n);
            if (abs(dn) < 1e-4) continue;
            t = -dot(pos, n) / dn;
            vec3 q = pos + t * dir;
            float x = dot(q, c);
            if (t <= 0.0 || x <= 0.0) continue;
            float u = atan(dot(q, tg), x) / uPromA[i].w;     // -1..1 le long
            float z = length(q) - 1.0;                        // altitude
            if (abs(u) >= 1.0 || z <= 0.0) continue;
            float edge = 1.0 / max(abs(dn), 0.15);            // vu par la tranche : plus epais
            if (z > 1.25 * h) continue;
            if (type < 0.5) {
                // Rideau : sommet irregulier, fils de gaz verticaux.
                float top = h * (1.0 - u * u * u * u) * (0.7 + 0.5 * valueNoise(vec3(u * 2.5, seed, 0.0)));
                float body = 1.0 - smoothstep(0.75 * top, top, z);
                // Eruption : le bas se detache, il ne reste qu'une arche qui monte.
                float base = 0.6 * uPromC[i].w * top;
                body *= smoothstep(base - 0.01, base + 0.01, z);
                float threads = 0.45 + 0.75 * valueNoise(vec3(u * uPromA[i].w * 70.0, z * 10.0, seed));
                col = body * threads * edge * 1.0;
            } else {
                // Arche : boucle de champ magnetique remplie de plasma.
                float arc = h * sqrt(max(1.0 - u * u, 0.0));
                float w = 0.006 + 0.12 * h;
                float dz = (z - arc) / w;
                float strands = 0.5 + 0.8 * valueNoise(vec3(u * 9.0, dz * 2.0, seed));
                col = exp(-dz * dz) * strands * edge * 1.2;
            }
        }
        col *= fade;
        if (col <= 0.0 || t <= 0.0) continue;
        if (hit && t > tHit) continue;                        // derriere l'etoile
        if (hit) trans *= 1.0 - clamp(0.55 * col, 0.0, 0.8); // filament sombre sur le disque
        else { light += HALPHA * col; cover += col; }
    }

    for (int i = 0; i < MAX_CMES; ++i) {
        if (i >= uCmeCount) break;
        vec3 d0 = uCmeA[i].xyz;
        float front = uCmeA[i].w;
        float bright = uCmeB[i].x;
        float seed = uCmeB[i].y;
        // Bulle qui grandit en s'eloignant : rayon = moitie du chemin parcouru.
        float rb = 0.5 * front;
        vec3 center = d0 * (1.0 + 0.5 * front);
        float tc = dot(center - pos, dir);
        vec3 off = pos + tc * dir - center;
        float d2 = dot(off, off);
        float outer = 1.1 * rb;
        if (d2 > outer * outer) continue;
        // Quelques echantillons a travers la bulle (seulement les pixels
        // qui la traversent) : coquille de plasma, plus dense vers l'avant,
        // dechiree en filaments ; coeur rose en bas.
        float half_ = sqrt(outer * outer - d2);
        float t0 = tc - half_, t1 = tc + half_;
        if (hit) t1 = min(t1, tHit);
        t0 = max(t0, 0.0);
        if (t1 <= t0) continue;
        const int N = 6;
        float dt = (t1 - t0) / float(N);
        float shell = 0.0, core = 0.0;
        for (int k = 0; k < N; ++k) {
            vec3 p = pos + (t0 + (float(k) + 0.5) * dt) * dir;
            vec3 rel = p - center;
            float dd = length(rel) / rb;
            float m = dot(rel, d0) / (dd * rb + 1e-4);
            float layer = (dd - 0.9) / 0.1;
            float ws = exp(-layer * layer) * smoothstep(-0.4, 0.6, m);
            vec3 rc = rel + d0 * 0.4 * rb;
            float wc = exp(-dot(rc, rc) / (0.05 * rb * rb));
            // Ni coquille ni coeur ici (l'interieur de la bulle est vide) :
            // on saute le bruit, la partie chere de l'echantillon.
            if (ws + wc < 0.01) continue;
            float rough = valueNoise(rel * (4.0 / rb) + seed);
            shell += ws * (0.15 + 1.6 * rough * rough * rough);
            core += wc * (0.3 + rough);
        }
        float norm = dt / rb;
        vec3 l = (uCoronaTint * shell * 0.35 * 10.0 / 6.0 + HALPHA * core * 0.6) * norm * bright;
        if (hit) trans *= 1.0 - clamp(0.3 * bright * (shell + core) * norm, 0.0, 0.35);
        else light += l;
    }
    return light;
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
    vec3 glowSum = vec3(0.0);        // lumiere des faisceaux et lignes de champ
    bool hit = false;
    float rMin = length(pos);
    vec3 axis = magAxisWorld();
    bool volumetric = uBeam > 0.0 || uField > 0.0;
    float burst = burstLevel();      // meme valeur pour tout le pixel

    // Etoile peu compacte (tout sauf les etoiles a neutrons) : la lumiere
    // est deviee de moins de 0,1 degre, invisible a l'ecran. On remplace
    // l'integration pas a pas par l'intersection exacte droite / sphere.
    bool straight = uCompact * uLensing < 1.0e-3;
    float tHit = 0.0;
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
            tHit = t;
        }
    }
    // Couronne, protuberances, ejections (seulement pour les etoiles a
    // dynamo, toutes tracees en ligne droite).
    vec3 activity = vec3(0.0);
    float cover = 0.0;
    if (straight && (uPromCount > 0 || uCmeCount > 0)) {
        float trans;
        activity = activityLight(pos, dir, hit, tHit, trans, cover);
        color *= trans;
    }

    for (int i = 0; i < (straight ? 0 : uMaxSteps); ++i) {
        float r = length(pos);
        float radial = dot(pos, vel);
        if (r > ESCAPE_R && radial > 0.0) break;

        float dt = clamp(STEP * r, 0.01, 8.0);
        // Faisceaux et lignes de champ sont fins : pas plus courts autour.
        if (volumetric) {
            dt = min(dt, 0.15 + 0.08 * r);
            if (uField > 0.0 && r < 7.5) dt = min(dt, 0.2);
        }
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

        if (volumetric) {
            vec3 mid = 0.5 * (prev + pos);
            if (dot(mid, mid) > 1.0)
                glowSum += (pulsarBeams(mid, axis) + fieldLines(mid, burst)) * dt;
        }

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
    vec3 sky = texture(uSky, vel).rgb * uSkyGain;

    if (!hit) {
        // Halo : lumiere diffusee par la couronne et l'oeil, qui decroit avec
        // la distance a la surface.
        float above = max(rMin - 1.0, 0.0);
        vec3 tint = blackbody(uStarTemp);
        vec3 glow = tint * (0.6 * exp(-above * 10.0) + 0.1 * exp(-above * 1.8)) * EXPOSURE;
        glow /= 1.0 + 3.0 * cover;   // une protuberance devant la lueur la cache
        color = glow + sky * 0.6;
        if (straight) {
            vec3 closest = pos - dot(pos, dir) * dir;
            color += coronaLight(rMin, normalize(closest + vec3(0.0, 1e-5, 0.0))) * EXPOSURE;
        }
        // Sursaut de magnetar : tout l'environnement s'illumine un instant.
        color += vec3(0.7, 0.5, 1.0) * burst * 0.08 * exp(-above * 0.8);
    }
    color += (glowSum + activity) * EXPOSURE;

    FragColor = vec4(color, 1.0);
}
