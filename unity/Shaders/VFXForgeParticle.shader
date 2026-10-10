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

            struct appdata
            {
                float4 vertex : POSITION;
                float4 color : COLOR;
                float2 uv : TEXCOORD0;
            };

            struct v2f
            {
                float4 pos : SV_POSITION;
                float4 color : COLOR;
                float2 uv : TEXCOORD0;
            };

            v2f vert(appdata v)
            {
                v2f o;
                o.pos = UnityObjectToClipPos(v.vertex);
                o.color = v.color;
                o.uv = v.uv;
                return o;
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
                    float4 p = tex2D(_MainTex, i.uv);
                    float a = i.color.a;
                    float3 tint = i.color.rgb * (a * _Glow);
                    return float4(tint * p.rgb * p.a, (_Additive > 0.5 ? 0.0 : a) * p.a);
                }

                // Across the particle from 0 to 1, with v pointing up.
                float2 local = i.uv;
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
