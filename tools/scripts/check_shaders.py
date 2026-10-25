#!/usr/bin/env python3
"""Checks exported VFX Forge .shader files for HLSL errors with glslangValidator.

Unity is not available here, so this is a partial check: the CG body of each shader is wrapped
with stand-ins for Unity's own functions and parsed as HLSL (fragment and vertex stages).
It catches syntax, type and undefined-name errors in the generated and shared code; it cannot
catch anything specific to Unity's compiler or render pipelines.

Usage: check_shaders.py <folder with .shader files>
"""
import pathlib
import re
import subprocess
import sys
import tempfile

PRELUDE = """
#define fixed float
#define fixed4 float4
#define fixed3 float3
#define half float
#define UNITY_VERTEX_INPUT_INSTANCE_ID
#define UNITY_VERTEX_OUTPUT_STEREO
#define UNITY_SETUP_INSTANCE_ID(v)
#define UNITY_INITIALIZE_VERTEX_OUTPUT_STEREO(o)
#define tex2Dlod(s, u) float4(1.0, 1.0, 1.0, 1.0)
float4 _Time;
float4 UnityObjectToClipPos(float4 v) { return v; }
float3 ObjSpaceViewDir(float4 v) { return v.xyz; }
float UnityGet2DClipping(float2 position, float4 clipRect) { return position.x * clipRect.x; }
#define UNITY_UI_CLIP_RECT 1
#define UNITY_UI_ALPHACLIP 1
"""


URP_PRELUDE = """
#define CBUFFER_START(name)
#define CBUFFER_END
float4 _Time;
float4 TransformObjectToHClip(float3 p) { return float4(p, 1.0); }
float3 TransformWorldToObject(float3 p) { return p; }
float3 GetCameraPositionWS() { return float3(0.0, 0.0, 0.0); }
"""


def body(text, urp=False):
    m = re.search(r"HLSLPROGRAM(.*?)ENDHLSL", text, re.S) if urp else re.search(r"CGPROGRAM(.*?)ENDCG", text, re.S)
    if not m:
        return None
    lines = []
    for line in m.group(1).splitlines():
        s = line.strip()
        if s.startswith("#pragma") or s.startswith("#include"):
            continue
        lines.append(line)
    code = "\n".join(lines)
    code = re.sub(r"sampler2D\s+_MainTex\s*;", "", code)
    return code


def main():
    root = pathlib.Path(sys.argv[1])
    files = sorted(root.rglob("*.shader"))
    if not files:
        print("no shaders found")
        return 1
    bad = 0
    for f in files:
        text = f.read_text()
        # (code, prelude, name): the Built-in block always, the URP block when the shader has one
        # (the UI family has none).
        blocks = []
        code = body(text)
        if code is None:
            print("FAIL", f, "no CGPROGRAM")
            bad += 1
            continue
        blocks.append((code, PRELUDE, "built-in"))
        urp = body(text, True)
        if urp is not None:
            blocks.append((urp, URP_PRELUDE, "urp"))
        failed = False
        for code, prelude, which in blocks:
          for entry, stage in (("frag", "frag"), ("vert", "vert")):
            with tempfile.NamedTemporaryFile("w", suffix=".hlsl", delete=False) as t:
                t.write(prelude + code)
                path = t.name
            r = subprocess.run(["glslangValidator", "-D", "-S", stage, "-e", entry, "-V", "--keep-uncalled", path],
                               capture_output=True, text=True)
            out = (r.stdout + r.stderr).strip()
            errs = [l for l in out.splitlines() if "ERROR" in l]
            if r.returncode != 0 or errs:
                bad += 1
                failed = True
                print("FAIL", f.name, which, entry)
                print("\n".join(errs[:12]))
                break
          if failed:
            break
        else:
            print("ok  ", f.name, "(built-in + urp)" if len(blocks) > 1 else "(built-in)")
    print("%d of %d failed" % (bad, len(files)))
    return 1 if bad else 0


sys.exit(main())
