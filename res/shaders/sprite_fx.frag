/*
 * Part of Astonia Client (c) Daniel Brockhaus. Please read license.txt.
 *
 * Sprite Effect Fragment Shader (GLSL 450)
 *
 * EXACT port of the CPU sprite effect pipeline:
 *   - sdl_effects.c: sdl_light/light_calc (lightmulti table incl. the
 *     GO_LIGHTER knobs), sdl_freeze, sdl_shine_pix, sdl_colorize_pix,
 *     sdl_colorize_pix2 (incl. the neighbour test), sdl_colorbalance
 *     (incl. the original client's cg-double-scaling typo - see
 *     the provenance note in the client CLAUDE.md).
 *   - sdl_image.c sdl_make bake loop: effect order, the 5-way
 *     directional lighting tile geometry, sink.
 *
 * The CPU implementation is the spec. Where the CPU uses doubles the
 * shader uses float32 (tolerance <= 2 LSB, verified by
 * tests/test_shaderfx_compare.c); where the CPU uses integer math the
 * shader uses integer math and is bit-exact.
 *
 * Known deliberate divergence: sdl_shine_pix can go negative for
 * shine > 100 (only reachable via multi-channel embedded colorize
 * shine) and then wraps through the IRGBA cast on the CPU; the shader
 * clamps to 0 instead. The client only routes such sprites here when
 * the checker proves them, see gpu_shaderfx_eligible().
 */

#version 450

layout(location = 0) in vec2 fragTexel;
layout(location = 1) flat in uint fragInstance;

layout(location = 0) out vec4 outColor;

struct FxInstance {
    vec4 dest;
    ivec4 src;
    ivec4 org_sz;
    uvec4 colorize;
    ivec4 balance;
    ivec4 fx;
    ivec4 light_a;
    ivec4 light_b;
};

/* fragment resources: sampler set 2 binding 0, storage buffer set 2
 * binding 1 (SDL GPU binding convention: samplers first, then storage) */
layout(set = 2, binding = 0) uniform sampler2D pageTexture;

layout(std430, set = 2, binding = 1) readonly buffer InstanceBuffer {
    FxInstance instances[];
};

layout(std140, set = 3, binding = 0) uniform PSFrame {
    int sdlScale;
    int leBonus;   // +8 when GO_LIGHTER, +12 more when GO_LIGHTER2
    uint psBaseInstance; // unused (index arrives via fragInstance)
    int psPad0;
};

/* ------------------------------------------------------------------ */
/* constants (sdl_effects.c)                                           */
/* ------------------------------------------------------------------ */

const float REDCOL = 0.40;
const float GREENCOL = 0.70;
const float BLUECOL = 0.70;

/* original dd.c light curve ("newlight" mode) */
const int lightmulti[16] = int[16](0, 2, 4, 8, 16, 18, 20, 22, 24, 26, 27, 28, 29, 30, 31, 32);

/* 5-bit channel extraction from the 15-bit colorize values */
float oget_r(uint c) { return float((c >> 10) & 0x1Fu); }
float oget_g(uint c) { return float((c >> 5) & 0x1Fu); }
float oget_b(uint c) { return float(c & 0x1Fu); }

/* ------------------------------------------------------------------ */
/* pixel fetch (base texture holds the raw sprite pixels)              */
/* ------------------------------------------------------------------ */

ivec4 fetch_texel(ivec2 pageCoord)
{
    vec4 c = texelFetch(pageTexture, pageCoord, 0);
    return ivec4(round(c * 255.0));
}

/* ------------------------------------------------------------------ */
/* sdl_light / light_calc (integer-exact)                              */
/* ------------------------------------------------------------------ */

int light_calc(int val, int light)
{
    light = clamp(light, 0, 15);
    int le = light + leBonus;
    return (val * (lightmulti[light] + le)) / (32 + le);
}

