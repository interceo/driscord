#version 450

layout(location = 0) out vec2 fragTexCoord;

// Fullscreen triangle — 3 vertices, no vertex buffer.
// Covers [-1,1] clip space with a single oversized triangle,
// hardware clips to the viewport rectangle.
void main()
{
    fragTexCoord = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position  = vec4(fragTexCoord * 2.0 - 1.0, 0.0, 1.0);
    fragTexCoord.y = 1.0 - fragTexCoord.y; // flip Y for top-left origin
}
