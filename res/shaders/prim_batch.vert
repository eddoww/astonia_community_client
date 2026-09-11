/*
 * Part of Astonia Client (c) Daniel Brockhaus. Please read license.txt.
 *
 * Batched Primitive Vertex Shader (GLSL 450)
 *
 * Instanced version of primitive.vert. The unbatched pipeline takes one
 * rectangle per uniform push, which costs a pipeline bind, a vertex
 * buffer bind and a draw call EACH - and the primitive path is how the
 * client draws circles, arcs, rings and anti-aliased lines, all of which
 * decompose into one 1x1 rect per plotted pixel. A single arc is up to
 * 361 draw calls that way.
 *
 * Instance layout must match gpu_prim_instance_t in
 * src/sdl/sdl_gpu_prim.h (32 bytes, std430).
 */

#version 450

layout(location = 0) in vec2 inPosition; // quad corner 0-1

struct PrimInstance {
    vec4 dest;  // x, y, w, h in device pixels
    vec4 color; // rgba
};

layout(std430, set = 0, binding = 0) readonly buffer InstanceBuffer {
    PrimInstance instances[];
};

layout(std140, set = 1, binding = 0) uniform VSFrame {
    vec2 invScreenSize;
    uint baseInstance;
    uint pad0;
};

layout(location = 0) out vec4 fragColor;

void main()
{
    uint idx = baseInstance + uint(gl_InstanceIndex);
    PrimInstance inst = instances[idx];

    vec2 screenPos = inst.dest.xy + inPosition * inst.dest.zw;
    vec2 ndc = screenPos * invScreenSize * 2.0 - 1.0;
    ndc.y = -ndc.y;

    gl_Position = vec4(ndc, 0.0, 1.0);
    fragColor = inst.color;
}
