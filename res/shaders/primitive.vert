/*
 * Part of Astonia Client (c) Daniel Brockhaus. Please read license.txt.
 *
 * Primitive Rendering Vertex Shader (GLSL 450)
 *
 * Simple shader for rendering colored primitives using push constants.
 */

#version 450

// Vertex input (quad corners 0-1)
layout(location = 0) in vec2 inPosition;

// Per-primitive data (set 1 = vertex uniform buffers in SDL3 GPU)
layout(set = 1, binding = 0, std140) uniform PrimitiveData {
    vec4 destRect;      // x, y, width, height in screen pixels
    vec4 color;         // RGBA color
    vec2 screenSize;    // Screen dimensions for NDC conversion
    vec2 padding;
} prim;

// Output to fragment shader
layout(location = 0) out vec4 fragColor;

void main() {
    // Calculate screen-space position
    vec2 screenPos = prim.destRect.xy + inPosition * prim.destRect.zw;

    // Convert to NDC (-1 to 1)
    vec2 ndcPos = (screenPos / prim.screenSize) * 2.0 - 1.0;
    ndcPos.y = -ndcPos.y;  // Flip Y for screen coordinates

    gl_Position = vec4(ndcPos, 0.0, 1.0);
    fragColor = prim.color;
}
