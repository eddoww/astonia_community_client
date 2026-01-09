/*
 * Part of Astonia Client (c) Daniel Brockhaus. Please read license.txt.
 *
 * Post-Processing Effects Shader
 *
 * This shader handles screen-space post-processing effects:
 * - Vignette (radial edge darkening)
 * - Screen tint (color overlay)
 * - Future: bloom, color grading, etc.
 */

// Vertex shader input
struct VSInput {
    float2 position : POSITION;   // NDC quad corners (-1 to 1)
    float2 texcoord : TEXCOORD0;  // UV coordinates (0 to 1)
};

// Vertex shader output / Fragment shader input
struct PSInput {
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

// Post-processing parameters (uniform buffer)
cbuffer PostFXParams : register(b0) {
    float2 screenSize;          // Screen dimensions in pixels
    float vignetteIntensity;    // Vignette strength (0 = off, 1 = max)
    float vignetteRadius;       // Vignette inner radius (0.5 = half screen)
    float3 tintColor;           // Screen tint RGB (0-1)
    float tintIntensity;        // Tint strength (0 = off, 1 = full)
    float brightness;           // Brightness adjustment (-1 to 1)
    float contrast;             // Contrast adjustment (0.5 to 2.0)
    float saturation;           // Saturation adjustment (0 = grayscale, 1 = normal, 2 = vivid)
    float padding;              // Padding for 16-byte alignment
};

// Scene texture (rendered game)
Texture2D sceneTexture : register(t0);
SamplerState sceneSampler : register(s0);

// ============================================================================
// Vertex Shader
// ============================================================================

PSInput VSMain(VSInput input) {
    PSInput output;
    output.position = float4(input.position, 0.0f, 1.0f);
    output.texcoord = input.texcoord;
    return output;
}

// ============================================================================
// Helper Functions
// ============================================================================

// Convert RGB to luminance (perceived brightness)
float luminance(float3 color) {
    return dot(color, float3(0.2126f, 0.7152f, 0.0722f));
}

// Apply saturation adjustment
float3 adjustSaturation(float3 color, float sat) {
    float lum = luminance(color);
    return lerp(float3(lum, lum, lum), color, sat);
}

// Apply contrast adjustment (centered on 0.5)
float3 adjustContrast(float3 color, float con) {
    return (color - 0.5f) * con + 0.5f;
}

// ============================================================================
// Fragment Shader
// ============================================================================

float4 PSMain(PSInput input) : SV_Target {
    // Sample scene texture
    float4 color = sceneTexture.Sample(sceneSampler, input.texcoord);

    // ========================================================================
    // Vignette Effect
    // ========================================================================
    // Darken edges of screen with smooth radial falloff
    if (vignetteIntensity > 0.0f) {
        // Calculate distance from center (0,0 at center, 0.5 at edges)
        float2 center = input.texcoord - 0.5f;

        // Account for aspect ratio (prevent elliptical vignette)
        float aspect = screenSize.x / screenSize.y;
        center.x *= aspect;

        // Distance from center (normalized)
        float dist = length(center);

        // Smooth falloff from inner radius to edge
        float innerRadius = vignetteRadius;
        float outerRadius = 0.707f * aspect;  // ~1.414/2 adjusted for aspect
        float vignette = smoothstep(innerRadius, outerRadius, dist);

        // Apply vignette (darkening)
        color.rgb *= 1.0f - (vignette * vignetteIntensity);
    }

    // ========================================================================
    // Screen Tint
    // ========================================================================
    // Overlay a color on the scene (for damage flash, environmental effects)
    if (tintIntensity > 0.0f) {
        color.rgb = lerp(color.rgb, tintColor, tintIntensity);
    }

    // ========================================================================
    // Color Adjustments (optional)
    // ========================================================================
    // Brightness
    if (brightness != 0.0f) {
        color.rgb += brightness;
    }

    // Contrast
    if (contrast != 1.0f) {
        color.rgb = adjustContrast(color.rgb, contrast);
    }

    // Saturation
    if (saturation != 1.0f) {
        color.rgb = adjustSaturation(color.rgb, saturation);
    }

    // Clamp final color to valid range
    color.rgb = saturate(color.rgb);

    return color;
}

// ============================================================================
// Simplified Vignette-Only Shader (for performance when only vignette is used)
// ============================================================================

float4 PSVignetteOnly(PSInput input) : SV_Target {
    float4 color = sceneTexture.Sample(sceneSampler, input.texcoord);

    // Fast vignette calculation
    float2 center = input.texcoord - 0.5f;
    float dist = dot(center, center);  // Squared distance (faster, no sqrt)
    float vignette = dist * vignetteIntensity * 2.0f;

    color.rgb *= saturate(1.0f - vignette);

    return color;
}
