// VFX Forge: draws the 3D model an effect was made on, as VFX Forge draws it:
// the colours of the model in three toon tones with a light rim at its edge.
// It writes depth, so particles that pass behind it are hidden.
// Works in the Built-in pipeline and in URP.
Shader "VFX Forge/Model"
{
    Properties
    {
        _Light ("Light direction (world)", Vector) = (-0.45, 0.65, -0.6, 0)
    }

    SubShader
    {
        Tags { "Queue" = "Geometry" "RenderType" = "Opaque" }
        ZWrite On
        Cull Off

        Pass
        {
            CGPROGRAM
            #pragma vertex vert
            #pragma fragment frag
            #include "UnityCG.cginc"

            float4 _Light;

            struct appdata
            {
                float4 vertex : POSITION;
                float3 normal : NORMAL;
                fixed4 color : COLOR;
            };
            struct v2f
            {
                float4 pos : SV_POSITION;
                float3 normal : TEXCOORD0;
                float3 view : TEXCOORD1;
                fixed4 color : COLOR;
            };

            float3 vfToLinear(float3 c)
            {
                c = saturate(c);
                return c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4);
            }
            float band(float x, float at) { return saturate((x - at) / 0.08 + 0.5); }

            v2f vert(appdata v)
            {
                v2f o;
                o.pos = UnityObjectToClipPos(v.vertex);
                o.normal = UnityObjectToWorldNormal(v.normal);
                o.view = normalize(_WorldSpaceCameraPos - mul(unity_ObjectToWorld, v.vertex).xyz);
                o.color = v.color;
                return o;
            }

            float4 frag(v2f i) : SV_Target
            {
                float3 n = normalize(i.normal);
                float facing = dot(n, normalize(i.view));
                if (facing < 0.0)
                {
                    // The inside of the model, seen through a hole: lit as its outside.
                    n = -n;
                    facing = -facing;
                }
                float lit = dot(n, normalize(_Light.xyz));
                float tone = 0.5 + 0.28 * band(lit, -0.2) + 0.22 * band(lit, 0.45);
                float rim = 0.28 * band(1.0 - facing, 0.72);
                float3 c = min(1.0, i.color.rgb * tone + rim);
#if !defined(UNITY_COLORSPACE_GAMMA)
                c = vfToLinear(c);
#endif
                return float4(c, 1.0);
            }
            ENDCG
        }
    }
}
