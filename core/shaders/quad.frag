#version 450

layout(location = 0) in  vec2 fragTexCoord;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D texSampler;

layout(push_constant) uniform PushConstants {
    vec2 scale;   // viewport-relative scale for aspect ratio letterboxing
    vec2 offset;  // centering offset in UV space
} pc;

void main()
{
    vec2 uv = (fragTexCoord - pc.offset) / pc.scale;

    // Black bars outside the video area
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
        outColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    outColor = texture(texSampler, uv);
}
