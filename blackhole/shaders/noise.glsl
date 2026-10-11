// =============================================================================
//  Bruit procédural partagé (fond de galaxie et texture du disque).
//  Ce fichier n'a pas de #version : le C++ l'ajoute et colle les fichiers
//  .glsl avant le shader principal (GLSL 3.30 n'a pas de #include).
// =============================================================================

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

// Pas "noise3" : c'est une fonction intégrée de GLSL (vec3 noise3), et le
// pilote Intel sous Windows refuse de la redéfinir avec un autre type.
float valueNoise(vec3 p)
{
    vec3 i = floor(p);
    vec3 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(mix(hash13(i + vec3(0, 0, 0)), hash13(i + vec3(1, 0, 0)), f.x),
                   mix(hash13(i + vec3(0, 1, 0)), hash13(i + vec3(1, 1, 0)), f.x), f.y),
               mix(mix(hash13(i + vec3(0, 0, 1)), hash13(i + vec3(1, 0, 1)), f.x),
                   mix(hash13(i + vec3(0, 1, 1)), hash13(i + vec3(1, 1, 1)), f.x), f.y), f.z);
}

// octaves : 5 pour le ciel (calculé une seule fois), moins pour le disque.
float fbm(vec3 p, int octaves)
{
    float v = 0.0, a = 0.5;
    for (int i = 0; i < octaves; ++i) {
        v += a * valueNoise(p);
        p = p * 2.03 + 17.1;
        a *= 0.5;
    }
    return v;
}
