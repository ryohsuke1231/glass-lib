// glass_surface.glsl — tone adjustment, highlight colour and output dithering.

// Screen-static noise of +-0.5/255, added just before the result is quantised
// to 8 bits. The backdrop behind the glass is blurred, so it is almost always
// a shallow gradient, and a shallow gradient in 8 bits bands into visible
// steps; one LSB of noise turns them into dither the eye integrates away.
float ditherLSB(vec2 p) {
    return (fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453) - 0.5) / 255.0;
}

// erfc(x) for x >= 0 (Abramowitz and Stegun 7.1.27, error below 5e-4).
float erfcPositive(float x) {
    float p = 1.0 + x * (0.278393 + x * (0.230389 + x * (0.000972 + x * 0.078108)));
    float p2 = p * p;
    return 1.0 / (p2 * p2);
}

// How much more saturated than the backdrop a highlight is drawn.
#define HIGHLIGHT_CHROMA_GAIN 1.25

// sRGB-encoded colour to Oklab. A 2.2 gamma stands in for the sRGB curve,
// which is close enough for tinting a highlight.
vec3 toOklab(vec3 c) {
    vec3 lin = pow(max(c, 0.0), vec3(2.2));
    vec3 lms = vec3(
        dot(lin, vec3(0.4122214708, 0.5363325363, 0.0514459929)),
        dot(lin, vec3(0.2119034982, 0.6806995451, 0.1073969566)),
        dot(lin, vec3(0.0883024619, 0.2817188376, 0.6299787005)));
    lms = pow(max(lms, 0.0), vec3(1.0 / 3.0));
    return vec3(
        dot(lms, vec3(0.2104542553, 0.7936177850, -0.0040720468)),
        dot(lms, vec3(1.9779984951, -2.4285922050, 0.4505937099)),
        dot(lms, vec3(0.0259040371, 0.7827717662, -0.8086757660)));
}

// Oklab to linear RGB, unclamped.
vec3 oklabToLinear(vec3 lab) {
    vec3 lms = vec3(
        dot(lab, vec3(1.0, 0.3963377774, 0.2158037573)),
        dot(lab, vec3(1.0, -0.1055613458, -0.0638541728)),
        dot(lab, vec3(1.0, -0.0894841775, -1.2914855480)));
    lms = lms * lms * lms;
    return vec3(
        dot(lms, vec3(4.0767416621, -3.3077115913, 0.2309699292)),
        dot(lms, vec3(-1.2684380046, 2.6097574011, -0.3413193965)),
        dot(lms, vec3(-0.0041960863, -0.7034186147, 1.7076147010)));
}

// How far from `from` a step of `delta` can go, as a fraction of it, before
// leaving [0, 1].
float gamutRoom(float from, float delta) {
    if (delta > 1.0e-6)
        return (1.0 - from) / delta;
    if (delta < -1.0e-6)
        return -from / delta;
    return 1.0;
}

// `lit` (base lit by `light` of white) with the backdrop's hue put back.
// Lighting with white washes a highlight out to grey; real glass lights up in
// the colour of what is behind it. The lightness stays as it was, so only
// the colour changes. The hue fades in over the first quarter of the light,
// so unlit glass keeps its colour.
//
// A bright saturated colour is often out of gamut. Scaling it back down would
// dim the highlight, so the colour is pulled towards the plain one instead,
// along a line of nearly constant luminance.
vec3 backdropHighlight(vec3 base, vec3 lit, float light) {
    vec3 litLab = toOklab(lit);
    vec2 baseChroma = toOklab(base).yz * HIGHLIGHT_CHROMA_GAIN;
    litLab.yz = mix(litLab.yz, baseChroma, clamp(light * 4.0, 0.0, 1.0));

    vec3 from = pow(max(lit, 0.0), vec3(2.2));
    vec3 delta = oklabToLinear(litLab) - from;
    float t = min(min(gamutRoom(from.r, delta.r), gamutRoom(from.g, delta.g)),
                  min(gamutRoom(from.b, delta.b), 1.0));
    return pow(clamp(from + delta * max(t, 0.0), 0.0, 1.0), vec3(1.0 / 2.2));
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
