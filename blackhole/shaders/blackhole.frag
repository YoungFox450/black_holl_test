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
//
//  Ce shader écrit une couleur HDR (non bornée) dans une image de plus basse
//  résolution que la fenêtre ; present.frag l'agrandit puis fait le tone
//  mapping. Le #version et noise.glsl sont ajoutés par le C++.
// =============================================================================

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
uniform samplerCube uSky; // fond de galaxie précalculé (voir sky.frag)

// Étoile compagne d'un système double (src/binary.*). uCompanion.w = 0 : pas d'étoile.
uniform vec4  uCompanion;        // xyz : centre, w : demi-axe perpendiculaire (rs)
uniform vec3  uCompAxis;         // direction de l'étoile vers le trou noir
uniform float uCompStretch;      // allongement de marée le long de uCompAxis (1 = sphère)
uniform vec3  uCompVel;          // vitesse orbitale (fraction de c)
uniform float uCompTemp;         // température de surface (K)
uniform float uCompBrightness;
uniform float uCompIrradiation;  // température ajoutée par les rayons X du disque (K)
uniform float uCompAngle;        // angle de l'orbite (l'étoile montre toujours la même face)

// Réglages du disque d'accrétion (panneau de contrôle, voir src/ui.cpp).
uniform float uDiskIn;          // rayon intérieur (3 rs = ISCO)
uniform float uDiskOut;         // rayon extérieur
uniform float uDiskTemp;        // température maximale du gaz (K)
uniform float uDiskBrightness;  // multiplicateur de luminosité
uniform int   uClumpCount;      // nombre d'amas de gaz chaud (0 à MAX_CLUMPS)
uniform float uDoppler;         // 1 = effet Doppler relativiste, 0 = coupé
uniform float uGravShift;       // 1 = décalage gravitationnel vers le rouge, 0 = coupé
uniform float uJets;            // intensité des jets relativistes (0 = pas de jets)
uniform float uJetBeta;         // vitesse du plasma des jets (fraction de c)
uniform float uJetWidth;        // largeur relative des jets (1 = normale)
uniform float uLensing;         // 1 = la gravité courbe la lumière, 0 = lignes droites
uniform float uSkyGain;         // luminosité du fond de galaxie

const float RS         = 1.0;   // rayon de Schwarzschild (horizon)
const float ESCAPE_R   = 60.0;  // au-delà, le rayon est considéré libre
const float PHOTON_R   = 1.5;   // sphère de photons : 3GM/c² = 1,5 rs
const float STEP       = 0.2;   // pas = STEP * r : environ 0,2 radian d'arc par pas
const float PI         = 3.14159265359;

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
    // Cette approximation donne une couleur sRGB (déjà corrigée gamma). Le rendu
    // travaille en lumière linéaire et present.frag réapplique le gamma : sans
    // cette conversion, les couleurs sont délavées (une étoile à 3000 K paraît
    // crème au lieu d'orangée).
    return pow(c, vec3(2.2));
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
const int   MAX_CLUMPS = 32;      // amas de gaz chaud qui spiralent vers le trou noir
const float CLUMP_LIFE = 900.0;   // durée moyenne de la chute jusqu'au bord intérieur
const float FLOW_CYCLE = 60.0;    // période de renouvellement de la texture

