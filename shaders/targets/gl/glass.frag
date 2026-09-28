// glass.frag (GL target) — glass-lib's glass pass: the shader core plus
// supersampling, for OpenGL ES 3.0 / OpenGL 3.3 core.
//
// The loader prepends the #version line and the precision statements; the
// #include below is expanded at build time.

uniform sampler2D glass_backdrop;   // the blurred backdrop (blur_rect_* / blur_tex_* describe it)
uniform sampler2D glass_frost;      // the frost's cloud, on the same grid (glass_material.glsl)

#define GLASS_SAMPLE(uv) texture(glass_backdrop, (uv))
#define GLASS_SAMPLE_FROST(uv) texture(glass_frost, (uv))

#include "../../core/glass_core.glsl"

in vec4 v_tex_coord;
out vec4 glass_frag_color;

uniform vec2 u_pixel_uv;         // one output pixel, in the units of v_tex_coord
uniform float u_supersample;     // 1 = one sample per pixel, 4 = rotated grid
uniform float u_rim_samples;     // samples per pixel where the lens acts (<= u_supersample: no change)

// Van der Corput radical inverse in base 2 (bitfieldReverse is GLES 3.1).
float radical_inverse(uint k)
{
    k = (k << 16u) | (k >> 16u);
    k = ((k & 0x55555555u) << 1u) | ((k & 0xAAAAAAAAu) >> 1u);
    k = ((k & 0x33333333u) << 2u) | ((k & 0xCCCCCCCCu) >> 2u);
    k = ((k & 0x0F0F0F0Fu) << 4u) | ((k & 0xF0F0F0F0u) >> 4u);
    k = ((k & 0x00FF00FFu) << 8u) | ((k & 0xFF00FF00u) >> 8u);
    return float(k) * 2.3283064365386963e-10;
}

// Supersampling: glass_shade() evaluates the rim light and the lens once per
// call, and both change faster than a pixel near the rim (the Fresnel rim is
// about a pixel wide; the lens folds the backdrop over a few pixels). One
// sample per pixel steps visibly along the curve of a small capsule. Four
// rotated-grid samples of the same function remove the steps without
// changing what is drawn (docs/memo.md 地雷4).
void main()
{
    vec2 resolution = vec2(resolution_x, resolution_y);
    vec2 p = v_tex_coord.st * resolution;
    vec2 half_size = max(glass_rect.zw * 0.5, vec2(1.0));
    vec2 local = p - (glass_rect.xy + half_size);
    float d = sdRoundRect(local, half_size, outlineRadius(local));
    float band = min(lensBandFor(min(half_size.x, half_size.y)), 8.0 * lens_px_scale);
    bool in_lens = d < max(edge_smoothing, 0.75) + 1.0 && -d < band;

    if (in_lens && u_rim_samples > u_supersample)
    {
        // Hammersley points: every sample has its own offset along any axis,
        // so the lens's sweep across the pixel is covered evenly.
        int n = int(u_rim_samples);
        float inv_n = 1.0 / float(n);
        vec4 sum = vec4(0.0);

        for (int i = 0; i < n; i++)
        {
            vec2 o = vec2((float(i) + 0.5) * inv_n, radical_inverse(uint(i)) + 0.5 * inv_n) - 0.5;
            sum += glass_shade(v_tex_coord.st + o * u_pixel_uv);
        }
        glass_frag_color = sum * inv_n;
        return;
    }

    if (u_supersample < 1.5)
    {
        glass_frag_color = glass_shade(v_tex_coord.st);
        return;
    }

    const vec2 offsets[4] = vec2[4](vec2( 0.375, -0.125), vec2( 0.125,  0.375),
                                    vec2(-0.375,  0.125), vec2(-0.125, -0.375));
    vec4 sum = vec4(0.0);

    for (int i = 0; i < 4; i++)
        sum += glass_shade(v_tex_coord.st + offsets[i] * u_pixel_uv);

    // Premultiplied, so a plain average is correct.
    glass_frag_color = sum * 0.25;
}
