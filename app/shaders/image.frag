#version 440

// Draws the artist's own pictures. The texture arrives premultiplied, and so
// does the particle's colour, so one multiply tints the picture and fades it,
// and additive particles (alpha 0) add the picture as light. The picture
// renderer in editor/src/Picture.cpp does the same sums.

layout(location = 0) in vec2 vTexCoord;  // where in the picture
layout(location = 1) in vec4 vColor;
layout(location = 2) in vec3 vShape;

layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
};

layout(binding = 1) uniform sampler2D picture;

void main() {
    if (vShape.x < -1.5) {
        // A trail ribbon behind a picture particle: drawn as in sprite.frag.
        float c = abs(vTexCoord.y * 2.0 - 1.0);
        float cover = clamp((1.0 - c) * 2.2, 0.0, 1.0);
        float head = 1.0 - vTexCoord.x;
        float shine = max(1.0 - c * 2.5, 0.0) * head * head * head;
        vec3 rgb = vColor.rgb;
        float white = max(vColor.a, max(rgb.r, max(rgb.g, rgb.b)));
        rgb += (vec3(white) - rgb) * shine;
        fragColor = vec4(rgb, vColor.a) * (cover * qt_Opacity);
        return;
    }
    vec4 t = texture(picture, vTexCoord);
    fragColor = vec4(vColor.rgb * t.rgb, vColor.a * t.a) * qt_Opacity;
}
