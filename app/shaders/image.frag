#version 440

// Draws the artist's own pictures. The texture arrives premultiplied, and so
// does the particle's colour, so one multiply tints the picture and fades it,
// and additive particles (alpha 0) add the picture as light. The picture
// renderer in editor/src/Picture.cpp does the same sums.

layout(location = 0) in vec2 vTexCoord;  // where in the picture
layout(location = 1) in vec4 vColor;
layout(location = 2) in vec4 vShape;     // which shape, a pixel across and down it, dissolve edge glow
layout(location = 3) in vec4 vExtra;     // own position (x, y), how far eaten away, own random number
layout(location = 4) in float vSoft;     // how soft a dissolving edge is, 0 to 1

layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
};

layout(binding = 1) uniform sampler2D picture;

// Dissolve: the particle eaten away in ragged patches as it dies. The same
// sums as dissolveAt in editor/include/vfx/editor/Shapes.h; if you change
// one, change the other (and the other fragment shader).
float dissolveSpeck(vec2 c) {
    vec3 q = fract(vec3(c.x, c.y, c.x) * 0.1031);
    q += dot(q, q.yzx + 33.33);
    return fract((q.x + q.y) * q.z);
}

float dissolveLumps(vec2 q) {
    vec2 i = floor(q);
    vec2 f = q - i;
    f = f * f * (3.0 - 2.0 * f);
    float low = mix(dissolveSpeck(i), dissolveSpeck(i + vec2(1.0, 0.0)), f.x);
    float high = mix(dissolveSpeck(i + vec2(0.0, 1.0)), dissolveSpeck(i + vec2(1.0, 1.0)), f.x);
    return mix(low, high, f.y);
}

// x: how much of this point is left; y: how brightly it glows for being
// near the eaten edge.
vec2 dissolveAt(vec2 own, float amount, float seed, float soft, float edge) {
    if (!(amount > 0.0)) {
        return vec2(1.0, 0.0);
    }
    // Three sizes of lump, each turned against the others so no grid shows.
    vec2 q = own * 1.7 + vec2(seed * 37.0, seed * 91.0);
    vec2 q2 = vec2(0.8 * q.x - 0.6 * q.y, 0.6 * q.x + 0.8 * q.y) * 2.1 + vec2(5.3, 1.7);
    vec2 q3 = vec2(0.28 * q.x + 0.96 * q.y, -0.96 * q.x + 0.28 * q.y) * 4.3 + vec2(9.1, 3.3);
    float n = 0.5 * dissolveLumps(q) + 0.3 * dissolveLumps(q2) + 0.2 * dissolveLumps(q3);
    n += 0.1 * (0.5 - min(length(own), 1.0));
    n = clamp((n - 0.2033) / (0.7556 - 0.2033), 0.0, 1.0);
    n = n * n * (3.0 - 2.0 * n);
    float width = 0.03 + 0.6 * soft;
    float past = n - (amount * (1.0 + width) - width);
    float keep = clamp(past / width, 0.0, 1.0);
    float inner = clamp((past - width) / 0.2, 0.0, 1.0);
    float rim = edge * (1.0 - inner) * (1.0 - inner) * min(amount * 6.0, 1.0);
    return vec2(keep, rim);
}

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
    vec3 rgb = vColor.rgb * t.rgb;
    float alpha = vColor.a * t.a;
    float keep = 1.0;
    if (vExtra.z > 0.0) {
        // Being eaten away as it dies, with a glow along the eaten edge.
        vec2 eaten = dissolveAt(vExtra.xy, vExtra.z, vExtra.w, vSoft, vShape.w);
        float white = max(alpha, max(rgb.r, max(rgb.g, rgb.b)));
        rgb += (vec3(white) - rgb) * eaten.y;
        keep = eaten.x;
    }
    fragColor = vec4(rgb, alpha) * (keep * qt_Opacity);
}
