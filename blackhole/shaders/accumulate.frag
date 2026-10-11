// Copie l'image du ray tracer dans l'image accumulée. En pause, le C++ active
// le mélange (poids 1/n) : la moyenne de n images décalées d'une fraction de
// pixel donne une image lissée, sans escaliers ni scintillement.

uniform sampler2D uImage;
uniform vec2      uSize;

out vec4 FragColor;

void main()
{
    FragColor = vec4(textureLod(uImage, gl_FragCoord.xy / uSize, 0.0).rgb, 1.0);
}
