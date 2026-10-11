// =============================================================================
//  Affichage : agrandit l'image HDR du ray tracer (calculée en basse
//  résolution) à la taille de la fenêtre, puis halo, exposition, tone
//  mapping et gamma. Le filtrage bilinéaire fait l'agrandissement gratuitement.
// =============================================================================

uniform sampler2D uImage;        // image HDR avec ses mipmaps
uniform vec2      uOutputSize;   // taille de la fenêtre en pixels
uniform float     uExposure;     // luminosité globale (1 = normale)
uniform float     uBloom;        // intensité du halo (0 = coupé)
uniform float     uAutoExposure; // 1 = exposition automatique
uniform float     uTopLevel;     // dernier niveau de mipmap (1 pixel = moyenne de l'image)

out vec4 FragColor;

void main()
{
    vec2 uv = gl_FragCoord.xy / uOutputSize;
    vec3 color = textureLod(uImage, uv, 0.0).rgb;

    // Halo (bloom) : la lumière très vive déborde un peu autour d'elle, dans
    // l'œil comme dans un objectif. Les mipmaps sont des versions de plus en
    // plus floues de l'image : on en ajoute un peu, en ne gardant que ce qui
    // dépasse le niveau moyen de la zone (le ciel sombre ne voile pas).
    if (uBloom > 0.0) {
        // Deux niveaux entre deux mipmaps (filtrage trilinéaire) : un halo
        // serré et un large, pour deux lectures de texture seulement.
        vec3 glow = textureLod(uImage, uv, 2.5).rgb * 0.55
                  + textureLod(uImage, uv, 4.5).rgb * 0.45;
        color += uBloom * max(glow - 0.15, 0.0);
    }

    // Exposition automatique : on vise une luminosité moyenne donnée, comme
    // l'œil qui s'adapte (bornée pour ne pas tout blanchir dans le noir).
    float exposure = uExposure;
    if (uAutoExposure > 0.5) {
        vec3 mean = textureLod(uImage, vec2(0.5), uTopLevel).rgb;
        float lum = dot(mean, vec3(0.2126, 0.7152, 0.0722));
        exposure *= clamp(0.12 / max(lum, 1e-4), 0.3, 4.0);
    }
    color *= exposure;

    // Tone mapping (ACES approché) + correction gamma.
    color = (color * (2.51 * color + 0.03)) / (color * (2.43 * color + 0.59) + 0.14);
    color = pow(clamp(color, 0.0, 1.0), vec3(1.0 / 2.2));
    FragColor = vec4(color, 1.0);
}
