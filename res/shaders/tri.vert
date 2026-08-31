/*
 * Part of Astonia Client (c) Daniel Brockhaus. Please read license.txt.
 *
 * Triangle Rendering Vertex Shader (GLSL 450)
 *
 * Renders a single triangle with per-vertex colors from uniform data.
 * The vertex buffer only carries the vertex index (0, 1 or 2 in x);
 * positions and colors come from the uniform block, so shapes are drawn
 * with one uniform push + draw and no per-frame buffer uploads.
 */

#version 450

// Vertex input (x = vertex index: 0.0, 1.0 or 2.0)
layout(location = 0) in vec2 inPosition;

// Per-triangle data (set 1 = vertex uniform buffers in SDL3 GPU)
layout(set = 1, binding = 0, std140) uniform TriData {
    vec4 p01;       // v0.x, v0.y, v1.x, v1.y in screen pixels
    vec4 p2screen;  // v2.x, v2.y, screenWidth, screenHeight
    vec4 color0;    // RGBA of vertex 0
    vec4 color1;    // RGBA of vertex 1
    vec4 color2;    // RGBA of vertex 2
} tri;

// Output to fragment shader
layout(location = 0) out vec4 fragColor;

void main() {
    int idx = int(inPosition.x + 0.5);

    vec2 screenPos = (idx == 0) ? tri.p01.xy : ((idx == 1) ? tri.p01.zw : tri.p2screen.xy);
    vec4 color = (idx == 0) ? tri.color0 : ((idx == 1) ? tri.color1 : tri.color2);

    // Convert to NDC (-1 to 1)
    vec2 ndcPos = (screenPos / tri.p2screen.zw) * 2.0 - 1.0;
    ndcPos.y = -ndcPos.y;  // Flip Y for screen coordinates

    gl_Position = vec4(ndcPos, 0.0, 1.0);
    fragColor = color;
}
