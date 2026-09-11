/*
 * Part of Astonia Client (c) Daniel Brockhaus. Please read license.txt.
 *
 * Simple Sprite Fragment Shader (GLSL 450)
 * Basic textured rendering with color modulation.
 */

#version 450

// Input from vertex shader
layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec4 fragColor;

// Output color
layout(location = 0) out vec4 outColor;

// Sprite texture
layout(set = 2, binding = 0) uniform sampler2D spriteTexture;

void main() {
    vec4 texColor = texture(spriteTexture, fragTexCoord);

    // Discard fully transparent pixels
    if (texColor.a < 0.01) {
        discard;
    }

    // Apply color modulation
    outColor = texColor * fragColor;
}
