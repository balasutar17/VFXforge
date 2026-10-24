// VFX Forge shader families.
//
// This one source is used twice, so the preview and Unity cannot disagree:
//  - the editor compiles it as C++ (with ShaderMath.h standing in for HLSL's vector types and
//    functions) to draw the live preview, and
//  - the Unity export copies the same text into the generated .shader file, where it is HLSL.
// So it is written in the small part of the language both accept: float, float2, float3,
// float4, plain functions, structs, for/if, no swizzles (write .x .y .z), no `out` parameters.
//
// The index constants (CM_speed, HG_color ...) come from ShaderParams.def: as C++ enums in the
// editor and as #defines at the top of the generated shader.
// VFX_OCTAVES is how much detail the noise has, VFX_QUALITY the profile (0 mobile .. 3 cinematic).

#define PF(i) P.v[i].x
#define PC(i) float3(P.v[i].x, P.v[i].y, P.v[i].z)

struct ShadeIn
{
    float3 pos;      // object position, about -1..1 across the object
    float3 nrm;      // surface normal
    float3 view;     // direction to the viewer
    float2 uv;       // 0..1 across a quad or sprite
    float t;         // seconds, already multiplied by the animation speed
    float life;      // 0..1 through a particle's life
    float3 hitPos;   // where the shield was hit
    float hitAge;    // seconds since the hit (large when there was none)
    float spriteA;   // UI: the sprite's alpha
    float spriteOut; // UI: the strongest alpha within the outline width
    float spriteGlow; // UI: the average alpha round the shape, for the glow
};

struct Params
{
    float4 v[48];
};

struct ShadeOut
{
    float3 rgb;
    float a;
};

ShadeOut makeOut(float3 c, float a)
{
    ShadeOut o;
    o.rgb = c;
    o.a = a;
    return o;
}

// ---------------------------------------------------------------- noise
float hash11(float n) { return frac(sin(n * 12.9898) * 43758.5453); }
float hash21(float2 p) { return frac(sin(dot(p, float2(127.1, 311.7))) * 43758.5453); }
float hash31(float3 p) { return frac(sin(dot(p, float3(127.1, 311.7, 74.7))) * 43758.5453); }
float3 hash33(float3 p)
{
    return float3(hash31(p), hash31(p + float3(19.3, 7.1, 3.7)), hash31(p + float3(5.2, 41.7, 13.9)));
}

float vnoise3(float3 p)
{
    float3 i = floor(p);
    float3 f = frac(p);
    f = f * f * (float3(3.0, 3.0, 3.0) - 2.0 * f);
    float a = hash31(i);
    float b = hash31(i + float3(1.0, 0.0, 0.0));
    float c = hash31(i + float3(0.0, 1.0, 0.0));
    float d = hash31(i + float3(1.0, 1.0, 0.0));
    float e = hash31(i + float3(0.0, 0.0, 1.0));
    float f1 = hash31(i + float3(1.0, 0.0, 1.0));
    float g = hash31(i + float3(0.0, 1.0, 1.0));
    float h = hash31(i + float3(1.0, 1.0, 1.0));
    return lerp(lerp(lerp(a, b, f.x), lerp(c, d, f.x), f.y), lerp(lerp(e, f1, f.x), lerp(g, h, f.x), f.y), f.z);
}

float fbm3(float3 p)
{
    float s = 0.0;
    float a = 0.5;
    float norm = 0.0;
    for (int i = 0; i < 4; i++)
    {
        if (i >= VFX_OCTAVES) break;
        s += a * vnoise3(p);
        norm += a;
        p = p * 2.03 + float3(1.7, 9.2, 3.1);
        a *= 0.5;
    }
    return s / norm;
}

// Nearest and second-nearest cell distance, for cracks and cells.
float2 voro3(float3 p)
{
    float3 ip = floor(p);
    float3 fp = frac(p);
    float d1 = 8.0;
    float d2 = 8.0;
    for (int k = -1; k <= 1; k++)
    {
        for (int j = -1; j <= 1; j++)
        {
            for (int i = -1; i <= 1; i++)
            {
                float3 g = float3(float(i), float(j), float(k));
                float3 r = g + hash33(ip + g) - fp;
                float d = dot(r, r);
                if (d < d1) { d2 = d1; d1 = d; }
                else if (d < d2) { d2 = d; }
            }
        }
    }
    return float2(sqrt(d1), sqrt(d2));
}