ivec3 sdl_light(int light, ivec3 v)
{
    if (light == 0) {
        return min(ivec3(255), v * 2 + 4);
    }
    return ivec3(light_calc(v.r, light), light_calc(v.g, light), light_calc(v.b, light));
}

/* ------------------------------------------------------------------ */
/* sdl_shine_pix (float port of the double math; clamps negatives)     */
/* ------------------------------------------------------------------ */

ivec3 shine_pix(ivec3 v, int shine)
{
    vec3 f = vec3(v) / 127.5;
    vec3 s = (f * f * f * f * float(shine) + f * (100.0 - float(shine))) / 200.0;
    s = clamp(s, 0.0, 1.0);
    return ivec3(s * 255.0); /* truncation toward zero, like (int)(x*255.0) */
}

/* ------------------------------------------------------------------ */
/* sdl_colorize_pix (old algorithm, sprites < 220000)                  */
/* ------------------------------------------------------------------ */

ivec4 colorize_old(ivec4 p, uvec3 cv)
{
    float rf = float(p.r) / 255.0;
    float gf = float(p.g) / 255.0;
    float bf = float(p.b) / 255.0;
    float c1 = 0.0, c2 = 0.0, c3 = 0.0;
    float sh = 0.0;

    float m = max(max(rf, gf), bf) + 0.000001;
    float rm = rf / m, gm = gf / m, bm = bf / m;

    // channel 1: green max
    if (cv.x != 0u && gm > 0.99 && rm < GREENCOL && bm < GREENCOL) {
        c1 = gf - max(bf, rf);
        if ((cv.x & 0x8000u) != 0u) {
            sh += gm - max(rm, bm);
        }
        gf -= c1;
    }

    m = max(max(rf, gf), bf) + 0.000001;
    rm = rf / m; gm = gf / m; bm = bf / m;

    // channel 2: blue max
    if (cv.y != 0u && bm > 0.99 && rm < BLUECOL && gm < BLUECOL) {
        c2 = bf - max(rf, gf);
        if ((cv.y & 0x8000u) != 0u) {
            sh += bm - max(rm, gm);
        }
        bf -= c2;
    }

    m = max(max(rf, gf), bf) + 0.000001;
    rm = rf / m; gm = gf / m; bm = bf / m;

    // channel 3: red max
    if (cv.z != 0u && rm > 0.99 && gm < REDCOL && bm < REDCOL) {
        c3 = rf - max(gf, bf);
        if ((cv.z & 0x8000u) != 0u) {
            sh += rm - max(gm, bm);
        }
        rf -= c3;
    }

    rf = max(0.0, rf);
    gf = max(0.0, gf);
    bf = max(0.0, bf);

    int r = min(255, int(16.0 * (c1 * oget_r(cv.x) + c2 * oget_r(cv.y) + c3 * oget_r(cv.z)) + 8.0 * rf * 31.0));
    int g = min(255, int(16.0 * (c1 * oget_g(cv.x) + c2 * oget_g(cv.y) + c3 * oget_g(cv.z)) + 8.0 * gf * 31.0));
    int b = min(255, int(16.0 * (c1 * oget_b(cv.x) + c2 * oget_b(cv.y) + c3 * oget_b(cv.z)) + 8.0 * bf * 31.0));

    ivec3 rgb = ivec3(r, g, b);
    if (sh > 0.1) {
        rgb = shine_pix(rgb, int(sh * 50.0));
    }
    return ivec4(rgb, p.a);
}

/* ------------------------------------------------------------------ */
/* sdl_colorize_pix2 (sprites >= 220000): neighbour-assisted           */
/* ------------------------------------------------------------------ */

