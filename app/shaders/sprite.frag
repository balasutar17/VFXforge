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

// One thin ray along an axis, narrowing and fading to its tip.
float ray(float along, float across, float len, float width) {
    if (along >= len) {
        return 0.0;
    }
    float left = 1.0 - along / len;
    return clamp(1.0 - across / (width * left + 0.0001), 0.0, 1.0) * sqrt(left);
}

float segmentDistance(vec2 p, vec2 a, vec2 b) {
    vec2 e = b - a;
    float h = clamp(dot(p - a, e) / dot(e, e), 0.0, 1.0);
    return length(p - a - e * h);
}

// Distance to the jagged line of a lightning bolt along x, with one fork.
float boltDistance(vec2 p) {
    vec2 c[16] = vec2[16](vec2(-0.95, 0.0), vec2(-0.8233, 0.08), vec2(-0.6967, -0.05), vec2(-0.57, 0.12), vec2(-0.4433, 0.02), vec2(-0.3167, -0.1), vec2(-0.19, 0.06), vec2(-0.0633, -0.02), vec2(0.0633, 0.14), vec2(0.19, -0.06), vec2(0.3167, 0.04), vec2(0.4433, -0.12), vec2(0.57, 0.03), vec2(0.6967, 0.09), vec2(0.8233, -0.04), vec2(0.95, 0.0));
    float best = 1e9;
    for (int i = 0; i < 15; ++i) {
        best = min(best, segmentDistance(p, c[i], c[i + 1]));
    }
    best = min(best, segmentDistance(p, c[6], vec2(-0.06, -0.30)));
    best = min(best, segmentDistance(p, vec2(-0.06, -0.30), vec2(0.10, -0.36)));
    best = min(best, segmentDistance(p, vec2(0.10, -0.36), vec2(0.24, -0.52)));
    return best;
}

// The hard white part of a star flash: an uneven core and nine spikes.
float starflashDistance(vec2 p) {
    float angle[9] = float[9](0.20, 0.90, 1.50, 2.30, 2.90, 3.60, 4.30, 5.00, 5.70);
    float len[9] = float[9](0.95, 0.55, 0.80, 0.45, 0.90, 0.60, 0.85, 0.50, 0.70);
    float wid[9] = float[9](0.08, 0.06, 0.07, 0.05, 0.08, 0.06, 0.07, 0.05, 0.06);
    float d = length(p) - (0.20 + 0.04 * sin(5.0 * atan(p.y, p.x)));
    for (int k = 0; k < 9; ++k) {
        float c = cos(angle[k]);
        float s = sin(angle[k]);
        float along = p.x * c + p.y * s;
        float across = abs(-p.x * s + p.y * c);
        if (along > 0.0 && along < len[k]) {
            d = min(d, (across - wid[k] * (1.0 - along / len[k])) * 0.9);
        }
    }
    return d;
}

// A rounded square: the outline of a gumdrop or jelly candy.
float squircle(vec2 p, float radius) {
    vec2 a = pow(abs(p) + vec2(1e-6), vec2(2.6));
    return pow(a.x + a.y, 1.0 / 2.6) - radius;
}

// A convex four-cornered fragment, corners counter-clockwise.
float shardShape(vec2 p) {
    vec2 c[4] = vec2[4](vec2(-0.85, -0.35), vec2(0.10, -0.90), vec2(0.90, 0.15), vec2(-0.20, 0.85));
    float d = -1e9;
    for (int i = 0; i < 4; ++i) {
        vec2 a = c[i];
        vec2 e = c[(i + 1) % 4] - a;
        d = max(d, ((p.x - a.x) * e.y - (p.y - a.y) * e.x) / length(e));
    }
    return d;
}

// A flying drop with its round head at +x and a tail to -x.
float dropShape(vec2 p) {
    float head = length(p - vec2(0.35, 0.0)) - 0.55;
    if (p.x >= 0.35 || p.x <= -0.95) {
        return head;
    }
    float width = 0.55 * pow((p.x + 0.95) / 1.3, 0.9);
    return min(head, (abs(p.y) - width) * 0.85);
}