float3 rotY(float3 p, float a)
{
    float c = cos(a);
    float s = sin(a);
    return float3(c * p.x + s * p.z, p.y, -s * p.x + c * p.z);
}

float2 mod2(float2 x, float2 y) { return x - y * floor(x / y); }

// Distance to the middle of a hexagon cell: 0 at the centre, 0.5 at the border.
float hexEdge(float2 p)
{
    float2 r = float2(1.0, 1.7320508);
    float2 h = r * 0.5;
    float2 a = mod2(p, r) - h;
    float2 b = mod2(p - h, r) - h;
    float2 gv = a;
    if (dot(b, b) < dot(a, a)) gv = b;
    float2 q = abs(gv);
    return max(dot(q, float2(0.5, 0.8660254)), q.x);
}

// ---------------------------------------------------------------- shared animation
float pulseOf(ShadeIn I, Params P) { return 1.0 + PF(CM_pulseAmp) * sin(I.t * 6.2831853 * PF(CM_pulseFreq)); }

float flickerOf(ShadeIn I, Params P)
{
    float n = hash11(floor(I.t * 18.0) + 3.0);
    float m = vnoise3(float3(I.t * 7.0, 1.0, 2.0));
    return 1.0 - PF(CM_flicker) * (0.6 * n * n + 0.4 * m);
}

// x: how much shows, y: the glowing edge of the dissolve.
float2 revealOf(ShadeIn I, Params P)
{
    float r = PF(CM_reveal);
    float n = vnoise3(I.pos * 3.5 + float3(3.1, 1.7, 5.3));
    float thr = (1.0 - r) * 1.12 - 0.06;
    float vis = smoothstep(thr, thr + 0.05, n);
    float edge = (1.0 - smoothstep(thr + 0.05, thr + 0.18, n)) * vis;
    return float2(vis, edge);
}

// ---------------------------------------------------------------- 11 Emissive / Glow
ShadeOut shade_EM(ShadeIn I, Params P)
{
    float3 N = normalize(I.nrm);
    float3 V = normalize(I.view);
    float t = I.t;
    float3 q = rotY(I.pos, t * PF(CM_rotateSpeed)) * PF(EM_patternScale);
    float3 flow = float3(0.0, -t * PF(CM_flowSpeed), 0.0);
    float warp = PF(EM_distortion);
    float3 base = q + flow;
    float3 w = float3(fbm3(base + float3(0.0, 0.0, 5.0)), fbm3(base + float3(7.0, 3.0, 1.0)), fbm3(base + float3(2.0, 9.0, 4.0)));
    float3 qq = base + (w - float3(0.5, 0.5, 0.5)) * (2.4 * warp);
    float e = 0.0;
    float veins = PF(EM_veins);
    if (PF(EM_pattern) < 0.5)
    {
        float n = fbm3(qq);
        e = pow(1.0 - abs(2.0 * n - 1.0), 1.0 + 4.0 * veins);
    }
    else if (PF(EM_pattern) < 1.5)
    {
        float2 d = voro3(qq);
        e = 1.0 - smoothstep(0.0, 0.10 + 0.35 * (1.0 - veins), d.y - d.x);
        e = e * 0.85 + 0.15 * (1.0 - d.x);
    }
    else
    {
        float n = fbm3(float3(qq.x * 0.45, qq.y * 2.4, qq.z * 0.45));
        e = pow(saturate(n * 1.5 - 0.1), 1.0 + 2.0 * veins);
    }
    e = lerp(e, e * (0.55 + 0.9 * vnoise3(qq * 3.7)), PF(CM_noiseAmt));
    float facing = saturate(dot(N, V));
    float rim = pow(1.0 - facing, PF(EM_rimPower));
    float core = facing * facing * PF(EM_coreGlow);
    float3 c1 = PC(EM_color1);
    float3 c2 = PC(EM_color2);
    float3 c3 = PC(EM_color3);
    float3 ramp = lerp(c1, c2, smoothstep(0.0, 0.5, e));
    ramp = lerp(ramp, c3, smoothstep(0.5, 1.0, e));
    float live = pulseOf(I, P) * flickerOf(I, P);
    float3 rgb = ramp * ((0.22 + 1.1 * e) * PF(EM_intensity) * live);
    rgb = rgb + c2 * (rim * PF(EM_rimIntensity) * live * 1.5);
    rgb = rgb + c3 * (core * 0.5 * live);
    float2 rv = revealOf(I, P);
    rgb = (rgb + c3 * (rv.y * 3.0)) * rv.x;
    return makeOut(rgb, saturate(rv.x));
}

