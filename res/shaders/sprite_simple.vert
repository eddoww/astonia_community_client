/*
 * Part of Astonia Client (c) Daniel Brockhaus. Please read license.txt.
 *
 * Simple Sprite Vertex Shader (GLSL 450)
 * Renders textured quads with basic transforms.
 */

#version 450

// Vertex input (quad corners 0-1)
layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec2 inTexCoord;

// Per-sprite data (set 1 = vertex uniform buffers in SDL3 GPU)
layout(set = 1, binding = 0, std140) uniform SpriteData {
    vec4 destRect;      // x, y, width, height in screen pixels
    vec4 srcRect;       // u, v, uWidth, vHeight (0-1)
    vec4 colorMod;      // RGBA color modulation
    vec2 screenSize;    // Screen dimensions for NDC conversion
    vec2 padding;
} sprite;

// Output to fragment shader
layout(location = 0) out vec2 fragTexCoord;
layout(location = 1) out vec4 fragColor;

void main() {
    // Calculate screen-space position
    vec2 screenPos = sprite.destRect.xy + inPosition * sprite.destRect.zw;

    // Convert to NDC (-1 to 1)
    vec2 ndcPos = (screenPos / sprite.screenSize) * 2.0 - 1.0;
    ndcPos.y = -ndcPos.y;  // Flip Y for screen coordinates

    gl_Position = vec4(ndcPos, 0.0, 1.0);

    // Calculate texture coordinates
    fragTexCoord = sprite.srcRect.xy + inTexCoord * sprite.srcRect.zw;
    fragColor = sprite.colorMod;
}