/* would this sprite-relative texel colorize on channel `what`? */
int would_colorize(ivec2 t, ivec2 org, ivec2 sz, int what)
{
    if (t.x < 0 || t.x >= sz.x || t.y < 0 || t.y >= sz.y) {
        return 0;
    }
    ivec4 q = fetch_texel(org + t);
    float rf = float(q.r) / 255.0;
    float gf = float(q.g) / 255.0;
    float bf = float(q.b) / 255.0;
    float m = max(max(rf, gf), bf) + 0.000001;
    float rm = rf / m, gm = gf / m, bm = bf / m;

    if (what == 0 && gm > 0.99 && rm < GREENCOL && bm < GREENCOL) {
        return 1;
    }
    if (what == 1 && bm > 0.99 && rm < BLUECOL && gm < BLUECOL) {
        return 1;
    }
    if (what == 2 && rm > 0.99 && gm < REDCOL && bm < REDCOL) {
        return 1;
    }
    return 0;
}

int would_colorize_neigh(ivec2 t, ivec2 org, ivec2 sz, int what)
{
    int v = would_colorize(t + ivec2(1, 0), org, sz, what) + would_colorize(t + ivec2(-1, 0), org, sz, what) +
            would_colorize(t + ivec2(0, 1), org, sz, what) + would_colorize(t + ivec2(0, -1), org, sz, what);
    if (sdlScale > 2) {
        v += would_colorize(t + ivec2(2, 0), org, sz, what) + would_colorize(t + ivec2(-2, 0), org, sz, what) +
             would_colorize(t + ivec2(0, 2), org, sz, what) + would_colorize(t + ivec2(0, -2), org, sz, what);
    }
    return v;
}

ivec4 colorize_new(ivec4 p, uvec3 cv, ivec2 t, ivec2 org, ivec2 sz)
{
    float rf = float(p.r) / 255.0;
    float gf = float(p.g) / 255.0;
    float bf = float(p.b) / 255.0;
    float m = max(max(rf, gf), bf) + 0.000001;
    float rm = rf / m, gm = gf / m, bm = bf / m;

    // channel 1: green
    if ((cv.x != 0u && gm > 0.99 && rm < GREENCOL && bm < GREENCOL) ||
        (cv.x != 0u && gm > 0.67 && would_colorize_neigh(t, org, sz, 0) != 0)) {
        int r = int(8.0 * (oget_r(cv.x) * gf + (1.0 - gf) * rf));
        int g = int(8.0 * oget_g(cv.x) * gf);
        int b = int(8.0 * (oget_b(cv.x) * gf + (1.0 - gf) * bf));
        return ivec4(r, g, b, p.a);
    }

    // channel 2: blue
    if ((cv.y != 0u && bm > 0.99 && rm < BLUECOL && gm < BLUECOL) ||
        (cv.y != 0u && bm > 0.67 && would_colorize_neigh(t, org, sz, 1) != 0)) {
        int r = int(8.0 * (oget_r(cv.y) * bf + (1.0 - bf) * rf));
        int g = int(8.0 * (oget_g(cv.y) * bf + (1.0 - bf) * gf));
        int b = int(8.0 * oget_b(cv.y) * bf);
        return ivec4(r, g, b, p.a);
    }

    // channel 3: red
    if ((cv.z != 0u && rm > 0.99 && gm < REDCOL && bm < REDCOL) ||
        (cv.z != 0u && rm > 0.67 && would_colorize_neigh(t, org, sz, 2) != 0)) {
        int r = int(8.0 * oget_r(cv.z) * rf);
        int g = int(8.0 * (oget_g(cv.z) * rf + (1.0 - rf) * gf));
        int b = int(8.0 * (oget_b(cv.z) * rf + (1.0 - rf) * bf));
        return ivec4(r, g, b, p.a);
    }

    return p;
}

/* ------------------------------------------------------------------ */
/* sdl_colorbalance (integer-exact, including the cg typo)             */
/* ------------------------------------------------------------------ */

