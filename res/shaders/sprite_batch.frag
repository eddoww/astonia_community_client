/*
 * Part of Astonia Client (c) Daniel Brockhaus. Please read license.txt.
 *
 * Batched Sprite Fragment Shader (GLSL 450)
 *
 * Simple fragment shader for batched sprite rendering.
 */

#version 450

// Input from vertex shader
layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec4 fragColorMod;
layout(location = 2) in float fragAlpha;
layout(location = 3) flat in uint fragInstanceID;

// Sprite texture (set 2 = fragment samplers)
layout(set = 2, binding = 0) uniform sampler2D spriteTexture;

// Output color
layout(location = 0) out vec4 outColor;

void main() {
    // Sample sprite texture
    vec4 texColor = texture(spriteTexture, fragTexCoord);

    // Early out for fully transparent pixels
    if (texColor.a < 0.01) {
        discard;
    }

    // Apply color modulation
    vec3 color = texColor.rgb * fragColorMod.rgb;

    // Calculate final alpha
    float alpha = texColor.a * fragAlpha * fragColorMod.a;

    outColor = vec4(color, alpha);
}
