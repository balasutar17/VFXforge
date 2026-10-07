#version 440

layout(location = 0) in vec2 vTexCoord;
layout(location = 1) in vec4 vColor;

layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
};

void main() {
    // The built-in soft dot: full strength in the middle, fading smoothly to
    // nothing at the edge of the particle's square.
    vec2 p = vTexCoord * 2.0 - 1.0;
    float falloff = clamp(1.0 - dot(p, p), 0.0, 1.0);
    falloff *= falloff;
    // vColor is premultiplied, so one multiply fades colour and alpha together.
    fragColor = vColor * (falloff * qt_Opacity);
}