ivec3 colorbalance(ivec3 v, ivec3 cbal, int lightness, int sat)
{
    int r = v.r, g = v.g, b = v.b;

    if (lightness != 0) {
        r += lightness;
        g += lightness;
        b += lightness;
    }

    if (sat != 0) {
        int grey = (r + g + b) / 3;
        r = ((r * (20 - sat)) + (grey * sat)) / 20;
        g = ((g * (20 - sat)) + (grey * sat)) / 20;
        b = ((b * (20 - sat)) + (grey * sat)) / 20;
    }

    /* original scaling INCLUDING the typo: cg scaled twice (0.5625),
     * cb never scaled. (cr*3)/4 == (char)(cr*0.75), (cg*9)/16 ==
     * (char)(cg*0.5625): both truncate toward zero, bit-exact. */
    int cr = (cbal.x * 3) / 4;
    int cg = (cbal.y * 9) / 16;
    int cb = cbal.z;

    r += cr;
    g -= cr / 2;
    b -= cr / 2;
    r -= cg / 2;
    g += cg;
    b -= cg / 2;
    r -= cb / 2;
    g -= cb / 2;
    b += cb;

    if (r < 0) { r = 0; }
    if (g < 0) { g = 0; }
    if (b < 0) { b = 0; }

    if (r > 255) {
        g += (r - 255) / 2;
        b += (r - 255) / 2;
        r = 255;
    }
    if (g > 255) {
        r += (g - 255) / 2;
        b += (g - 255) / 2;
        g = 255;
    }
    if (b > 255) {
        r += (b - 255) / 2;
        g += (b - 255) / 2;
        b = 255;
    }

    return min(ivec3(r, g, b), ivec3(255));
}

/* ------------------------------------------------------------------ */
/* sdl_freeze (integer-exact)                                          */
/* ------------------------------------------------------------------ */

ivec3 freeze_pix(ivec3 v, int fr)
{
    /* 23 == 3*RENDERFX_MAX_FREEZE-1 */
    int add = (255 * fr) / 23;
    int addb = (255 * 3 * fr) / 23;
    return min(ivec3(v.r + add, v.g + add, v.b + addb), ivec3(255));
}

/* ------------------------------------------------------------------ */
/* 5-way directional lighting (sdl_make bake loop, integer-exact)      */
/* ------------------------------------------------------------------ */

ivec4 directional_light(ivec4 p, ivec2 t, ivec4 la, int dl)
{
    int ml = la.x, ll = la.y, rl = la.z, ul = la.w;

    if (ll == ml && rl == ml && ul == ml && dl == ml) {
        return ivec4(sdl_light(ml, p.rgb), p.a);
    }

    int s = sdlScale;
    int x = t.x, y = t.y;
    int v1, v2, v3, v4, v5;
    ivec3 r1 = ivec3(0), r2 = ivec3(0), r3 = ivec3(0), r4 = ivec3(0), r5 = ivec3(0);

    if (y < 10 * s + (20 * s - abs(20 * s - x)) / 2) {
        /* floor tile / top of a wall tile */
        if (x / 2 < 20 * s - y) {
            v2 = -(x / 2 - (20 * s - y));
            r2 = sdl_light(ll, p.rgb);
        } else {
            v2 = 0;
        }
        if (x / 2 > 20 * s - y) {
            v3 = (x / 2 - (20 * s - y));
            r3 = sdl_light(rl, p.rgb);
        } else {
            v3 = 0;
        }
        if (x / 2 > y) {
            v4 = (x / 2 - y);
            r4 = sdl_light(ul, p.rgb);
        } else {
            v4 = 0;
        }
        if (x / 2 < y) {
            v5 = -(x / 2 - y);
            r5 = sdl_light(dl, p.rgb);
        } else {
            v5 = 0;
        }
        v1 = 20 * s - (v2 + v3 + v4 + v5);
        r1 = sdl_light(ml, p.rgb);
    } else {
        /* lower part (left side and front) */
        if (x < 10 * s) {
            v2 = 10 * s - x;
            r2 = sdl_light(ll, p.rgb);
        } else {
            v2 = 0;
        }
        if (x > 10 * s && x < 20 * s) {
            v3 = x - 10 * s;
            r3 = sdl_light(rl, p.rgb);
        } else {
            v3 = 0;
        }
        if (x >= 20 * s && x < 30 * s) {
            v5 = 30 * s - x;
            r5 = sdl_light(dl, p.rgb);
        } else {
            v5 = 0;
        }
        if (x > 30 * s && x < 40 * s) {
            v4 = x - 30 * s;
            r4 = sdl_light(ul, p.rgb);
        } else {
            v4 = 0;
        }
        v1 = 20 * s - v2 - v3 - v4 - v5;
        r1 = sdl_light(ml, p.rgb);
    }

    int div = v1 + v2 + v3 + v4 + v5;
    if (div == 0) {
        return ivec4(0);
    }
    ivec3 rgb = (r1 * v1 + r2 * v2 + r3 * v3 + r4 * v4 + r5 * v5) / div;
    return ivec4(rgb, p.a);
}

