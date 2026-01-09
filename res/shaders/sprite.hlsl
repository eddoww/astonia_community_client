/*
 * Part of Astonia Client (c) Daniel Brockhaus. Please read license.txt.
 *
 * Sprite Rendering Shader
 *
 * This shader handles GPU-accelerated sprite rendering with effects:
 * - Directional lighting (5-way: middle, left, right, up, down)
 * - Colorization (channel-based color replacement)
 * - Color balance (RGB adjustment, lightness, saturation)
 * - Freeze effect (ice-blue tint)
 * - Shine effect (specular highlights)
 * - Alpha blending
 *
 * Uses instanced rendering for sprite batching.
 */

// ============================================================================
// Constants
// ============================================================================

// Effect flags (matches C code in sdl_private.h and render.c)
#define EFFECT_FLAG_COLORIZE     (1 << 0)
#define EFFECT_FLAG_COLOR_BALANCE (1 << 1)
#define EFFECT_FLAG_FREEZE       (1 << 2)
#define EFFECT_FLAG_SHINE        (1 << 3)
#define EFFECT_FLAG_LIGHTING     (1 << 4)
#define EFFECT_FLAG_SINK         (1 << 5)

// ============================================================================
// Input Structures
// ============================================================================

// Vertex shader input (per-vertex data for quad)
struct VSInput {
    float2 position : POSITION;      // Quad corner (0-1)
    float2 texcoord : TEXCOORD0;     // UV coordinates
    uint instanceID : SV_InstanceID; // Instance index
};

// Per-instance sprite data (structured buffer)
struct SpriteInstance {
    // Position and size
    float4 destRect;      // x, y, width, height in screen pixels
    float4 srcRect;       // UV coordinates (u, v, uWidth, vHeight)

    // Effect parameters (packed for efficiency)
    float4 colorMod;      // RGBA color modulation
    float4 effectParams;  // x=light (0-16), y=freeze (0-7), z=shine (0-100), w=alpha (0-255)

    // Directional lighting (5-way)
    float4 dirLight;      // x=ml (middle), y=ll (left), z=rl (right), w=ul (up)
    float  dlLight;       // dl (down) - separate due to alignment

    // Colorization parameters
    float3 colorize;      // c1, c2, c3 color channels (0-255 scaled to 0-1)

    // Color balance
    float4 colorBalance;  // x=cr, y=cg, z=cb, w=light

    // Additional parameters
    float  saturation;    // Saturation adjustment (0-20 scaled)
    uint   flags;         // Effect flags
    float2 padding;       // Alignment padding
};

// Vertex shader output / Fragment shader input
struct PSInput {
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
    nointerpolation uint instanceID : TEXCOORD1;
    float2 localPos : TEXCOORD2;     // Position within sprite (0-1)
};

// ============================================================================
// Resources
// ============================================================================

// Frame-level uniforms
cbuffer FrameUniforms : register(b0) {
    float2 screenSize;    // Viewport dimensions
    float2 invScreenSize; // 1.0 / screenSize for pixel->NDC conversion
};

// Instance buffer (all sprites for this batch)
StructuredBuffer<SpriteInstance> instances : register(t0);

// Sprite texture (atlas or individual)
Texture2D spriteTexture : register(t1);
SamplerState spriteSampler : register(s0);

// ============================================================================
// Vertex Shader
// ============================================================================

PSInput VSMain(VSInput input) {
    PSInput output;

    SpriteInstance inst = instances[input.instanceID];

    // Calculate screen-space position
    float2 screenPos = inst.destRect.xy + input.position * inst.destRect.zw;

    // Convert to NDC (-1 to 1)
    float2 ndcPos = (screenPos * invScreenSize) * 2.0f - 1.0f;
    ndcPos.y = -ndcPos.y;  // Flip Y for screen coordinates

    output.position = float4(ndcPos, 0.0f, 1.0f);

    // Calculate texture coordinates
    output.texcoord = inst.srcRect.xy + input.texcoord * inst.srcRect.zw;

    // Pass through instance ID and local position
    output.instanceID = input.instanceID;
    output.localPos = input.position;

    return output;
}

