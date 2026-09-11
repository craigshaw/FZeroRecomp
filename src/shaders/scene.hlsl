// Windows equivalent of the Metal scene pass in presentation.c.
// Keep colour grading, sampling bounds, and style values in sync with it.
// SDL's shipped DXIL vertex shader uses TEXCOORD0 for colour and TEXCOORD1
// for UV (its HLSL source names are remapped during shader cross-compilation).
// The drawn texture is sampler slot 0.
Texture2D<float4> tex : register(t0, space2);
SamplerState smp : register(s0, space2);
cbuffer SceneParams : register(b0, space3) { float4 params; };

struct In { float4 color : TEXCOORD0; float2 uv : TEXCOORD1; };

float4 scene(In input) : SV_Target0 {
    // SNES output is already colour-correct. Ignore its unused alpha byte.
    float3 c = tex.Sample(smp, input.uv).rgb;
    if (params.x < 0.5) return float4(c, 1.0);
    float l = dot(c, float3(0.2126, 0.7152, 0.0722));
    if (params.x > 2.5) return float4(l, l, l, 1.0);
    if (params.x > 1.5) {
        float peak = max(c.r, max(c.g, c.b));
        float low = min(c.r, min(c.g, c.b));
        float amount = min(1.85, peak / max(peak-low, 0.00001));
        float3 vivid = clamp(peak + (c-peak)*amount, 0.0, 1.0);
        vivid += 0.22 * (vivid-0.5) * 4.0 * vivid * (1.0-vivid);
        return float4(clamp(vivid, 0.0, 1.0), 1.0);
    }
    float3 graded = lerp(float3(l, l, l), c, 1.10);
    graded += (graded - 0.5) * 0.055 * min(l * 4.0, 1.0);
    graded += float3(-0.010, 0.003, 0.018) * l * (1.0 - l);
    float2 lo = params.yz * 0.5, hi = 1.0 - lo;
    if (params.w > 0.0 && input.uv.x >= params.w * params.y &&
        input.uv.x < (params.w + 256.0) * params.y) {
        lo.x = (params.w + 0.5) * params.y;
        hi.x = (params.w + 255.5) * params.y;
    }
    float3 glow = float3(0.0, 0.0, 0.0);
    for (int y = -2; y <= 2; ++y) for (int x = -2; x <= 2; ++x) {
        float2 uv = clamp(input.uv + float2(x, y) * params.yz, lo, hi);
        float3 tap = tex.Sample(smp, uv).rgb;
        float peak = max(tap.r, max(tap.g, tap.b));
        float weight = float((3 - abs(x)) * (3 - abs(y))) / 81.0;
        glow += tap * smoothstep(0.65, 1.0, peak) * weight;
    }
    return float4(clamp(graded + glow * 0.18, 0.0, 1.0), 1.0);
}
