#version 440

// Draws the built-in particle shapes.
//
// These formulas are written twice: here, and in editor/src/Shapes.cpp for
// the picture renderer and the tests. If you change one, change the other.

layout(location = 0) in vec2 vTexCoord;
layout(location = 1) in vec4 vColor;   // premultiplied; alpha 0 means "add light"
layout(location = 2) in vec3 vShape;

layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
};

float sq(float v) { return v * v; }

// Coverage of a hard edge, from the signed distance to it and a pixel's size.
float edge(float distance, float pixel) { return clamp(0.5 - distance / pixel, 0.0, 1.0); }

float blob(vec2 p, vec2 c, float radius) {
    vec2 q = p - c;
    return sq(clamp(1.0 - dot(q, q) / (radius * radius), 0.0, 1.0));
}

// Signed distance to a five-pointed star, one point up (after Inigo Quilez).
float star5(vec2 p, float radius, float inner) {
    const vec2 k1 = vec2(0.809016994, -0.587785252);
    const vec2 k2 = vec2(-0.809016994, -0.587785252);
    p.x = abs(p.x);
    p -= 2.0 * max(dot(k1, p), 0.0) * k1;
    p -= 2.0 * max(dot(k2, p), 0.0) * k2;
    p.x = abs(p.x);
    p.y -= radius;
    vec2 ba = vec2(inner * -k1.y, inner * k1.x - 1.0);
    float h = clamp(dot(p, ba) / dot(ba, ba), 0.0, radius);
    float side = p.y * ba.x - p.x * ba.y;
    return length(p - ba * h) * (side > 0.0 ? 1.0 : -1.0);
}

// Signed distance to a heart with its tip at the origin (after Inigo Quilez).
float heart(vec2 p) {
    p.x = abs(p.x);
    if (p.x + p.y > 1.0) {
        return length(p - vec2(0.25, 0.75)) - 0.353553391;
    }
    float m = 0.5 * max(p.x + p.y, 0.0);
    vec2 a = p - vec2(0.0, 1.0);
    vec2 b = p - vec2(m, m);
    return sqrt(min(dot(a, a), dot(b, b))) * (p.x > p.y ? 1.0 : -1.0);
}

// The union of round lobes that makes a toon cloud.
float cloud(vec2 p) {
    float d = length(p - vec2(0.0, -0.05)) - 0.56;
    d = min(d, length(p - vec2(-0.44, -0.12)) - 0.42);
    d = min(d, length(p - vec2(0.46, -0.16)) - 0.40);
    d = min(d, length(p - vec2(-0.20, 0.36)) - 0.44);
    d = min(d, length(p - vec2(0.30, 0.30)) - 0.42);
    return d;
}

// How far out the edge of a spiky burst is in a given direction.
float spikes(vec2 p, float reach) {
    float turn = atan(p.y, p.x + 0.000001) / 6.2831853 + 0.5;
    float along = turn * 10.0;
    float cell = floor(along);
    float across = abs(along - cell - 0.5) * 2.0;
    float longer = mod(cell, 2.0) < 0.5 ? 1.0 : 0.72;
    return reach * (0.38 + (longer - 0.38) * pow(1.0 - across, 1.5));
}

// A hard-edged flame leaning to one side: negative inside, positive outside.
float blaze(vec2 p, float bottom, float top, float girth) {
    float t = (p.y - bottom) / (top - bottom);
    if (t <= 0.0 || t >= 1.0) {
        return 1.0;
    }
    float base = (0.3 - min(t, 0.3)) / 0.3;
    float taper = 1.0 - max(t - 0.3, 0.0) / 0.7;
    float width = girth * sqrt(clamp(1.0 - base * base, 0.0, 1.0)) * pow(taper, 0.9);
    return (abs(p.x - 0.16 * t * t) - width) * 0.8;
}

