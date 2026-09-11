/*
 * Part of Astonia Client (c) Daniel Brockhaus. Please read license.txt.
 *
 * Glow Fragment Shader (GLSL 450)
 *
 * Radial falloff around a capsule (see glow.vert). This replaces the
 * hand-rolled falloffs the CPU effect code has always used - the
 * concentric alpha rings of sdl_pretty_pixel()/sdl_rain_pixel() and the
 * nine offset lines with a brightness ramp in render_display_strike() -
 * with a real one, so the same effects come out round and smooth at any
 * radius instead of stepped and capped at 3 device pixels.
 *
 * Two lobes: a wide quadratic halo plus an optional tight core (f^8),
 * which is what gives sparkles a hot centre without widening the halo.
 *
 * Output is NOT premultiplied; the pipeline blends SRC_ALPHA/ONE
 * (classic particle additive, same as sdl_set_blend_mode's ADD).
 */

#version 450

layout(location = 0) in vec2 fragPos;
layout(location = 1) flat in vec4 fragSeg;
layout(location = 2) flat in vec2 fragShape;
layout(location = 3) flat in vec4 fragColor;

layout(location = 0) out vec4 outColor;

/* Shortest distance from p to the segment [a,b]. Degenerates to
 * length(p - a) when the segment has zero length (the point case). */
float dist_to_segment(vec2 p, vec2 a, vec2 b)
{
    vec2 ab = b - a;
    float len2 = dot(ab, ab);
    if (len2 < 1e-6) {
        return length(p - a);
    }
    float t = clamp(dot(p - a, ab) / len2, 0.0, 1.0);
    return length(p - (a + t * ab));
}

void main()
{
    float radius = fragShape.x;
    float core = fragShape.y;

    float d = dist_to_segment(fragPos, fragSeg.xy, fragSeg.zw);
    float f = clamp(1.0 - d / max(radius, 0.001), 0.0, 1.0);

    /* smoothstep, not a raw square: the square collapses towards zero as
     * soon as you leave the centre, which at the few-pixel radii these
     * effects use left nothing visible. smoothstep holds a plateau near
     * the middle and still reaches zero cleanly at the rim. */
    float f2 = f * f;
    float halo = f2 * (3.0 - 2.0 * f);
    float hot = f2 * f2 * f2 * f2; /* f^8 - tight centre */

    float intensity = halo + core * hot;
    if (intensity <= 0.0) {
        discard;
    }

    outColor = vec4(fragColor.rgb, fragColor.a * intensity);
}
