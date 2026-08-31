/*
 * Part of Astonia Client (c) Daniel Brockhaus. Please read license.txt.
 *
 * Batched Sprite Vertex Shader (GLSL 450)
 *
 * Uses storage buffer for instanced sprite rendering.
 * Structure must match gpu_sprite_instance_t in sdl_gpu_batch.h (128 bytes)
 */

#version 450

// Vertex input (quad corners 0-1)
layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec2 inTexCoord;

// Per-instance sprite data (storage buffer)
// Matches gpu_sprite_instance_t exactly (96 bytes, 16-byte aligned)
struct SpriteInstance {
    // Position and size (16 bytes)
    float dest_x, dest_y;
    float dest_w, dest_h;

    // Texture coordinates (16 bytes)
    float src_u, src_v;
    float src_w, src_h;

    // Color modulation (16 bytes)
    float color_r, color_g, color_b, color_a;

    // Effect parameters (16 bytes)
    float light;        // Base light level (0-16)
    float freeze;       // Freeze effect (0-7)
    float shine;        // Shine effect (0-100)
    float alpha;        // Alpha (0-255)

    // Directional lighting (16 bytes)
    float ml, ll, rl, ul;  // Middle, left, right, up light

    // Additional parameters (16 bytes)
    float dl;           // Down light
    float c1, c2, c3;   // Colorization channels

    // Color balance (16 bytes)
    float cr, cg, cb, light_adj;

    // Saturation and flags (16 bytes)
    float saturation;
    uint flags;
    float padding1, padding2;
};

// Storage buffer for instances (set 0 = vertex storage buffers)
layout(std430, set = 0, binding = 0) readonly buffer InstanceBuffer {
    SpriteInstance instances[];
};

// Frame uniforms (set 1 = vertex uniform buffers)
layout(set = 1, binding = 0, std140) uniform FrameData {
    vec2 screenSize;    // Screen dimensions
    vec2 invScreenSize; // 1.0 / screenSize
} frame;

// Output to fragment shader
layout(location = 0) out vec2 fragTexCoord;
layout(location = 1) out vec4 fragColorMod;
layout(location = 2) out float fragAlpha;
layout(location = 3) flat out uint fragInstanceID;

void main() {
    // Get instance data
    SpriteInstance inst = instances[gl_InstanceIndex];

    // Calculate screen-space position
    vec2 screenPos = vec2(inst.dest_x, inst.dest_y) + inPosition * vec2(inst.dest_w, inst.dest_h);

    // Convert to NDC (-1 to 1)
    vec2 ndcPos = (screenPos * frame.invScreenSize) * 2.0 - 1.0;
    ndcPos.y = -ndcPos.y;  // Flip Y for screen coordinates

    gl_Position = vec4(ndcPos, 0.0, 1.0);

    // Calculate texture coordinates
    fragTexCoord = vec2(inst.src_u, inst.src_v) + inTexCoord * vec2(inst.src_w, inst.src_h);

    // Pass through color modulation and alpha
    fragColorMod = vec4(inst.color_r, inst.color_g, inst.color_b, inst.color_a);
    fragAlpha = inst.alpha / 255.0;
    fragInstanceID = gl_InstanceIndex;
}
