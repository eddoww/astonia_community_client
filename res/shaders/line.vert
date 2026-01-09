/*
 * Part of Astonia Client (c) Daniel Brockhaus. Please read license.txt.
 *
 * Line Rendering Vertex Shader (GLSL 450)
 *
 * Simple shader for rendering colored lines using uniform data.
 */

#version 450

// Vertex input (0.0 or 1.0 for line interpolation)
layout(location = 0) in vec2 inPosition;

// Per-line data (set 1 = vertex uniform buffers in SDL3 GPU)
layout(set = 1, binding = 0, std140) uniform LineData {
    vec4 startEnd;      // start.x, start.y, end.x, end.y in screen pixels
    vec4 color;         // RGBA color
    vec2 screenSize;    // Screen dimensions for NDC conversion
    vec2 padding;
} line;

// Output to fragment shader
layout(location = 0) out vec4 fragColor;

void main() {
    // Interpolate between start and end based on vertex position (x = 0 or 1)
    vec2 screenPos = mix(line.startEnd.xy, line.startEnd.zw, inPosition.x);

    // Convert to NDC (-1 to 1)
    vec2 ndcPos = (screenPos / line.screenSize) * 2.0 - 1.0;
    ndcPos.y = -ndcPos.y;  // Flip Y for screen coordinates

    gl_Position = vec4(ndcPos, 0.0, 1.0);
    fragColor = line.color;
}
