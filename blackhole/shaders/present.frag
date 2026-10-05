// =============================================================================
//  Affichage : agrandit l'image HDR du ray tracer (calculée en basse
//  résolution) à la taille de la fenêtre, puis tone mapping + gamma.
//  Le filtrage bilinéaire de la texture fait l'agrandissement gratuitement.
// =============================================================================

uniform sampler2D uImage;
uniform vec2      uOutputSize;   // taille de la fenêtre en pixels

out vec4 FragColor;

void main()
{
    vec3 color = texture(uImage, gl_FragCoord.xy / uOutputSize).rgb;

    // Tone mapping (ACES approché) + correction gamma.
    color = (color * (2.51 * color + 0.03)) / (color * (2.43 * color + 0.59) + 0.14);
    color = pow(clamp(color, 0.0, 1.0), vec3(1.0 / 2.2));
    FragColor = vec4(color, 1.0);
}
