/*
 * Part of Astonia Client (c) Daniel Brockhaus. Please read license.txt.
 *
 * Sprite Effect Vertex Shader (GLSL 450)
 *
 * Instanced quad expansion for the shader-effects sprite path.
 * Instance layout must match gpu_fx_instance_t in src/sdl/sdl_gpu_shaderfx.h
 * (128 bytes, std430).
 */

#version 450

layout(location = 0) in vec2 inPosition; // quad corner 0-1
layout(location = 1) in vec2 inTexCoord; // 0-1 (equals inPosition for the quad)

struct FxInstance {
    vec4 dest;       // dest x,y,w,h in device pixels
    ivec4 src;       // source region in page texels x,y,w,h
    ivec4 org_sz;    // sprite origin (xy) and full size (zw) in page texels
    uvec4 colorize;  // c1,c2,c3 (raw, incl 0x8000), flags
    ivec4 balance;   // cr,cg,cb,light
    ivec4 fx;        // sat, shine, freeze, sink_px
    ivec4 light_a;   // ml, ll, rl, ul
    ivec4 light_b;   // dl, alpha, reserved, reserved
};

layout(std430, set = 0, binding = 0) readonly buffer InstanceBuffer {
    FxInstance instances[];
};

layout(std140, set = 1, binding = 0) uniform VSFrame {
    vec2 screenSize;
    vec2 invScreenSize;
    uint baseInstance;
    uint pad0;
    uint pad1;
    uint pad2;
};

layout(location = 0) out vec2 fragTexel;       // position in page texels
layout(location = 1) flat out uint fragInstance;

void main()
{
    uint idx = baseInstance + uint(gl_InstanceIndex);
    FxInstance inst = instances[idx];

    vec2 screenPos = inst.dest.xy + inPosition * inst.dest.zw;
    vec2 ndc = screenPos * invScreenSize * 2.0 - 1.0;
    ndc.y = -ndc.y;

    gl_Position = vec4(ndc, 0.0, 1.0);
    fragTexel = vec2(inst.src.xy) + inTexCoord * vec2(inst.src.zw);
    fragInstance = idx;
}