// ============================================================================
// Effect Functions
// ============================================================================

// Apply directional lighting (5-way blend)
// Matches CPU implementation in sdl_image.c:992-1101
float3 applyDirectionalLighting(float3 color, float2 localPos, SpriteInstance inst) {
    float ml = inst.dirLight.x;  // Middle light
    float ll = inst.dirLight.y;  // Left light
    float rl = inst.dirLight.z;  // Right light
    float ul = inst.dirLight.w;  // Up light
    float dl = inst.dlLight;     // Down light

    // Calculate base light level
    float baseLight = inst.effectParams.x / 16.0f;  // Normalize 0-16 to 0-1

    // Use ml as primary if no directional variation
    if (ml == ll && ml == rl && ml == ul && ml == dl) {
        // Simple uniform lighting
        return color * baseLight * (ml / 15.0f + 0.5f);
    }

    // Calculate position-based weights for 5-way blend
    // Based on isometric tile geometry
    float2 pos = localPos;

    // Weight calculations (simplified from CPU code)
    float midWeight = 0.4f;
    float leftWeight = (1.0f - pos.x) * 0.3f;
    float rightWeight = pos.x * 0.3f;
    float upWeight = (1.0f - pos.y) * 0.3f * 0.5f;
    float downWeight = pos.y * 0.3f * 0.5f;

    // Normalize weights
    float totalWeight = midWeight + leftWeight + rightWeight + upWeight + downWeight;
    midWeight /= totalWeight;
    leftWeight /= totalWeight;
    rightWeight /= totalWeight;
    upWeight /= totalWeight;
    downWeight /= totalWeight;

    // Blend light levels (normalize from 0-15 range)
    float blendedLight = (ml * midWeight + ll * leftWeight + rl * rightWeight +
                          ul * upWeight + dl * downWeight) / 15.0f;

    // Apply lighting
    return color * baseLight * (blendedLight + 0.2f);
}

// Apply colorization (channel-based color replacement)
// Matches CPU implementation in sdl_effects.c:116-297
float3 applyColorization(float3 color, float3 colorize) {
    // Detect dominant channel
    float maxChannel = max(max(color.r, color.g), color.b);

    if (maxChannel < 0.1f) {
        return color;  // Too dark, skip colorization
    }

    // Threshold-based channel selection (matches CPU GREENCOL, BLUECOL, REDCOL)
    float greenThreshold = 0.70f;
    float blueThreshold = 0.70f;
    float redThreshold = 0.40f;

    float3 result = color;

    // Check for color replacement based on dominant channel
    float redRatio = color.r / maxChannel;
    float greenRatio = color.g / maxChannel;
    float blueRatio = color.b / maxChannel;

    // Colorize if strongly one channel (simple approach)
    if (greenRatio > greenThreshold && colorize.y > 0.0f) {
        // Replace green with colorize color 2
        float intensity = color.g;
        result = lerp(result, float3(colorize.y, colorize.y * 0.8f, colorize.y * 0.6f), greenRatio);
    }
    else if (blueRatio > blueThreshold && colorize.z > 0.0f) {
        // Replace blue with colorize color 3
        float intensity = color.b;
        result = lerp(result, float3(colorize.z * 0.6f, colorize.z * 0.8f, colorize.z), blueRatio);
    }
    else if (redRatio > redThreshold && colorize.x > 0.0f) {
        // Replace red with colorize color 1
        float intensity = color.r;
        result = lerp(result, float3(colorize.x, colorize.x * 0.7f, colorize.x * 0.5f), redRatio);
    }

    return result;
}

