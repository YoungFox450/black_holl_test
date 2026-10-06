// =============================================================================
//  Fond de ciel à partir d'une image (src/sky_image.cpp) : pour chaque texel
//  de la cubemap, on prend la direction, on la tourne dans le repère de
//  l'image (galactique ou équatorial), on en déduit longitude et latitude et
//  on lit la carte équirectangulaire.
// =============================================================================

uniform int       uFace;       // face de la cubemap en cours de calcul (0..5)
uniform float     uFaceSize;   // taille d'une face en texels
uniform sampler2D uImage;      // carte équirectangulaire, ligne 0 = nord
uniform mat3      uToImage;    // direction de la scène -> repère de l'image
uniform float     uMirror;     // 1 : longitude croissante vers la droite

out vec4 FragColor;

const float PI = 3.14159265359;

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

vec3 skyColor(vec3 dir)
{
    vec3 v = normalize(uToImage * normalize(dir));
    float lon = atan(v.y, v.x);                      // -π..π, 0 = centre de l'image
    float lat = asin(clamp(v.z, -1.0, 1.0));
    float u = 0.5 + mix(-1.0, 1.0, uMirror) * lon / (2.0 * PI);
    // textureLod 0 : la longitude saute de 1 à 0 sur la couture, une
    // dérivée automatique y choisirait le plus petit niveau de mipmap.
    return textureLod(uImage, vec2(fract(u), 0.5 - lat / PI), 0.0).rgb;
}

void main()
{
    // 4 échantillons par texel, comme sky.frag.
    vec3 col = vec3(0.0);
    for (int i = 0; i < 4; ++i) {
        vec2 offset = vec2(i & 1, i >> 1) * 0.5 + 0.25;
        vec2 st = (floor(gl_FragCoord.xy) + offset) / uFaceSize * 2.0 - 1.0;
        col += skyColor(cubeDirection(uFace, st));
    }
    FragColor = vec4(col * 0.25, 1.0);
}
