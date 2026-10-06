// Asteroide : petite sphere de roche eclairee par le corps central, qui
// rougeoie quand la chaleur de l'etoile la porte a plus de ~800 K.

in vec3  vLightDir;
in float vHeat;
in float vDim;
in float vFall;

out vec4 FragColor;

uniform vec3 uLightColor;   // couleur et intensite de la lumiere du corps central
uniform float uAmbient;     // lumiere diffuse (un disque d'accretion eclaire de partout)

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

void main()
{
    vec2 p = gl_PointCoord * 2.0 - 1.0;
    p.y = -p.y;
    float d2 = dot(p, p);
    if (d2 > 1.0) discard;
    vec3 n = vec3(p, sqrt(1.0 - d2));

    // Roche grise un peu brune, eclairage de Lambert + un peu d'ambiance.
    float lambert = max(dot(n, normalize(vLightDir)), 0.0);
    vec3 rock = vec3(0.42, 0.38, 0.33);
    vec3 color = rock * (uLightColor * (lambert + uAmbient) * vFall + vec3(0.04));

    // Incandescence.
    float glow = clamp((vHeat - 800.0) / 1500.0, 0.0, 3.0);
    color += blackbody(vHeat) * glow * 1.5;

    float edge = 1.0 - smoothstep(0.75, 1.0, d2);
    FragColor = vec4(color, edge * vDim);
}
