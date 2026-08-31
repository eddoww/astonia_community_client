/*
 * Part of Astonia Client (c) Daniel Brockhaus. Please read license.txt.
 *
 * Triangle Rendering Fragment Shader (GLSL 450)
 *
 * Outputs the interpolated per-vertex color (used for solid and gradient
 * triangles).
 */

#version 450

// Input from vertex shader
layout(location = 0) in vec4 fragColor;

// Output color
layout(location = 0) out vec4 outColor;

void main() {
    outColor = fragColor;
}