// ---------------------------------------------------------------- 12 Hologram / Energy
ShadeOut shade_HG(ShadeIn I, Params P)
{
    float t = I.t;
    float3 N = normalize(I.nrm);
    float3 V = normalize(I.view);
    float live = pulseOf(I, P) * flickerOf(I, P);
    float3 pp = rotY(I.pos, t * PF(CM_rotateSpeed));
    float y = I.pos.y;
    float band = floor(y * 9.0 + 9.0);
    float gstep = floor(t * PF(HG_glitchFreq) * 6.0);
    float gOn = step(1.0 - PF(HG_glitch) * 0.45, hash21(float2(band, gstep)));
    float shift = (hash21(float2(band + 11.0, gstep)) - 0.5) * 0.5 * gOn;
    pp = float3(pp.x + shift, pp.y, pp.z);
    float fres = pow(saturate(1.0 - abs(dot(N, V))), PF(HG_fresnel));
    float s = 0.5 + 0.5 * sin(6.2831853 * (y * PF(HG_scanDensity) * 0.5 - t * PF(HG_scanSpeed)));
    float scan = smoothstep(1.0 - PF(HG_scanWidth), 1.0, s);
    float3 gq = abs(frac(pp * PF(HG_gridScale) * 0.5) - float3(0.5, 0.5, 0.5));
    float grid = smoothstep(0.43, 0.5, max(max(gq.x, gq.y), gq.z));
    float barY = lerp(-1.4, 1.4, frac(t * 0.3));
    float bar = exp(-abs(y - barY) * 9.0);
    float tex = 0.55 + 0.45 * vnoise3(pp * 5.0 + float3(0.0, -t * 1.5, 0.0));
    float h = y * 0.5 + 0.5;
    float fade = lerp(1.0, 1.0 - 0.9 * h, PF(HG_heightFade));
    float body = PF(HG_transparency) * tex;
    float lum = body * 0.55 + scan * PF(HG_scanIntensity) * (0.35 + 0.6 * tex) + grid * PF(HG_gridStrength) * 0.7 + bar * PF(HG_sweep) * 1.1;
    float3 col = PC(HG_color);
    float3 edge = PC(HG_edgeColor);
    float3 rgb = (col * lum + edge * (fres * 1.7)) * (PF(HG_intensity) * live * fade);
    float a = saturate((body * 0.7 + scan * 0.55 + grid * 0.3 + bar * 0.6 + fres) * fade);
    float2 rv = revealOf(I, P);
    rgb = (rgb + edge * (rv.y * 2.5)) * rv.x;
    return makeOut(rgb, a * rv.x);
}

// ---------------------------------------------------------------- 14 Force field / Shield
float hexLines(float3 pos, float3 N, float scale)
{
    float3 w = abs(N);
    w = w * w * w * w;
    float sum = w.x + w.y + w.z + 0.0001;
    float d = 0.0;
    if (VFX_QUALITY >= 1)
    {
        float hx = hexEdge(float2(pos.y, pos.z) * scale);
        float hy = hexEdge(float2(pos.x, pos.z) * scale);
        float hz = hexEdge(float2(pos.x, pos.y) * scale);
        d = (hx * w.x + hy * w.y + hz * w.z) / sum;
    }
    else
    {
        // The mobile profile draws one projection, from the side the surface faces most.
        if (w.y > w.x && w.y > w.z) d = hexEdge(float2(pos.x, pos.z) * scale);
        else if (w.x > w.z) d = hexEdge(float2(pos.y, pos.z) * scale);
        else d = hexEdge(float2(pos.x, pos.y) * scale);
    }
    return smoothstep(0.40, 0.5, d);
}

