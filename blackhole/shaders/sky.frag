// =============================================================================
//  Fond de l'univers : champ d'étoiles + bande de galaxie (type Voie lactée).
//
//  Le fond ne dépend que de la DIRECTION. Au lieu de le recalculer pour chaque
//  pixel à chaque image (~300 appels de bruit par pixel, le plus gros coût sur
//  une carte graphique intégrée), on le calcule UNE fois au démarrage dans une
//  cubemap (6 faces). Le ray tracer n'a plus qu'à lire une texture.
// =============================================================================

uniform int   uFace;      // face de la cubemap en cours de calcul (0..5)
uniform float uFaceSize;  // taille d'une face en texels

out vec4 FragColor;

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
        // smoothstep(a, b, x) avec a > b n'est pas défini en GLSL (les pilotes
        // Intel peuvent renvoyer n'importe quoi) : on écrit 1 - smoothstep.
        float b = 1.0 - smoothstep(0.0, size, d);
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
    float clouds = fbm(dir * 6.0, 5);
    float dust   = smoothstep(0.45, 0.75, fbm(dir * 11.0 + 3.0, 5));  // bandes de poussière
    float glow   = band * (0.35 + 0.9 * clouds) * (1.0 - 0.8 * dust * band);
    glow *= 0.4 + 1.6 * pow(toCenter, 4.0);

    vec3 bandCol = mix(vec3(0.35, 0.45, 0.9), vec3(1.0, 0.8, 0.55), pow(toCenter, 2.0));
    vec3 col = bandCol * glow * 0.35;

    // Nébuleuses colorées très diffuses.
    float neb = fbm(dir * 2.5 + 11.0, 5);
    col += vec3(0.5, 0.15, 0.35) * pow(neb, 6.0) * 0.25;
    col += vec3(0.1, 0.3, 0.5)   * pow(fbm(dir * 3.0 - 7.0, 5), 6.0) * 0.3;

    // Étoiles : plus nombreuses dans la bande galactique.
    col += starLayer(dir, 80.0, 0.12 + 0.3 * band);
    col += starLayer(dir, 220.0, 0.10 + 0.3 * band) * 0.7;

    return col;
}

// Direction 3D d'un point (s, t) ∈ [-1, 1]² d'une face, convention OpenGL.
vec3 cubeDirection(int face, vec2 st)
{
    float s = st.x, t = st.y;
    if (face == 0) return vec3( 1.0,  -t,  -s);   // +X
    if (face == 1) return vec3(-1.0,  -t,   s);   // -X
    if (face == 2) return vec3(   s, 1.0,   t);   // +Y
    if (face == 3) return vec3(   s, -1.0, -t);   // -Y
    if (face == 4) return vec3(   s,  -t, 1.0);   // +Z
    return               vec3(  -s,  -t, -1.0);   // -Z
}

void main()
{
    // 4 échantillons par texel : les petites étoiles tombent rarement pile au
    // centre d'un texel, sans ça une partie d'entre elles disparaîtrait.
    vec3 col = vec3(0.0);
    for (int i = 0; i < 4; ++i) {
        vec2 offset = vec2(i & 1, i >> 1) * 0.5 + 0.25;
        vec2 st = (floor(gl_FragCoord.xy) + offset) / uFaceSize * 2.0 - 1.0;
        col += galaxyBackground(cubeDirection(uFace, st));
    }
    FragColor = vec4(col * 0.25, 1.0);
}
