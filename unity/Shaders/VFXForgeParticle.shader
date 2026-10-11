// VFX Forge: draws particles with the built-in VFX Forge shapes, or with
// the artist's own picture.
//
// The shape picture holds every shape: how much of the particle shows in
// its alpha, the toon tone (a lighter or darker shade) in its red, and the
// white gloss in its green.
// With Picture on, _MainTex is the artist's picture instead, tinted by the
// particle's colour; a sprite sheet is stepped through by the Particle
// System's Texture Sheet Animation, which hands over the right cell.
// Colours arrive premultiplied, so ordinary and glowing (additive)
// particles both use one blend: source + destination * (1 - source alpha).
// Works in the Built-in pipeline and in URP, including the 2D Renderer.
Shader "VFX Forge/Particle"
{
    Properties
    {
        _MainTex ("Shape picture", 2D) = "white" {}
        _Shape ("Shape", Float) = 0
        _Columns ("Columns in the picture", Float) = 8
        _Rows ("Rows in the picture", Float) = 4
        _Glow ("Glow", Float) = 1
        [Toggle] _Additive ("Glow blending (additive)", Float) = 0
        _MotionAxis ("Streak direction", Float) = 0
        [Toggle] _Picture ("Draw the picture as painted", Float) = 0
        [Toggle] _Ribbon ("Draw a trail ribbon", Float) = 0
        _Dissolve ("Dissolve: share of life at the end", Range(0, 1)) = 0
        _DissolveSoft ("Dissolve softness", Range(0, 1)) = 0.15
        _DissolveEdge ("Dissolve edge glow", Range(0, 1)) = 0
        _SheetColumns ("Sprite sheet columns (for dissolve)", Float) = 1
        _SheetRows ("Sprite sheet rows (for dissolve)", Float) = 1
    }

    SubShader
    {
        Tags { "Queue" = "Transparent" "RenderType" = "Transparent" "IgnoreProjector" = "True" "PreviewType" = "Plane" }
        Blend One OneMinusSrcAlpha
        ZWrite Off
        Cull Off

        Pass
        {
            CGPROGRAM
            #pragma vertex vert
            #pragma fragment frag
            #include "UnityCG.cginc"

            sampler2D _MainTex;
            float _Shape;
            float _Columns;
            float _Rows;
            float _Glow;
            float _Additive;
            float _MotionAxis;
            float _Picture;
            float _Ribbon;
            float _Dissolve;
            float _DissolveSoft;
            float _DissolveEdge;
            float _SheetColumns;
            float _SheetRows;

            struct appdata
            {
                float4 vertex : POSITION;
                float4 color : COLOR;
                // xy: the picture position. For a dissolving layer the importer
                // also sends z: how far through its life the particle is, and
                // w: a number of its own from 0 to 1.
                float4 uv : TEXCOORD0;
            };

            struct v2f
            {
                float4 pos : SV_POSITION;
                float4 color : COLOR;
                float4 uv : TEXCOORD0;
            };

            v2f vert(appdata v)
            {
                v2f o;
                o.pos = UnityObjectToClipPos(v.vertex);
                o.color = v.color;
                o.uv = v.uv;
                return o;
            }

            // Dissolve: the particle eaten away in ragged patches as it dies.
            // The same sums as VFX Forge's own preview.
            float dissolveSpeck(float2 c)
            {
                float3 q = frac(float3(c.x, c.y, c.x) * 0.1031);
                q += dot(q, q.yzx + 33.33);
                return frac((q.x + q.y) * q.z);
            }

            float dissolveLumps(float2 q)
            {
                float2 i = floor(q);
                float2 f = q - i;
                f = f * f * (3.0 - 2.0 * f);
                float low = lerp(dissolveSpeck(i), dissolveSpeck(i + float2(1.0, 0.0)), f.x);
                float high = lerp(dissolveSpeck(i + float2(0.0, 1.0)), dissolveSpeck(i + float2(1.0, 1.0)), f.x);
                return lerp(low, high, f.y);
            }

            // x: how much of this point is left; y: how brightly it glows for
            // being near the eaten edge. own is the particle's own -1..1 square.
            float2 dissolveAt(float2 own, float age, float seed)
            {
                if (_Dissolve <= 0.0)
                    return float2(1.0, 0.0);
                float amount = saturate((age - (1.0 - _Dissolve)) / _Dissolve);
                if (amount <= 0.0)
                    return float2(1.0, 0.0);
                float2 q = own * 1.7 + float2(seed * 37.0, seed * 91.0);
                float2 q2 = float2(0.8 * q.x - 0.6 * q.y, 0.6 * q.x + 0.8 * q.y) * 2.1 + float2(5.3, 1.7);
                float2 q3 = float2(0.28 * q.x + 0.96 * q.y, -0.96 * q.x + 0.28 * q.y) * 4.3 + float2(9.1, 3.3);
                float n = 0.5 * dissolveLumps(q) + 0.3 * dissolveLumps(q2) + 0.2 * dissolveLumps(q3);
                n += 0.1 * (0.5 - min(length(own), 1.0));
                n = saturate((n - 0.2033) / (0.7556 - 0.2033));
                n = n * n * (3.0 - 2.0 * n);
                float width = 0.03 + 0.6 * _DissolveSoft;
                float past = n - (amount * (1.0 + width) - width);
                float keep = saturate(past / width);
                float inner = saturate((past - width) / 0.2);
                float rim = _DissolveEdge * (1.0 - inner) * (1.0 - inner) * min(amount * 6.0, 1.0);
                return float2(keep, rim);
            }

            float4 frag(v2f i) : SV_Target
            {
                if (_Ribbon > 0.5)
                {
                    // A trail: solid in the middle, soft at the edges, with a
                    // white-hot centre line.
                    float c = abs(i.uv.y * 2.0 - 1.0);
                    float body = saturate((1.0 - c) * 2.2);
                    float ra = i.color.a;
                    float3 rrgb = i.color.rgb * (ra * _Glow);
                    float rwhite = max(ra, max(rrgb.r, max(rrgb.g, rrgb.b)));
                    rrgb += (rwhite - rrgb) * (saturate(1.0 - c * 2.5) * 0.6);
                    return float4(rrgb, _Additive > 0.5 ? 0.0 : ra) * body;
                }
                if (_Picture > 0.5)
                {
                    // The picture, premultiplied, tinted and faded by the particle.
                    float4 p = tex2D(_MainTex, i.uv.xy);
                    float a = i.color.a;
                    float3 tint = i.color.rgb * (a * _Glow);
                    float3 prgb = tint * p.rgb * p.a;
                    float pa = (_Additive > 0.5 ? 0.0 : a) * p.a;
                    // Where on the particle this is: within its own sprite-sheet cell.
                    float2 cell = frac(i.uv.xy * float2(_SheetColumns, _SheetRows));
                    float2 gone = dissolveAt(float2(cell.x * 2.0 - 1.0, cell.y * 2.0 - 1.0), i.uv.z, i.uv.w);
                    float pwhite = max(pa, max(prgb.r, max(prgb.g, prgb.b)));
                    prgb += (pwhite - prgb) * gone.y;
                    return float4(prgb, pa) * gone.x;
                }

                // Across the particle from 0 to 1, with v pointing up.
                float2 local = i.uv.xy;
                if (_MotionAxis > 0.5)
                    local = float2(local.y, 1.0 - local.x);
                local = clamp(local, 0.004, 0.996);

                // Pick this particle's shape out of the grid.
                float s = floor(_Shape + 0.5);
                float column = fmod(s, _Columns);
                float row = floor(s / _Columns);
                float2 uv = float2((column + local.x) / _Columns, 1.0 - (row + 1.0 - local.y) / _Rows);
                float4 t = tex2D(_MainTex, uv);
                float cover = t.a;
                float tone = t.r * 2.0 - 1.0;
                if (abs(tone) < 0.01)
                    tone = 0.0;
                float shine = t.g;

                // Being eaten away as it dies.
                float2 eaten = dissolveAt(local * 2.0 - 1.0, i.uv.z, i.uv.w);
                cover *= eaten.x;
                shine = max(shine, eaten.y);

                float alpha = i.color.a;
                float3 rgb = i.color.rgb * (alpha * _Glow);

                // Toon shading: a highlight moves toward white at the
                // particle's own brightness; a shadow dims.
                if (tone > 0.0)
                {
                    float most = max(rgb.r, max(rgb.g, rgb.b));
                    rgb = (rgb + (most - rgb) * (0.5 * tone)) * (1.0 + 0.3 * tone);
                }
                else
                {
                    rgb *= 1.0 + 0.5 * tone;
                }

                // Gloss: toward white at the particle's own brightness.
                float white = max(alpha, max(rgb.r, max(rgb.g, rgb.b)));
                rgb += (white - rgb) * shine;

                return float4(rgb, _Additive > 0.5 ? 0.0 : alpha) * cover;
            }
            ENDCG
        }
    }
}