ShadeOut shade_FF(ShadeIn I, Params P)
{
    float t = I.t;
    float3 N = normalize(I.nrm);
    float3 V = normalize(I.view);
    float live = pulseOf(I, P) * flickerOf(I, P);
    float3 pos = I.pos;
    float wob = vnoise3(pos * 3.0 + float3(0.0, t * 0.8, 0.0)) - 0.5;
    pos = pos + N * (wob * PF(FF_distortion) * 0.25);
    float3 spin = rotY(pos, t * PF(CM_rotateSpeed));
    float fres = pow(saturate(1.0 - abs(dot(N, V))), PF(FF_fresnel));
    float hex = hexLines(spin + float3(0.0, -t * PF(FF_hexScroll), 0.0), N, PF(FF_hexScale) * 0.5);
    float rip = 0.5 + 0.5 * sin(pos.y * 7.0 - t * 2.2 + wob * 3.0);
    rip = rip * rip * PF(FF_ripple);
    float hd = length(I.pos - I.hitPos);
    float r = max(PF(FF_hitRadius), 0.02);
    float age = I.hitAge;
    float ring = exp(-pow((hd - age * 1.4) / (r * 0.45), 2.0)) * exp(-age * PF(FF_hitDecay));
    float spot = exp(-hd * hd / (r * r)) * exp(-age * PF(FF_hitDecay) * 1.6);
    float hit = (ring * 0.9 + spot) * PF(FF_hitIntensity);
    float body = PF(FF_transparency);
    float3 col = PC(FF_color);
    float3 edge = PC(FF_edgeColor);
    float3 hc = PC(FF_hitColor);
    float3 rgb = col * (body * 0.9 + rip * 0.45 + hex * PF(FF_hexStrength) * (0.45 + 1.1 * fres + 0.9 * hit * 0.3));
    rgb = rgb + edge * (fres * PF(FF_edgeGlow));
    rgb = (rgb * live + hc * hit * (1.0 + hex * 1.5)) * PF(FF_intensity) * 0.55;
    float a = saturate(body + fres * 0.9 + hex * PF(FF_hexStrength) * 0.55 + rip * 0.2 + hit * 0.5);
    float2 rv = revealOf(I, P);
    rgb = (rgb + edge * (rv.y * 2.5)) * rv.x;
    return makeOut(rgb, a * rv.x);
}

// ---------------------------------------------------------------- 15 Particle / VFX material
ShadeOut shade_PV(ShadeIn I, Params P)
{
    float t = I.t;
    float2 c = I.uv * 2.0 - float2(1.0, 1.0);
    float r = length(c);
    float life = saturate(I.life);
    float soft = lerp(0.12, 0.85, PF(PV_softness));
    float shape = 1.0 - smoothstep(1.0 - soft, 1.0, r);
    float ns = PF(PV_noiseScale);
    float up = life * PF(CM_flowSpeed) * 5.0 + t * 0.15;
    float3 q = float3(c.x * ns, c.y * ns / PF(PV_stretch) - up, life * 2.0 + PF(CM_rotateSpeed) * t);
    float tb = PF(PV_turbulence);
    float3 w = float3(vnoise3(q * 0.7 + float3(0.0, 4.0, 0.0)), vnoise3(q * 0.7 + float3(5.0, 0.0, 2.0)), 0.0);
    float3 qq = q + float3(w.x - 0.5, w.y - 0.5, 0.0) * (2.0 * tb);
    float n = fbm3(qq);
    float v = 0.0;
    float k = PF(PV_pattern);
    if (k < 0.5)
    {
        float tongue = saturate(1.15 - (c.y * 0.5 + 0.5) * 1.1);
        v = saturate(shape * (0.2 + 1.5 * n) * (0.35 + tongue) - 0.12 * (c.y * 0.5 + 0.5));
    }
    else if (k < 1.5)
    {
        v = saturate(shape * (0.25 + 0.95 * n) * 0.9);
    }
    else if (k < 2.5)
    {
        float core = exp(-r * r * 9.0);
        float flare = exp(-abs(c.x) * 18.0 - abs(c.y) * 2.5) + exp(-abs(c.y) * 18.0 - abs(c.x) * 2.5);
        v = saturate(core * 1.4 + flare * 0.5) * lerp(0.7, 1.0, n);
    }
    else if (k < 3.5)
    {
        float ang = atan2(c.y, c.x) + r * (3.0 + 4.0 * tb) - t * 2.0 - life * 6.0;
        float arm = 0.5 + 0.5 * sin(ang * 3.0 + n * 3.0);
        v = saturate(shape * (0.15 + 1.1 * arm * (0.4 + n)));
    }
    else
    {
        float2 d = voro3(float3(c.x * ns * 1.4, c.y * ns * 1.4 - up, life));
        v = saturate(shape * (1.0 - smoothstep(0.0, 0.22, d.x)) * 1.5);
    }
    v = v * (1.0 + (PF(CM_noiseAmt) - 0.25) * (n - 0.5));
    float3 c1 = PC(PV_color1);
    float3 c2 = PC(PV_color2);
    float3 c3 = PC(PV_color3);
    float3 col = lerp(c1, c2, smoothstep(0.0, 0.5, life));
    col = lerp(col, c3, smoothstep(0.5, 1.0, life));
    float fin = smoothstep(0.0, max(PF(PV_fadeIn), 0.001), life);
    float fout = 1.0 - smoothstep(1.0 - max(PF(PV_fadeOut), 0.001), 1.0, life);
    float live = pulseOf(I, P) * flickerOf(I, P);
    float2 rv = revealOf(I, P);
    float a = saturate(v) * fin * fout * rv.x;
    return makeOut(col * (PF(PV_intensity) * v * live), a);
}