void main() {
    // Across the particle from -1 to 1, with y pointing up.
    vec2 p = vec2(vTexCoord.x * 2.0 - 1.0, 1.0 - vTexCoord.y * 2.0);
    int shape = int(vShape.x + 0.5);
    float pixel = max(max(vShape.y, vShape.z), 0.0001);
    float r2 = dot(p, p);
    float r = sqrt(r2);

    float cover = 0.0;
    float tone = 0.0;

    if (shape == 0) {            // soft
        cover = sq(clamp(1.0 - r2, 0.0, 1.0));
    } else if (shape == 1) {     // disc
        cover = edge(r - 0.9, pixel);
    } else if (shape == 2) {     // ring
        cover = edge(abs(r - 0.85) - 0.055, pixel);
    } else if (shape == 3) {     // bubble
        float inside = edge(r - 0.86, pixel);
        float rim = edge(abs(r - 0.86) - 0.05, pixel);
        float fill = inside * (0.10 + 0.22 * r2);
        float shine = inside * (0.85 * blob(p, vec2(-0.36, 0.40), 0.24) +
                                0.35 * blob(p, vec2(0.42, -0.46), 0.12));
        cover = clamp(max(rim, fill + shine), 0.0, 1.0);
    } else if (shape == 4) {     // sparkle
        float rays = clamp(1.0 - (sqrt(abs(p.x)) + sqrt(abs(p.y))), 0.0, 1.0);
        float core = sq(clamp(1.0 - r2 / (0.42 * 0.42), 0.0, 1.0));
        cover = clamp(2.4 * pow(rays, 1.5) + 0.7 * core, 0.0, 1.0);
    } else if (shape == 5) {     // star
        cover = edge(star5(p, 0.82, 0.45) - 0.10, pixel);
        tone = edge(star5(p + vec2(0.0, 0.02), 0.46, 0.45) - 0.06, pixel);
    } else if (shape == 6) {     // smoke
        float clear = 1.0;
        clear *= 1.0 - 0.6 * blob(p, vec2(0.0, 0.0), 0.80);
        clear *= 1.0 - 0.6 * blob(p, vec2(-0.30, 0.20), 0.56);
        clear *= 1.0 - 0.6 * blob(p, vec2(0.32, 0.14), 0.54);
        clear *= 1.0 - 0.6 * blob(p, vec2(-0.14, -0.30), 0.52);
        clear *= 1.0 - 0.6 * blob(p, vec2(0.26, -0.30), 0.48);
        clear *= 1.0 - 0.6 * blob(p, vec2(0.04, 0.38), 0.50);
        cover = 1.0 - clear;
    } else if (shape == 7) {     // square
        vec2 q = abs(p) - vec2(0.66);
        cover = edge(length(max(q, vec2(0.0))) + min(max(q.x, q.y), 0.0) - 0.12, pixel);
    } else if (shape == 8) {     // diamond
        cover = edge((abs(p.x) / 0.66 + abs(p.y) / 0.92 - 1.0) * 0.536, pixel);
        tone = clamp((p.y - p.x) * 4.0, 0.0, 1.0) * 0.8 *
               edge((abs(p.x) / 0.66 + abs(p.y) / 0.92 - 0.72) * 0.536, pixel);
    } else if (shape == 9) {     // heart
        cover = edge(heart(vec2(p.x * 0.64, p.y * 0.64 + 0.55)) / 0.64, pixel);
        tone = edge(sqrt(sq((p.x + 0.42) / 1.5) + sq(p.y - 0.42)) - 0.13, pixel);
    } else if (shape == 10) {    // streak
        float t = clamp((p.x + 1.0) * 0.5, 0.0, 1.0);
        float nose = max(t - 0.75, 0.0) / 0.25;
        float width = 0.85 * pow(t, 0.7) * sqrt(clamp(1.0 - nose * nose, 0.0, 1.0));
        if (width > 0.0001) {
            cover = sq(clamp(1.0 - sq(p.y / width), 0.0, 1.0)) * pow(t, 1.2);
        }
    } else if (shape == 11) {    // flame
        float t = clamp((p.y + 1.0) * 0.5, 0.0, 1.0);
        float base = (0.3 - min(t, 0.3)) / 0.3;
        float taper = 1.0 - max(t - 0.3, 0.0) / 0.7;
        float width = 0.8 * sqrt(clamp(1.0 - base * base, 0.0, 1.0)) * pow(taper, 0.8);
        if (width > 0.0001) {
            cover = sq(clamp(1.0 - sq(p.x / width), 0.0, 1.0)) * (1.0 - 0.55 * t);
        }
    } else if (shape == 12) {    // puff
        cover = edge(cloud(p * 1.08) / 1.08, pixel);
        float lit = edge(cloud((p + vec2(0.13, -0.15)) * 1.5) / 1.5, pixel);
        float shade = 1.0 - edge(cloud((p + vec2(0.07, -0.08)) * 1.14) / 1.14, pixel);
        tone = lit - 0.6 * shade;
    } else if (shape == 13) {    // burst
        cover = edge(r - spikes(p, 0.95), pixel);
        tone = edge(r - spikes(p, 0.52), pixel);
    } else if (shape == 14) {    // crescent
        float bite = length(p + vec2(0.42, 0.0)) - 0.98;
        cover = edge(max(r - 0.92, -bite), pixel);
        tone = edge(max(r - 0.92, -(length(p + vec2(0.16, 0.0)) - 0.98)), pixel);
    } else if (shape == 15) {    // orb
        cover = edge(r - 0.9, pixel);
        float shine = edge(length(p - vec2(-0.34, 0.36)) - 0.24, pixel);
        float shade = 1.0 - edge(length(p - vec2(-0.16, 0.16)) - 0.84, pixel);
        tone = shine - 0.7 * shade;
    } else if (shape == 16) {    // glint
        float s = sqrt(abs(p.x)) + sqrt(abs(p.y));
        cover = edge((s - 0.95) * (0.5 * r + 0.12), pixel);
        tone = edge((s - 0.6) * (0.5 * r + 0.12), pixel);
    } else {                     // blaze
        cover = edge(blaze(p, -0.92, 0.95, 0.62), pixel);
        tone = edge(blaze(p + vec2(0.02, 0.0), -0.74, 0.42, 0.36), pixel);
    }

    // Toon shading: a highlight moves toward white at the particle's own
    // brightness; a shadow dims.
    vec3 rgb = vColor.rgb;
    if (tone > 0.0) {
        float most = max(rgb.r, max(rgb.g, rgb.b));
        rgb = (rgb + (vec3(most) - rgb) * (0.5 * tone)) * (1.0 + 0.3 * tone);
    } else if (tone < 0.0) {
        rgb *= 1.0 + 0.5 * tone;
    }

    // vColor is premultiplied, so one multiply fades colour and alpha together.
    vec4 result = vec4(rgb, vColor.a) * (cover * qt_Opacity);

    // A nudge of less than one colour step, different at every pixel, hides
    // the bands that a faint, wide glow would otherwise show.
    vec2 where = gl_FragCoord.xy + vTexCoord * 61.0;
    float noise = fract(52.9829189 * fract(dot(where, vec2(0.06711056, 0.00583715)))) - 0.5;
    result.rgb = max(result.rgb + vec3(noise / 255.0) * clamp(cover * 40.0, 0.0, 1.0), vec3(0.0));

    fragColor = result;
}
