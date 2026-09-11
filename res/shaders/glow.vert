/*
 * Part of Astonia Client (c) Daniel Brockhaus. Please read license.txt.
 *
 * Glow Vertex Shader (GLSL 450)
 *
 * Instanced quad expansion for the spell-effect glow path. Each instance
 * is a CAPSULE - a line segment plus a radius - which covers both the
 * point case (p0 == p1: bless/potion/heal sparkles, pulse dots) and the
 * line case (p0 != p1: lightning bolts, rain streaks) with one pipeline.
 *
 * The quad is the segment's bounding box grown by the radius; the
 * fragment shader does the distance-to-segment falloff.
 *
 * Instance layout must match gpu_glow_instance_t in
 * src/sdl/sdl_gpu_glow.h (48 bytes, std430).
 */

#version 450

layout(location = 0) in vec2 inPosition; // quad corner 0-1

struct GlowInstance {
    vec4 seg;   // p0.xy, p1.xy in device pixels
    vec4 shape; // radius (device px), core weight, unused, unused
    vec4 color; // rgb + intensity
};

layout(std430, set = 0, binding = 0) readonly buffer InstanceBuffer {
    GlowInstance instances[];
};

layout(std140, set = 1, binding = 0) uniform VSFrame {
    vec2 invScreenSize;
    uint baseInstance;
    uint pad0;
};

layout(location = 0) out vec2 fragPos; // device-pixel position of this fragment
layout(location = 1) flat out vec4 fragSeg;
layout(location = 2) flat out vec2 fragShape;
layout(location = 3) flat out vec4 fragColor;

void main()
{
    uint idx = baseInstance + uint(gl_InstanceIndex);
    GlowInstance inst = instances[idx];

    /* one pixel of slack so the falloff reaches zero inside the quad
     * instead of being clipped at its edge */
    float pad = inst.shape.x + 1.0;
    vec2 lo = min(inst.seg.xy, inst.seg.zw) - pad;
    vec2 hi = max(inst.seg.xy, inst.seg.zw) + pad;

    vec2 screenPos = mix(lo, hi, inPosition);
    vec2 ndc = screenPos * invScreenSize * 2.0 - 1.0;
    ndc.y = -ndc.y;

    gl_Position = vec4(ndc, 0.0, 1.0);
    fragPos = screenPos;
    fragSeg = inst.seg;
    fragShape = inst.shape.xy;
    fragColor = inst.color;
}
