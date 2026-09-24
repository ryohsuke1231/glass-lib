// glass_surface.glsl — tone adjustment and output dithering.

// Screen-static noise of +-0.5/255, added just before the result is quantised
// to 8 bits. The backdrop behind the glass is blurred, so it is almost always
// a shallow gradient, and a shallow gradient in 8 bits bands into visible
// steps; one LSB of noise turns them into dither the eye integrates away.
float ditherLSB(vec2 p) {
    return (fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453) - 0.5) / 255.0;
}

// Saturation, contrast, brightness.
vec3 applySCB(vec3 color, float b, float c, float s) {
    // Brightness: a plain multiply.
    color *= b;

    // Contrast: distance from mid grey, scaled.
    color = mix(vec3(0.5), color, c);

    // Saturation: blend with the Rec. 601 luma.
    float luma = dot(color, vec3(0.299, 0.587, 0.114));
    color = mix(vec3(luma), color, s);

    return max(color, 0.0);
}
