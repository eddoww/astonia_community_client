/*
 * Part of Astonia Client (c) Daniel Brockhaus. Please read license.txt.
 *
 * Post-Processing Fragment Shader (GLSL 450)
 *
 * Effects: Vignette, Screen Tint, Brightness/Contrast/Saturation
 */

#version 450

// Input from vertex shader
layout(location = 0) in vec2 fragTexCoord;

// Output color
layout(location = 0) out vec4 outColor;

// Scene texture (set 2 = fragment samplers in SDL3 GPU)
layout(set = 2, binding = 0) uniform sampler2D sceneTexture;

// Post-processing parameters (set 3 = fragment uniform buffers in SDL3 GPU)
// Must match gpu_postfx_params_t in sdl_gpu_post.h
layout(set = 3, binding = 0, std140) uniform PostFXParams {
    float screenWidth;
    float screenHeight;
    float vignetteIntensity;
    float vignetteRadius;
    float tintR;
    float tintG;
    float tintB;
    float tintIntensity;
    float brightness;
    float contrast;
    float saturation;
    float padding;
};

// Convert RGB to luminance (perceived brightness)
float luminance(vec3 color) {
    return dot(color, vec3(0.2126, 0.7152, 0.0722));
}

// Apply saturation adjustment
vec3 adjustSaturation(vec3 color, float sat) {
    float lum = luminance(color);
    return mix(vec3(lum), color, sat);
}

// Apply contrast adjustment (centered on 0.5)
vec3 adjustContrast(vec3 color, float con) {
    return (color - 0.5) * con + 0.5;
}

void main() {
    // Sample scene texture
    vec4 color = texture(sceneTexture, fragTexCoord);

    // Vignette Effect
    if (vignetteIntensity > 0.0) {
        // Calculate distance from center
        vec2 center = fragTexCoord - 0.5;

        // Account for aspect ratio
        float aspect = screenWidth / screenHeight;
        center.x *= aspect;

        // Distance from center
        float dist = length(center);

        // Smooth falloff
        float innerRadius = vignetteRadius;
        float outerRadius = 0.707 * aspect;
        float vignette = smoothstep(innerRadius, outerRadius, dist);

        // Apply vignette
        color.rgb *= 1.0 - (vignette * vignetteIntensity);
    }

    // Screen Tint
    if (tintIntensity > 0.0) {
        vec3 tintColor = vec3(tintR, tintG, tintB);
        color.rgb = mix(color.rgb, tintColor, tintIntensity);
    }

    // Brightness
    if (brightness != 0.0) {
        color.rgb += brightness;
    }

    // Contrast
    if (contrast != 1.0) {
        color.rgb = adjustContrast(color.rgb, contrast);
    }

    // Saturation
    if (saturation != 1.0) {
        color.rgb = adjustSaturation(color.rgb, saturation);
    }

    // Clamp to valid range
    color.rgb = clamp(color.rgb, 0.0, 1.0);

    outColor = color;
}