/* ------------------------------------------------------------------ */
/* main: CPU bake order (sdl_make, scale==100)                         */
/* ------------------------------------------------------------------ */

void main()
{
    FxInstance inst = instances[fragInstance];

    ivec2 pageCoord = ivec2(floor(fragTexel));
    ivec2 org = inst.org_sz.xy;
    ivec2 sz = inst.org_sz.zw;
    ivec2 t = pageCoord - org; /* sprite-relative texel */

    ivec4 p = fetch_texel(pageCoord);

    /* base pixels with alpha 0 stay untouched by every effect
     * (colorize/balance/shine/light keep alpha; only freeze adds RGB,
     * which is invisible at alpha 0) */
    if (p.a == 0) {
        discard;
    }

    /* Plain-texture mode (GPU_FX_MODE_PLAIN): no effect pipeline, just
     * texel * color-mod - the batched equivalent of the parity path's
     * sprite_simple draw (text glyphs, cached text quads, GUI blits).
     * balance.xyz carries the RGB modulation 0..255 (255 = untinted),
     * light_b.y the alpha modulation, exactly like the direct path's
     * colorMod uniform. */
    if ((inst.colorize.w & 2u) != 0u) {
        vec3 cmod = vec3(inst.balance.xyz) / 255.0;
        float amod = float(inst.light_b.y) / 255.0;
        outColor = vec4((vec3(p.rgb) / 255.0) * cmod, (float(p.a) / 255.0) * amod);
        return;
    }

    /* 1. colorize */
    if ((inst.colorize.x | inst.colorize.y | inst.colorize.z) != 0u) {
        if ((inst.colorize.w & 1u) != 0u) { /* GPU_FX_COLORIZE_NEW */
            p = colorize_new(p, inst.colorize.xyz, t, org, sz);
        } else {
            p = colorize_old(p, inst.colorize.xyz);
        }
    }

    /* 2. color balance */
    if ((inst.balance.x | inst.balance.y | inst.balance.z | inst.balance.w | inst.fx.x) != 0) {
        p.rgb = colorbalance(p.rgb, inst.balance.xyz, inst.balance.w, inst.fx.x);
    }

    /* 3. shine */
    if (inst.fx.y != 0) {
        p.rgb = shine_pix(p.rgb, inst.fx.y);
    }

    /* 4. directional lighting (always applied, like the bake loop) */
    p = directional_light(p, t, inst.light_a, inst.light_b.x);

    /* 5. sink: zero alpha below the cutoff */
    if (inst.fx.w != 0 && t.y > sz.y - inst.fx.w) {
        p.a = 0;
    }

    /* 6. freeze */
    if (inst.fx.z != 0) {
        p.rgb = freeze_pix(p.rgb, inst.fx.z);
    }

    float drawAlpha = float(inst.light_b.y) / 255.0;
    outColor = vec4(vec3(p.rgb) / 255.0, (float(p.a) / 255.0) * drawAlpha);
}
