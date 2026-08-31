/*
 * Part of Astonia Client (c) Daniel Brockhaus. Please read license.txt.
 *
 * Primitive Rendering Fragment Shader (GLSL 450)
 *
 * Simple shader for rendering colored primitives.
 */

#version 450

// Input from vertex shader
layout(location = 0) in vec4 fragColor;

// Output color
layout(location = 0) out vec4 outColor;

void main() {
    outColor = fragColor;
}