// Apply color balance adjustment
// Matches CPU implementation in sdl_effects.c:299-377
float3 applyColorBalance(float3 color, float4 balance, float sat) {
    float cr = balance.x / 128.0f;  // -128 to 128 normalized
    float cg = balance.y / 128.0f;
    float cb = balance.z / 128.0f;
    float lightness = balance.w / 128.0f;

    // Apply color balance with cross-channel mixing
    float3 result;
    result.r = color.r + cr * 0.5f + lightness;
    result.g = color.g + cg * 0.5f + lightness;
    result.b = color.b + cb * 0.5f + lightness;

    // Apply saturation (0-20 range, 10 = normal)
    float satNorm = sat / 10.0f;
    float lum = dot(result, float3(0.299f, 0.587f, 0.114f));
    result = lerp(float3(lum, lum, lum), result, satNorm);

    return saturate(result);
}

// Apply freeze effect (ice-blue tint)
// Matches CPU implementation in sdl_effects.c:71-85
float3 applyFreeze(float3 color, float freeze) {
    // freeze is 0-7, normalize to 0-1
    float f = freeze / 7.0f;

    // Add ice-blue tint
    float3 iceColor = float3(0.7f, 0.85f, 1.0f);
    return lerp(color, iceColor * (color.r + color.g + color.b) / 3.0f, f);
}

// Apply shine effect (specular highlights)
// Matches CPU implementation in sdl_effects.c:87-114
float3 applyShine(float3 color, float shine) {
    // shine is 0-100
    if (shine <= 0.0f) return color;

    // Calculate per-channel shine using quartic curve
    float3 normalized = color / 127.5f;  // Normalize to 0-2 range (127.5 is midpoint for 0-255)
    float3 power = normalized * normalized * normalized * normalized;  // x^4

    float shineNorm = shine / 100.0f;
    return lerp(color, power, shineNorm);
}

// ============================================================================
// Fragment Shader
// ============================================================================

float4 PSMain(PSInput input) : SV_Target {
    SpriteInstance inst = instances[input.instanceID];

    // Sample sprite texture
    float4 texColor = spriteTexture.Sample(spriteSampler, input.texcoord);

    // Early out for fully transparent pixels
    if (texColor.a < 0.01f) {
        discard;
    }

    float3 color = texColor.rgb;
    uint flags = inst.flags;

    // ========================================================================
    // Apply Effects (in same order as CPU for visual parity)
    // ========================================================================

    // 1. Colorization
    if (flags & EFFECT_FLAG_COLORIZE) {
        color = applyColorization(color, inst.colorize);
    }

    // 2. Color balance
    if (flags & EFFECT_FLAG_COLOR_BALANCE) {
        color = applyColorBalance(color, inst.colorBalance, inst.saturation);
    }

    // 3. Shine effect
    if (flags & EFFECT_FLAG_SHINE) {
        color = applyShine(color, inst.effectParams.z);
    }

    // 4. Directional lighting
    if (flags & EFFECT_FLAG_LIGHTING) {
        color = applyDirectionalLighting(color, input.localPos, inst);
    }

    // 5. Freeze effect
    if (flags & EFFECT_FLAG_FREEZE) {
        color = applyFreeze(color, inst.effectParams.y);
    }

    // Apply color modulation
    color *= inst.colorMod.rgb;

    // Calculate final alpha
    float alpha = texColor.a * (inst.effectParams.w / 255.0f) * inst.colorMod.a;

    return float4(color, alpha);
}

// ============================================================================
// Simple Sprite Shader (no effects, for performance)
// ============================================================================

float4 PSSimple(PSInput input) : SV_Target {
    SpriteInstance inst = instances[input.instanceID];

    float4 texColor = spriteTexture.Sample(spriteSampler, input.texcoord);

    if (texColor.a < 0.01f) {
        discard;
    }

    // Just apply color modulation and alpha
    float3 color = texColor.rgb * inst.colorMod.rgb;
    float alpha = texColor.a * (inst.effectParams.w / 255.0f) * inst.colorMod.a;

    return float4(color, alpha);
}
