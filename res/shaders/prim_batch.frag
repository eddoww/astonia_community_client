/*
 * Part of Astonia Client (c) Daniel Brockhaus. Please read license.txt.
 *
 * Batched Primitive Fragment Shader (GLSL 450)
 *
 * Same as primitive.frag; the colour comes from the instance buffer via
 * the vertex shader rather than from a per-draw uniform.
 */

#version 450

layout(location = 0) in vec4 fragColor;
layout(location = 0) out vec4 outColor;

void main()
{
    outColor = fragColor;
}
