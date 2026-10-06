// Paquet de gaz : tache lumineuse gaussienne, ajoutee a l'image (melange
// additif) pour que les paquets voisins forment un jet continu.

in vec3 vColor;

out vec4 FragColor;

void main()
{
    vec2 p = gl_PointCoord * 2.0 - 1.0;
    float d2 = dot(p, p);
    if (d2 > 1.0) discard;
    FragColor = vec4(vColor * exp(-d2 * 3.5), 1.0);
}
