// The little part of HLSL the shader families use, written in C++, so the very same shader
// source (editor/shaders/ShaderCore.hlsli) runs here for the preview and on the GPU in Unity.
#pragma once

#include <cmath>

namespace vfx::editor::hlsl {

struct float2 {
    float x = 0, y = 0;
    float2() = default;
    explicit float2(float a) : x(a), y(a) {}
    float2(float a, float b) : x(a), y(b) {}
};
struct float3 {
    float x = 0, y = 0, z = 0;
    float3() = default;
    explicit float3(float a) : x(a), y(a), z(a) {}
    float3(float a, float b, float c) : x(a), y(b), z(c) {}
};
struct float4 {
    float x = 0, y = 0, z = 0, w = 0;
    float4() = default;
    explicit float4(float a) : x(a), y(a), z(a), w(a) {}
    float4(float a, float b, float c, float d) : x(a), y(b), z(c), w(d) {}
};

#define VFX_BINARY(T, FIELDS)                                                                         \
    inline T operator+(T a, T b) { return T FIELDS(a, b, +); }                                       \
    inline T operator-(T a, T b) { return T FIELDS(a, b, -); }                                       \
    inline T operator*(T a, T b) { return T FIELDS(a, b, *); }                                       \
    inline T operator/(T a, T b) { return T FIELDS(a, b, /); }

#define VFX_F2(a, b, op) (a.x op b.x, a.y op b.y)
#define VFX_F3(a, b, op) (a.x op b.x, a.y op b.y, a.z op b.z)
#define VFX_F4(a, b, op) (a.x op b.x, a.y op b.y, a.z op b.z, a.w op b.w)
VFX_BINARY(float2, VFX_F2)
VFX_BINARY(float3, VFX_F3)
VFX_BINARY(float4, VFX_F4)
#undef VFX_F2
#undef VFX_F3
#undef VFX_F4
#undef VFX_BINARY

inline float2 operator*(float2 a, float s) { return {a.x * s, a.y * s}; }
inline float2 operator*(float s, float2 a) { return {a.x * s, a.y * s}; }
inline float2 operator/(float2 a, float s) { return {a.x / s, a.y / s}; }
inline float3 operator*(float3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
inline float3 operator*(float s, float3 a) { return {a.x * s, a.y * s, a.z * s}; }
inline float3 operator/(float3 a, float s) { return {a.x / s, a.y / s, a.z / s}; }
inline float4 operator*(float4 a, float s) { return {a.x * s, a.y * s, a.z * s, a.w * s}; }
inline float2 operator-(float2 a) { return {-a.x, -a.y}; }
inline float3 operator-(float3 a) { return {-a.x, -a.y, -a.z}; }
inline float3& operator+=(float3& a, float3 b) { a = a + b; return a; }

// ---- scalars
inline float sin(float a) { return std::sin(a); }
inline float cos(float a) { return std::cos(a); }
inline float atan2(float y, float x) { return std::atan2(y, x); }
inline float sqrt(float a) { return std::sqrt(a); }
inline float exp(float a) { return std::exp(a); }
inline float pow(float a, float b) { return std::pow(a, b); }
inline float abs(float a) { return std::fabs(a); }
inline float floor(float a) { return std::floor(a); }
inline float frac(float a) { return a - std::floor(a); }
inline float saturate(float a) { return a < 0 ? 0 : (a > 1 ? 1 : a); }
inline float min(float a, float b) { return a < b ? a : b; }
inline float max(float a, float b) { return a > b ? a : b; }
inline float step(float edge, float x) { return x < edge ? 0.0f : 1.0f; }
inline float lerp(float a, float b, float t) { return a + (b - a) * t; }
inline float smoothstep(float a, float b, float x) {
    float t = saturate((x - a) / (b - a));
    return t * t * (3.0f - 2.0f * t);
}

// ---- vectors
inline float2 floor(float2 a) { return {std::floor(a.x), std::floor(a.y)}; }
inline float3 floor(float3 a) { return {std::floor(a.x), std::floor(a.y), std::floor(a.z)}; }
inline float2 frac(float2 a) { return {frac(a.x), frac(a.y)}; }
inline float3 frac(float3 a) { return {frac(a.x), frac(a.y), frac(a.z)}; }
inline float2 abs(float2 a) { return {std::fabs(a.x), std::fabs(a.y)}; }
inline float3 abs(float3 a) { return {std::fabs(a.x), std::fabs(a.y), std::fabs(a.z)}; }
inline float dot(float2 a, float2 b) { return a.x * b.x + a.y * b.y; }
inline float dot(float3 a, float3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline float length(float2 a) { return std::sqrt(dot(a, a)); }
inline float length(float3 a) { return std::sqrt(dot(a, a)); }
inline float3 normalize(float3 a) {
    const float l = length(a);
    return l > 1e-20f ? a / l : float3(0, 1, 0);
}
inline float2 lerp(float2 a, float2 b, float t) { return a + (b - a) * t; }
inline float3 lerp(float3 a, float3 b, float t) { return a + (b - a) * t; }

}  // namespace vfx::editor::hlsl