// A splat of jelly: a round body, lobes around it and a few flung drops.
float splatShape(vec2 p) {
    float d = length(p) - 0.52;
    d = min(d, length(p - vec2(0.56, 0.18)) - 0.22);
    d = min(d, length(p - vec2(0.10, 0.60)) - 0.20);
    d = min(d, length(p - vec2(-0.50, 0.34)) - 0.19);
    d = min(d, length(p - vec2(-0.58, -0.22)) - 0.21);
    d = min(d, length(p - vec2(-0.06, -0.60)) - 0.18);
    d = min(d, length(p - vec2(0.48, -0.40)) - 0.20);
    d = min(d, length(p - vec2(0.86, 0.52)) - 0.09);
    d = min(d, length(p - vec2(-0.84, 0.66)) - 0.07);
    d = min(d, length(p - vec2(0.30, -0.90)) - 0.08);
    return d;
}

void main() {
    // Across the particle from -1 to 1, with y pointing up.
    vec2 p = vec2(vTexCoord.x * 2.0 - 1.0, 1.0 - vTexCoord.y * 2.0);
    int shape = int(floor(vShape.x + 0.5));
    float pixel = max(max(vShape.y, vShape.z), 0.0001);
    float r2 = dot(p, p);
    float r = sqrt(r2);

    float cover = 0.0;
    float tone = 0.0;
    float shine = 0.0;

    if (shape == -2) {           // a trail ribbon: u along, v across
        float c = abs(vTexCoord.y * 2.0 - 1.0);
        cover = clamp((1.0 - c) * 2.2, 0.0, 1.0);
        float head = 1.0 - vTexCoord.x;
        shine = max(1.0 - c * 2.5, 0.0) * head * head * head;
    } else if (shape == 0) {     // soft
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
        shine = 0.8 * tone;
    } else if (shape == 10) {    // streak
        float t = clamp((p.x + 1.0) * 0.5, 0.0, 1.0);
        float nose = max(t - 0.75, 0.0) / 0.25;
        float width = 0.85 * pow(t, 0.7) * sqrt(clamp(1.0 - nose * nose, 0.0, 1.0));
        if (width > 0.0001) {
            cover = sq(clamp(1.0 - sq(p.y / width), 0.0, 1.0)) * pow(t, 1.2);
        }
        shine = sq(clamp((t - 0.6) / 0.35, 0.0, 1.0)) * cover;
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
        float spot = edge(length(p - vec2(-0.34, 0.36)) - 0.24, pixel);
        float shade = 1.0 - edge(length(p - vec2(-0.16, 0.16)) - 0.84, pixel);
        tone = spot - 0.7 * shade;
        shine = 0.8 * spot;
    } else if (shape == 16) {    // glint
        float s = sqrt(abs(p.x)) + sqrt(abs(p.y));
        cover = edge((s - 0.95) * (0.5 * r + 0.12), pixel);
        tone = edge((s - 0.6) * (0.5 * r + 0.12), pixel);
    } else if (shape == 17) {    // blaze
        cover = edge(blaze(p, -0.92, 0.95, 0.62), pixel);
        tone = edge(blaze(p + vec2(0.02, 0.0), -0.74, 0.42, 0.36), pixel);
    } else if (shape == 18) {    // candy
        cover = edge(squircle(p, 0.86), pixel);
        float sx = (p.x + 0.30) * 0.866 + (p.y - 0.40) * 0.5;
        float sy = -(p.x + 0.30) * 0.5 + (p.y - 0.40) * 0.866;
        float longShine = edge((sqrt(sq(sx / 0.30) + sq(sy / 0.13)) - 1.0) * 0.13, pixel);
        float spot = edge(length(p - vec2(0.14, 0.58)) - 0.07, pixel);
        float shade = 1.0 - edge(squircle(p + vec2(0.10, -0.12), 0.80), pixel * 2.0);
        tone = 0.35 * longShine - 0.55 * shade;
        shine = max(0.9 * longShine, spot);
    } else if (shape == 19) {    // shard
        cover = edge(shardShape(p * 1.05) / 1.05, pixel);
        float side = (0.3 * (p.y - 0.85) + 1.75 * (p.x + 0.2)) / 1.7755;
        float lit = edge(side, pixel);
        tone = 0.6 * lit - 0.4 * (1.0 - lit);
        shine = edge(length(p - vec2(-0.16, 0.56)) - 0.09, pixel);
    } else if (shape == 20) {    // drop
        cover = edge(dropShape(p), pixel);
        float spot = edge(length(p - vec2(0.48, 0.20)) - 0.13, pixel);
        float shade = 1.0 - edge(dropShape(p + vec2(0.03, 0.12)) + 0.08, pixel * 2.0);
        tone = 0.3 * spot - 0.45 * shade;
        shine = spot;
    } else if (shape == 21) {    // splat
        cover = edge(splatShape(p), pixel);
        float spot = edge(sqrt(sq((p.x + 0.18) / 1.6) + sq(p.y - 0.22)) - 0.11, pixel);
        float rim = 1.0 - edge(r - 0.40, pixel * 3.0);
        tone = 0.3 * spot - 0.35 * rim;
        shine = spot;
    } else if (shape == 22) {    // shockwave
        float ring = edge(abs(r - 0.77) - 0.15, pixel);
        float haze = 0.15 * pow(clamp(r / 0.62, 0.0, 1.0), 3.0) * edge(r - 0.62, pixel);
        cover = clamp(ring + haze, 0.0, 1.0);
        float inner = edge(abs(r - 0.68) - 0.05, pixel);
        tone = ring * (inner - 0.35 * edge(0.86 - r, pixel));
        shine = 0.6 * ring * inner;
    } else if (shape == 23) {    // twinkle
        float d = 0.70710678;
        vec2 q = abs(p);
        vec2 w = abs(vec2(p.x + p.y, p.x - p.y) * d);
        float rays = max(max(ray(q.x, q.y, 0.95, 0.07), ray(q.y, q.x, 0.95, 0.07)),
                         max(ray(w.x, w.y, 0.45, 0.05), ray(w.y, w.x, 0.45, 0.05)));
        float core = sq(clamp(1.0 - r2 / (0.30 * 0.30), 0.0, 1.0));
        cover = clamp(max(rays, core) + 0.25 * sq(clamp(1.0 - r2 / (0.55 * 0.55), 0.0, 1.0)), 0.0, 1.0);
        shine = core;
    } else if (shape == 24) {    // flare
        float angle = atan(p.y, p.x);
        if (angle < 0.0) {
            angle += 6.2831853;
        }
        int k = int(floor(angle * 12.0 / 6.2831853 + 0.5)) % 12;
        float lengths[12] = float[12](0.95, 0.55, 0.80, 0.50, 0.92, 0.62, 0.86, 0.46, 0.95, 0.58, 0.76, 0.52);
        float len = lengths[k];
        float off = abs(angle - float(k) * 6.2831853 / 12.0);
        float across = min(off, 6.2831853 - off) * r;
        float rays = r < len ? clamp(1.0 - across / (0.10 * (1.0 - r / len) + 0.0001), 0.0, 1.0) *
                                   pow(1.0 - r / len, 0.8)
                             : 0.0;
        float core = sq(clamp(1.0 - r2 / (0.22 * 0.22), 0.0, 1.0));
        float halo = 0.5 * sq(clamp(1.0 - r2 / (0.55 * 0.55), 0.0, 1.0));
        cover = clamp(max(rays, halo + core), 0.0, 1.0);
        shine = core;
    } else if (shape == 25) {    // rays
        float angle = atan(p.y, p.x);
        if (angle < 0.0) {
            angle += 6.2831853;
        }
        float a = angle * 10.0 / 6.2831853;
        float f = abs(fract(a) - 0.5) * 2.0;
        float beam = sq(clamp(1.0 - f * 1.15, 0.0, 1.0));
        cover = r < 1.0 ? beam * pow(1.0 - r, 0.9) * clamp(r / 0.12, 0.0, 1.0) +
                              0.35 * sq(clamp(1.0 - r2 / (0.3 * 0.3), 0.0, 1.0))
                        : 0.0;
        cover = clamp(cover, 0.0, 1.0);
    } else if (shape == 26) {    // swirl
        cover = edge(r - 0.88, pixel);
        float angle = atan(p.y, p.x);
        float s = angle / 6.2831853 * 3.0 + r * 1.7;
        float stripe = edge((abs(fract(s) - 0.5) - 0.25) * (r * 2.0 + 0.1), pixel);
        float spot = edge(length(p - vec2(-0.36, 0.40)) - 0.13, pixel);
        float shade = 1.0 - edge(length(p - vec2(-0.12, 0.12)) - 0.82, pixel * 2.0);
        tone = 0.8 * stripe - 0.45 * shade;
        shine = max(0.85 * stripe * (1.0 - 0.5 * shade), spot);
    } else if (shape == 28) {    // bolt
        float thin = 1.0 - pow(abs(p.x), 6.0);
        float d = boltDistance(p);
        float core = edge(d - 0.03 * thin, pixel);
        float glow = 0.75 * sq(clamp(1.0 - d / 0.2, 0.0, 1.0)) * thin;
        cover = clamp(max(core, glow), 0.0, 1.0);
        shine = core;
    } else if (shape == 29) {    // starflash
        float hard = edge(starflashDistance(p), pixel);
        float halo = 0.5 * sq(clamp(1.0 - r2 / (0.85 * 0.85), 0.0, 1.0));
        cover = max(hard, halo);
        shine = hard;
    } else if (shape == 30) {    // sliver
        cover = edge((abs(p.x) / 0.88 + abs(p.y) / 0.09 - 1.0) * 0.09, pixel);
        shine = 0.8 * edge(abs(p.y) - 0.02, pixel) * clamp(1.0 - abs(p.x), 0.0, 1.0);
    } else if (shape == 31) {    // shardring
        float angle = atan(p.y, p.x);
        if (angle < 0.0) {
            angle += 6.2831853;
        }
        float a = angle * 6.0 / 6.2831853;
        float seg = fract(a);
        float inPiece = clamp((seg - 0.08) / 0.84, 0.0, 1.0);
        float taper = (seg > 0.08 && seg < 0.92) ? sin(3.14159265 * inPiece) : 0.0;
        float jag = 0.03 * fract(a * 3.0);
        float thick = (0.08 + jag) * taper;
        cover = edge(max(r - 0.88, (0.88 - thick) - r), pixel);
        shine = 0.5 * cover * edge(r - 0.86 + thick * 0.5, pixel);
        tone = 0.3 * cover;
    } else if (shape == 27) {    // bean
        float by = p.y + 0.28 * p.x * p.x - 0.06;
        float body = length(vec2(max(abs(p.x) - 0.48, 0.0), by)) - 0.38;
        cover = edge(body, pixel);
        float spot = edge(length(vec2(max(abs(p.x + 0.06) - 0.28, 0.0), (by - 0.17) / 0.45)) - 0.05, pixel);
        float shade = 1.0 - edge(length(vec2(max(abs(p.x + 0.04) - 0.46, 0.0), by - 0.08)) - 0.32, pixel * 2.0);
        tone = 0.3 * spot - 0.5 * shade;
        shine = spot;
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

    // Gloss: toward white at the particle's own brightness.
    if (shine > 0.0) {
        float white = max(vColor.a, max(rgb.r, max(rgb.g, rgb.b)));
        rgb += (vec3(white) - rgb) * shine;
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
