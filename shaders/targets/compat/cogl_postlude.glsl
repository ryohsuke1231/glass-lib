// Wrapper main() for the reference shader (see cogl_prelude.glsl).
//
// Supersampling. glass.frag evaluates its lighting and its lens once per
// pixel, and several of those terms change faster than a pixel near the rim:
// the Fresnel rim is a line about a pixel wide, and the lens folds the
// backdrop over a few pixels. On a small capsule (an in-app button, radius
// ~20 px) that single sample steps visibly along the curve. Averaging four
// rotated-grid samples of the SAME shader removes the steps without changing
// what is drawn: each sample is the reference maths, only the sample position
// moves by a fraction of a pixel.

#undef main

uniform vec2 u_pixel_uv;      // one output pixel, in the units of cogl_tex_coord_in
uniform float u_supersample;  // 1 = one sample (the extension's look, pixel for pixel), 4 = RGSS

void main()
{
    if (u_supersample < 1.5)
    {
        cogl_tex_coord_in[0] = v_tex_coord;
        glass_reference_main();
        glass_frag_color = cogl_color_out;
        return;
    }

    // Rotated-grid pattern (the same offsets glass.frag uses for its own
    // backdrop taps): no two samples share a row or a column.
    const vec2 offsets[4] = vec2[4](vec2( 0.375, -0.125), vec2( 0.125,  0.375),
                                    vec2(-0.375,  0.125), vec2(-0.125, -0.375));
    vec4 sum = vec4(0.0);

    for (int i = 0; i < 4; i++)
    {
        cogl_tex_coord_in[0] = v_tex_coord + vec4(offsets[i] * u_pixel_uv, 0.0, 0.0);
        glass_reference_main();
        sum += cogl_color_out;   // premultiplied, so a plain average is correct
    }

    glass_frag_color = sum * 0.25;
}