// ---------------------------------------------------------------- 16 UI / Sprite
// A rounded border, 0..1 across the sprite's rectangle with a margin left for the glow; negative
// inside the outer edge. Used when the shape comes from Border instead of a sprite.
float borderAlpha(float2 uv, float width, float radius)
{
    float2 q = abs(uv - float2(0.5, 0.5)) - float2(0.44 - radius, 0.44 - radius);
    float ox = max(q.x, 0.0);
    float oy = max(q.y, 0.0);
    float d = sqrt(ox * ox + oy * oy) + min(max(q.x, q.y), 0.0) - radius;
    float aa = 0.006;
    return (1.0 - smoothstep(-aa, aa, d)) * smoothstep(-width - aa, -width + aa, d);
}

ShadeOut shade_UI(ShadeIn I, Params P)
{
    float t = I.t;
    float m = I.spriteA;
    float2 uv = I.uv;
    float gy = saturate(uv.y + PF(UI_scrollV) * t * 0.25 + 0.5 * sin(PF(UI_scrollU) * t));
    float3 c1 = PC(UI_color1);
    float3 c2 = PC(UI_color2);
    float3 gc = PC(UI_glowColor);
    float3 oc = PC(UI_outlineColor);
    float live = pulseOf(I, P) * flickerOf(I, P);
    float3 body = lerp(c2, c1, gy) * live;
    // the diagonal shine
    float ang = PF(UI_shineAngle) * 0.0174533;
    float along = dot(uv - float2(0.5, 0.5), float2(cos(ang), sin(ang)));
    float sweep = 1.0 / max(PF(UI_shineSpeed), 0.05);
    float cycle = sweep + PF(UI_shineInterval);
    float ph = (t - cycle * floor(t / cycle)) / sweep;
    float pos = lerp(-0.85, 0.85, saturate(ph));
    float on = 1.0 - step(1.0, ph);
    float wdt = max(PF(UI_shineWidth), 0.02);
    float shine = exp(-pow((along - pos) / wdt, 2.0)) * on * PF(UI_shineIntensity);
    // rotating sparkles
    float sp = 0.0;
    if (PF(UI_sparkle) > 0.001)
    {
        float2 pc = (uv - float2(0.5, 0.5)) * 7.0;
        float cs = cos(t * 0.6);
        float sn = sin(t * 0.6);
        float2 pr = float2(cs * pc.x - sn * pc.y, sn * pc.x + cs * pc.y);
        float2 cell = floor(pr);
        float2 f = frac(pr) - float2(0.5, 0.5);
        float h = hash21(cell);
        float tw = 0.5 + 0.5 * sin(t * 3.0 + h * 40.0);
        float star = exp(-abs(f.x) * 24.0 - abs(f.y) * 4.0) + exp(-abs(f.y) * 24.0 - abs(f.x) * 4.0);
        sp = step(0.82, h) * tw * star * PF(UI_sparkle);
    }
    float3 rgb = body * m + gc * (shine * m + sp * m * 1.5);
    float outline = saturate(I.spriteOut - m) * step(0.001, PF(UI_outlineWidth));
    rgb = rgb + oc * (outline * live);
    float glow = saturate(I.spriteGlow - m) * PF(UI_glowIntensity) * live;
    rgb = rgb + gc * glow * 0.8;
    float a = saturate(m + outline + glow * 0.55);
    float2 rv = revealOf(I, P);
    rgb = (rgb + gc * (rv.y * 2.0 * m)) * rv.x;
    return makeOut(rgb, a * rv.x);
}

