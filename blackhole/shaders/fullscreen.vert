// Un seul triangle qui couvre tout l'écran, généré sans aucun vertex buffer.
// Chaque pixel de l'écran exécutera ensuite le fragment shader = 1 rayon.

void main()
{
    vec2 pos = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2); // (0,0) (2,0) (0,2)
    gl_Position = vec4(pos * 2.0 - 1.0, 0.0, 1.0);
}