float keplerOmega(float r)
{
    return sqrt(M / (r * r * r));
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
    float n0 = fbm(vec3(r * 1.6, cos(a0) * 3.0, sin(a0) * 3.0) + floor(c0) * 7.31, 4);
    float n1 = fbm(vec3(r * 1.6, cos(a1) * 3.0, sin(a1) * 3.0) + floor(c1) * 7.31 + 3.7, 4);
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
    for (int i = 0; i < MAX_CLUMPS; ++i) {
        if (i >= uClumpCount) break;
        vec3 h = hash33(vec3(float(i) * 1.73 + 0.5, 3.1, 9.2));
        float life = CLUMP_LIFE * (0.7 + 0.6 * h.x);
        float age  = mod(t + h.y * life, life);
        float r0   = max(uDiskOut - 1.0 - 2.5 * h.z, uDiskIn + 0.5);
        float vr   = (r0 - uDiskIn) / life;
        float rc   = r0 - vr * age;
        float ang  = h.x * 6.2831853 + 2.0 * sqrt(M) / vr * (inversesqrt(rc) - inversesqrt(r0));

        float dPhi = mod(phi - ang + PI, 2.0 * PI) - PI;   // écart angulaire dans [-π, π]
        float dr   = r - rc;
        float arc  = dPhi * rc;                             // distance le long de l'orbite
        // Forme allongée le long de l'orbite (cisaillement).
        float blob = exp(-dr * dr * 8.0 - arc * arc * 0.35);

        float s = age / life;
        float fade = smoothstep(0.0, 0.08, s) * (1.0 - smoothstep(0.92, 1.0, s));   // (bord0 > bord1 : non défini en GLSL)
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
    if (r < uDiskIn || r > uDiskOut) return vec4(0.0);
    float phi = atan(p.z, p.x);

    // Profil de température d'un disque mince (Shakura-Sunyaev) :
    //   T(r) ∝ r^(-3/4) * (1 - sqrt(r_in / r))^(1/4)
    float x = uDiskIn / r;
    float profile = pow(x, 0.75) * pow(max(1.0 - sqrt(x), 0.0), 0.25);
    float T = uDiskTemp * profile / 0.488;  // 0,488 = maximum du profil : T max = uDiskTemp

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
    float doppler  = mix(1.0, 1.0 / (gamma * (1.0 - beta * cosTheta)), uDoppler);

    // Décalage gravitationnel vers le rouge : la lumière perd de l'énergie
    // en sortant du puits de potentiel.
    float gravShift = mix(1.0, sqrt(1.0 - RS / r), uGravShift);

    float g = doppler * gravShift;
    vec3 color = blackbody(T * g);
    float intensity = pow(g, 4.0) * profile * uDiskBrightness * heat;   // I_obs = g^4 * I_émis

    // Bords adoucis.
    // (smoothstep avec bord0 > bord1 n'est pas défini en GLSL : 1 - smoothstep.)
    float fadeOut = min(4.0, 0.4 * (uDiskOut - uDiskIn));
    float edge = smoothstep(uDiskIn, uDiskIn + 0.3, r) * (1.0 - smoothstep(uDiskOut - fadeOut, uDiskOut, r));
    float alpha = clamp(edge * (0.55 + 0.6 * turb + 0.5 * clumps), 0.0, 1.0);

    return vec4(color * intensity * edge, alpha);
}

// -----------------------------------------------------------------------------
//  Jets relativistes (quasar, noyau actif de galaxie)
//
//  Le champ magnétique du disque, enroulé par la rotation, éjecte du plasma
//  le long de l'axe de rotation (y) à une vitesse proche de c. On le rend en
//  volume : à chaque pas, le rayon accumule la lumière du jet qu'il traverse.
//  Le plasma va à β = uJetBeta (0,8 par défaut) : le jet qui vient vers nous est amplifié par
//  l'effet Doppler (δ³) et celui qui s'éloigne presque éteint. C'est pour
//  cela que beaucoup de quasars ne montrent qu'un seul jet.
//  Les "nœuds" sont des chocs internes qui remontent le jet.
// -----------------------------------------------------------------------------
float gJetGamma;   // facteur de Lorentz des jets, calculé une fois par pixel dans main()

vec3 jetEmission(vec3 p, vec3 rayDir)
{
    float ay = abs(p.y);
    float d = length(p.xz);
    float w = (0.25 + 0.07 * ay) * uJetWidth;        // le jet s'ouvre lentement
    float core = exp(-d * d / (w * w)) * smoothstep(1.5, 3.5, ay) * exp(-ay / 40.0);
    if (core < 1e-4) return vec3(0.0);

    float side = p.y > 0.0 ? 1.0 : -1.0;
    float knots = 0.45 + 0.9 * pow(noise3(vec3(side * 7.0, ay * 0.3 - uTime * 0.02, 0.0)), 2.0);

    vec3 jetDir = vec3(0.0, side, 0.0);
    float cosTheta = dot(jetDir, -normalize(rayDir));
    float delta = 1.0 / (gJetGamma * (1.0 - uJetBeta * cosTheta));
    float beaming = mix(1.0, delta * delta * delta, uDoppler);

    // Rayonnement synchrotron : bleuté, coeur plus blanc.
    vec3 color = mix(vec3(0.35, 0.5, 1.0), vec3(0.9, 0.95, 1.0), exp(-d * d / (0.2 * w * w)));
    return color * core * knots * beaming * uJets * 1.2;
}

// -----------------------------------------------------------------------------
//  Étoile compagne (système double)
//
//  L'étoile est un ellipsoïde allongé vers le trou noir par la marée. Le ray
//  tracer teste chaque segment du rayon courbé contre elle : sa lumière est
//  donc déviée comme celle du disque, et on voit une seconde image de
//  l'étoile de l'autre côté du trou noir quand elle passe derrière.
//  Pour l'intersection, on comprime l'espace le long de l'axe de marée :
//  l'ellipsoïde devient une sphère de rayon uCompanion.w.
// -----------------------------------------------------------------------------
vec3 squashCompanion(vec3 v)
{
    return v + uCompAxis * dot(v, uCompAxis) * (1.0 / uCompStretch - 1.0);
}

// Fraction (0..1) du segment a -> b où il entre dans l'étoile, ou -1.
float companionHit(vec3 a, vec3 b)
{
    vec3 A = squashCompanion(a - uCompanion.xyz);
    vec3 D = squashCompanion(b - uCompanion.xyz) - A;
    float qa = dot(D, D);
    float qb = dot(A, D);
    float qc = dot(A, A) - uCompanion.w * uCompanion.w;
    float disc = qb * qb - qa * qc;
    if (disc < 0.0 || qc < 0.0) return -1.0;   // manqué, ou caméra dans l'étoile
    float t = (-qb - sqrt(disc)) / qa;
    return (t >= 0.0 && t <= 1.0) ? t : -1.0;
}

vec3 companionColor(vec3 p, vec3 rayDir)
{
    // Normale de l'ellipsoïde : gradient de |squash(p - c)|².
    vec3 n = normalize(squashCompanion(squashCompanion(p - uCompanion.xyz)));
    vec3 toCam = -normalize(rayDir);
    vec3 toHole = -normalize(p);

    // Assombrissement centre-bord : au bord, on voit des couches plus hautes et plus froides.
    float mu = max(dot(n, toCam), 0.0);
    float limb = 1.0 - 0.7 * (1.0 - mu);

    // Assombrissement gravitationnel (von Zeipel, T ∝ g^1/4) : la pointe vers
    // L1, où la gravité de l'étoile est presque annulée, est plus froide.
    float tip = max(dot(n, uCompAxis), 0.0);
    float T = uCompTemp * (1.0 - 0.2 * (uCompStretch - 1.0) * tip * tip * tip);

    // La face tournée vers le trou noir est chauffée par les rayons X du disque.
    float lit = max(dot(n, toHole), 0.0);
    float Tirr = uCompIrradiation * sqrt(lit);
    T = pow(T * T * T * T + Tirr * Tirr * Tirr * Tirr, 0.25);

    // Granulation, fixe sur l'étoile (rotation synchrone avec l'orbite).
    float ca = cos(uCompAngle), sa = sin(uCompAngle);
    vec3 nl = vec3(ca * n.x + sa * n.z, n.y, -sa * n.x + ca * n.z);
    float gran = fbm(nl * 7.0 + vec3(0.0, uTime * 0.0004, 0.0), 3);
    T *= 0.94 + 0.12 * gran;

    // Effet Doppler (vitesse orbitale) et décalage gravitationnel.
    float b2 = dot(uCompVel, uCompVel);
    float dop = 1.0 / (sqrt(1.0 - b2) * (1.0 - dot(uCompVel, toCam)));
    float g = mix(1.0, dop, uDoppler) * mix(1.0, sqrt(max(1.0 - RS / length(p), 0.0)), uGravShift);

    float boost = pow(T / uCompTemp, 2.0);
    return blackbody(T * g) * pow(g, 4.0) * limb * boost * uCompBrightness;
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
    return -1.5 * RS * h2 * x / r5;   // appelé seulement avec la lentille
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
    gJetGamma = inversesqrt(1.0 - uJetBeta * uJetBeta);

    for (int i = 0; i < uMaxSteps; ++i) {
        float r = length(pos);
        float radial = dot(pos, vel);   // < 0 : le rayon se rapproche du trou noir

        // Sous la sphère de photons et en train de descendre : aucune
        // trajectoire de lumière ne remonte de là (pas de point de rebroussement
        // sous 1,5 rs), le rayon finira dans l'horizon. Inutile de le suivre
        // jusqu'au bout. Le disque commence à 3 rs : rien à ajouter en route.
        // (Sans lentille, les rayons vont tout droit : seul l'horizon les arrête.)
        if (r < RS || (uLensing > 0.5 && r < PHOTON_R && radial < 0.0)) { captured = true; break; }

        // Assez loin et s'éloignant : la déviation restante est négligeable.
        if (r > ESCAPE_R && radial > 0.0) break;

        // Pas adaptatif proportionnel à r : chaque pas balaie ~0,2 radian
        // autour du trou noir, la même précision partout. Comparé à un rendu de
        // référence 20 fois plus fin, l'écart est invisible (PSNR 60 dB), pour
        // ~8 fois moins de pas qu'avant près de la sphère de photons.
        float dt = clamp(STEP * r, 0.02, 8.0);
        if (uJets > 0.0) {
            // Jets fins : pas plus courts, mais seulement près de l'axe. Hors
            // du cylindre où le jet émet (exp(-d²/w²) > 1e-4, soit d < 3,1 w),
            // le pas peut aller jusqu'au bord de ce cylindre, qui s'élargit
            // avec |y| (marge de 20 % pour la courbure du rayon).
            float jd = length(pos.xz);
            float jr = 3.1 * (0.25 + 0.07 * abs(pos.y)) * uJetWidth;
            float free = (jd - jr) * 0.8 / (1.0 + 0.22 * uJetWidth);
            dt = min(dt, max(0.3 + 0.08 * r, free));
        }

        vec3 prev = pos;
        if (uLensing < 0.5) {
            // Sans lentille le rayon va tout droit : un pas exact, sans les
            // quatre évaluations de Runge-Kutta (même branche pour tous les
            // pixels, donc gratuite sur le GPU).
            pos += dt * vel;
        } else {
            // Intégration Runge-Kutta d'ordre 4.
            vec3 k1v = geodesicAccel(pos, h2);
            vec3 k1x = vel;
            vec3 k2v = geodesicAccel(pos + 0.5 * dt * k1x, h2);
            vec3 k2x = vel + 0.5 * dt * k1v;
            vec3 k3v = geodesicAccel(pos + 0.5 * dt * k2x, h2);
            vec3 k3x = vel + 0.5 * dt * k2v;
            vec3 k4v = geodesicAccel(pos + dt * k3x, h2);
            vec3 k4x = vel + dt * k3v;

            pos += dt / 6.0 * (k1x + 2.0 * k2x + 2.0 * k3x + k4x);
            vel += dt / 6.0 * (k1v + 2.0 * k2v + 2.0 * k3v + k4v);
        }

        if (uJets > 0.0)
            color += (1.0 - alpha) * jetEmission(0.5 * (prev + pos), vel) * dt;

        // L'étoile compagne est-elle sur ce segment ?
        // (Un "if" et non un "? :" : le compilateur évalue souvent les deux
        // côtés d'un "? :", même sans étoile. Test grossier de la sphère
        // englobante d'abord : la plupart des segments passent loin d'elle.)
        float tComp = -1.0;
        if (uCompanion.w > 0.0) {
            vec3 mid = 0.5 * (prev + pos) - uCompanion.xyz;
            float reach = uCompanion.w * uCompStretch + 0.5 * dt * length(vel);
            if (dot(mid, mid) < reach * reach)
                tComp = companionHit(prev, pos);
        }

        // Le rayon a-t-il traversé le plan du disque pendant ce pas ?
        // (Seulement devant l'étoile compagne si elle est sur le segment.)
        if (uDisk == 1 && prev.y * pos.y < 0.0 && (tComp < 0.0 || prev.y / (prev.y - pos.y) < tComp)) {
            float t = prev.y / (prev.y - pos.y);
            vec3 hit = mix(prev, pos, t);
            vec4 d = accretionDisk(hit, vel);
            color += (1.0 - alpha) * d.rgb * d.a;
            alpha += (1.0 - alpha) * d.a;
            if (alpha > 0.99) break;
        }

        // L'étoile est opaque : le rayon s'arrête sur sa surface.
        if (tComp >= 0.0) {
            color += (1.0 - alpha) * companionColor(mix(prev, pos, tComp), vel);
            alpha = 1.0;
            break;
        }
    }

    // Lecture du ciel HORS de tout "if" : la carte graphique calcule le niveau
    // de mipmap à partir des pixels voisins, ce qui n'est défini que si tous
    // les pixels exécutent la lecture. Les mipmaps lissent le ciel là où la
    // lentille le comprime énormément (près de l'ombre) : pas de scintillement.
    vec3 sky = texture(uSky, vel).rgb * uSkyGain;

    if (!captured && alpha < 0.99) {
        // Le rayon s'est échappé : on lit le ciel dans sa direction déviée.
        color += (1.0 - alpha) * sky;
    }

    FragColor = vec4(color, 1.0);
}