// ---------------------------------------------------------------- 18 Ice / Snow
ShadeOut shade_IC(ShadeIn I, Params P)
{
    float t = I.t;
    float3 N = normalize(I.nrm);
    float3 V = normalize(I.view);
    float3 L = normalize(float3(-0.45, 0.65, 0.6));
    float3 ice = PC(IC_iceColor);
    float3 deep = PC(IC_deepColor);
    float3 frostCol = PC(IC_frostColor);
    // facets: every cell of the crystal grid leans the surface a little differently
    float3 cp = I.pos * PF(IC_crystalScale);
    float3 fn = hash33(floor(cp)) * 2.0 - float3(1.0, 1.0, 1.0);
    float3 N2 = normalize(N + fn * (0.12 + 0.28 * PF(IC_refraction)));
    float diff = 0.45 + 0.55 * saturate(dot(N2, L));
    float3 R = N2 * (2.0 * dot(N2, L)) - L;
    float spec = pow(saturate(dot(R, V)), 18.0 + 220.0 * PF(IC_smoothness)) * (0.5 + 2.0 * PF(IC_smoothness));
    float facing = saturate(dot(N, V));
    float rim = pow(1.0 - facing, 3.0) * PF(IC_edgeHighlight);
    // the look of depth: a pattern that bends with the facets
    float inner = vnoise3(I.pos * PF(IC_crystalScale) * 1.6 + N2 * (PF(IC_refraction) * 1.8) + float3(0.0, t * 0.05, 0.0));
    float3 body = lerp(ice, deep, saturate(pow(1.0 - facing, 0.7) * 0.9 + inner * 0.35));
    // melt takes the top first
    float h = I.pos.y * 0.5 + 0.5;
    float meltEdge = 1.0 - PF(IC_melt) * 1.25;
    float meltMask = 1.0 - smoothstep(meltEdge, meltEdge + 0.25, h);
    float wet = PF(IC_melt) * 0.8;
    // frost and snow
    float fb = fbm3(I.pos * PF(IC_frostScale));
    float frostAmt = PF(IC_frost) * (1.0 - wet);
    float frostMask = smoothstep(1.0 - frostAmt - 0.12, 1.0 - frostAmt + 0.12, fb + 0.25 * (1.0 - facing));
    float up = smoothstep(0.1, 0.7, N.y + 0.25 * (fb - 0.5));
    float snowMask = saturate(PF(IC_snow) * 1.4 * up) * (1.0 - wet);
    // cracks
    float crack = 0.0;
    if (VFX_QUALITY >= 1)
    {
        float2 vc = voro3(I.pos * PF(IC_crackScale) * 0.6 + float3(0.0, 0.0, 1.5));
        crack = (1.0 - smoothstep(0.0, 0.06, vc.y - vc.x)) * PF(IC_cracks);
    }
    // shimmer: a bright band sliding over the facets
    float glint = pow(saturate(sin(dot(I.pos, float3(1.0, 0.8, 0.6)) * 4.0 - t * 1.6) * 0.5 + 0.5), 14.0) * PF(IC_shimmer) * hash31(floor(cp)) ;
    float3 rgb = body * diff + float3(1.0, 1.0, 1.0) * (spec + glint * 1.6) + frostCol * (rim * 0.5);
    rgb = lerp(rgb, frostCol * (0.7 + 0.3 * diff), saturate(frostMask * 0.85 + snowMask));
    rgb = rgb + float3(0.8, 0.95, 1.0) * crack * 0.9 * (1.0 - frostMask * 0.5);
    float a = lerp(PF(IC_transparency), 1.0, saturate(frostMask * 0.9 + snowMask + crack * 0.4 + rim * 0.2));
    a = a * (1.0 - wet * 0.35);
    float2 rv = revealOf(I, P);
    a = a * rv.x * meltMask;
    rgb = (rgb + float3(0.7, 0.95, 1.0) * (rv.y * 2.0)) * rv.x;
    return makeOut(rgb, saturate(a));
}
