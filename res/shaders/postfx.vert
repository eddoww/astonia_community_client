/*
 * Part of Astonia Client (c) Daniel Brockhaus. Please read license.txt.
 *
 * Post-Processing Vertex Shader (GLSL 450)
 */

#version 450

// Vertex input
layout(location = 0) in vec2 inPosition;   // NDC quad corners (-1 to 1)
layout(location = 1) in vec2 inTexCoord;   // UV coordinates (0 to 1)

// Output to fragment shader
layout(location = 0) out vec2 fragTexCoord;

void main() {
    gl_Position = vec4(inPosition, 0.0, 1.0);
    fragTexCoord = inTexCoord;
}
