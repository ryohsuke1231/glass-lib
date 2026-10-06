// glass_material.glsl — glass-lib's iOS 27 material (design.md §10.3.2):
// the body and the outline of SwiftUI's glassEffect(.regular) on iOS 27, as
// liquid_glass_widgets measured them against the native control
// (LiquidGlassSettings.ios27Light / ios27Dark, 2026-09) and ported here.
//
// Every term is off at 0, and at 0 glass_shade() runs the reference's
// arithmetic unchanged (tests/golden). None of the values is a setting: the
// materials carry them (spec/params.json "surfaces", "outlines").
//
// Lengths are in logical px times lens_px_scale. liquid_glass_widgets gives
// them in physical px at 3x, hence the thirds.

// The frost's cloud: the backdrop blurred far more than GLASS_SAMPLE's (a
// sigma of about 14 pt), on the same sub-rectangle and texel grid, so
// blurUV() maps into both. The target defines it next to GLASS_SAMPLE.
#ifndef GLASS_SAMPLE_FROST
#define GLASS_SAMPLE_FROST(uv) GLASS_SAMPLE(uv)
#endif

// 0 = the reference's body (SCB, then the tint mixed in); 1 = iOS 27's: the
// frost, the tint, then the saturation.
uniform float glass_body_mode;
// The cloud laid over the lightly blurred copy of the content (0..1); 0 =
// no frost.
uniform float frost_opacity;
// How far the copy may stray from the cloud on one side: > 0 keeps it from
// going darker than the cloud by more than this, < 0 from going lighter.
uniform float frost_clamp;
// The outline: a half-point line on the very edge (its strength, and how
// much of it is left at the ends of the light axis), two highlight lobes at
// those ends, and a darkening across the bevel.
uniform float rim_shade;
uniform float rim_shade_ends;
uniform float rim_light;
uniform float edge_absorption;

// ITU-R BT.709 luma, as the measurements use (applySCB() uses Rec. 601).
const vec3 GLASS_LUMA_709 = vec3(0.2126, 0.7152, 0.0722);

// The bevel the outline and the absorption follow: a quarter circle 32 px
// deep at 3x. Not the lens's (EDGE_LENS_BAND): liquid_glass_widgets fits
// its lens with a different profile over this depth; the lens here is
// glass-lib's own, measured on macOS 27 to the same displacement.
#define IOS27_BEVEL (32.0 / 3.0)

bool ios27Outline() {
    return rim_shade > 0.0 || rim_light > 0.0 || edge_absorption > 0.0;
}

// How far in from the edge the outline reaches, px (0 when it is off): the
// flat-interior early exit must stay clear of it.
float ios27Reach() {
    return ios27Outline() ? IOS27_BEVEL * lens_px_scale : 0.0;
}

// The direction the light travels across the glass, y down: from the top at
// light_angle_deg = 90. The lobe on the far side (the bottom) is the lit one.
vec2 ios27LightTravel() {
    float a = radians(light_angle_deg);
    return vec2(-cos(a), sin(a));
}

// How much of the half-point line this depth is (px inside the edge; 0 or
// less outside it, within the antialiased silhouette).
float ios27Hairline(float depthPx) {
    if (rim_shade <= 0.0)
        return 0.0;
    return 1.0 - smoothstep(0.25 * lens_px_scale, (2.0 / 3.0) * lens_px_scale, depthPx);
}

// The line's colour: the backdrop just outside the glass, unrefracted, and
// averaged along the edge (three taps half a point apart), so it stays a
// line over a busy backdrop.
vec3 ios27HairlineColor(vec2 uv, vec2 dirOut, vec2 resolution) {
    vec2 outward = dirOut * ((2.5 / 3.0) * lens_px_scale);
    vec2 along = vec2(-dirOut.y, dirOut.x) * (0.5 * lens_px_scale);
    return (GLASS_SAMPLE(blurUV(uv + outward / resolution, resolution)).rgb +
            GLASS_SAMPLE(blurUV(uv + (outward + along) / resolution, resolution)).rgb +
            GLASS_SAMPLE(blurUV(uv + (outward - along) / resolution, resolution)).rgb) / 3.0;
}

vec3 ios27Saturate(vec3 c, float s) {
    float luma = dot(c, GLASS_LUMA_709);
    return clamp(mix(vec3(luma), c, s), 0.0, 1.0);
}

// The tint of iOS 27 glass: an achromatic one (white, grey) is mixed in; a
// coloured one shifts the hue and keeps the backdrop's luma.
vec3 ios27Tint(vec3 c, vec3 tint, float a) {
    vec3 direct = mix(c, tint, a);
    vec3 shifted = clamp(tint + (dot(c, GLASS_LUMA_709) - dot(tint, GLASS_LUMA_709)), 0.0, 1.0);
    float chroma = max(max(tint.r, tint.g), tint.b) - min(min(tint.r, tint.g), tint.b);
    return mix(direct, mix(c, shifted, a), clamp(chroma * 8.0, 0.0, 1.0));
}

// The body at one sample: `ghost` is the lightly blurred copy of the content
// the lens brings here (GLASS_SAMPLE, refracted), the cloud is read where
// the sample itself lies. Within the outline the content just outside
// (`hair`) takes over, untinted and unsaturated.
vec3 ios27Body(vec3 ghost, vec2 uv, vec2 resolution, vec3 hair, float hairline) {
    vec3 tint = vec3(tint_r, tint_g, tint_b);
    vec3 body = ghost;

    if (frost_opacity > 0.0) {
        vec3 cloud = GLASS_SAMPLE_FROST(blurUV(uv, resolution)).rgb;
        float k = frost_clamp;
        vec3 held = k > 0.0 ? max(ghost, cloud - k)
                  : k < 0.0 ? min(ghost, cloud - k)
                  : ghost;
        body = mix(held, cloud, clamp(frost_opacity, 0.0, 1.0));
    }
    body = mix(body, hair, hairline);

    vec3 c = ios27Tint(body, tint, tint_strength * (1.0 - hairline));
    // The measured saturation holds with this in place: +20% over black,
    // -20% over white.
    float adaptive = mix(1.2, 0.8, dot(body, GLASS_LUMA_709));
    c = ios27Saturate(c, mix(saturation * adaptive, 1.0, hairline));
    return mix(c, tint, tint_strength * 0.12 * (adaptive - 1.0) * (1.0 - hairline));
}

// Light lost crossing the thicker glass at the rim (a factor on the body):
// less where the rim faces the light's far side, more on the near one.
// `along` is dot(outward direction, ios27LightTravel()).
float ios27Absorption(float depthPx, float along) {
    if (edge_absorption <= 0.0)
        return 1.0;
    float u = clamp(depthPx / (IOS27_BEVEL * lens_px_scale), 0.0, 1.0);
    float height = sqrt(max(0.0, 1.0 - (1.0 - u) * (1.0 - u)));
    float r = 1.0 - height;
    float rimThickness = 1.0 - sqrt(max(0.0, 1.0 - r * r));
    float dirScale = mix(1.4, 0.6, along * 0.5 + 0.5);
    return max(0.0, 1.0 - sqrt(rimThickness) * edge_absorption * dirScale);
}

// The two highlight lobes at the ends of the light axis, the far (lit) one
// a little stronger: a sharp core just inside the line and a soft tail a
// few points into the body, added in sRGB and easing off as the body
// brightens (+50/255 over black, about +27/255 over near white). With
// highlight_backdrop_color the lobes keep their lightness but take the
// body's hue (backdropHighlight()), like the reference's rim light.
vec3 ios27RimLight(vec3 c, float depthPx, float along, float hairline) {
    float s3 = lens_px_scale / 3.0;
    if (rim_light <= 0.0 || depthPx >= 30.0 * s3)
        return c;
    float lit = max(along, 0.0);
    float opp = max(-along, 0.0);
    float lobe = lit * lit * lit + 0.85 * opp * opp * opp;
    float inset = max(0.0, depthPx - 2.25 * s3) / s3;
    float core = exp(-inset / 1.5);
    float tail = exp(-inset / 8.0);
    float ease = pow(1.0 - clamp(dot(c, GLASS_LUMA_709), 0.0, 1.0), 0.3);
    float light = rim_light * lobe * ease * (0.175 * core + 0.035 * tail) * (1.0 - hairline);
    vec3 lifted = clamp(c + vec3(light), 0.0, 1.0);
    if (highlight_backdrop_color > 0.5 && light > 0.0)
        lifted = backdropHighlight(c, lifted, light);
    return lifted;
}

// The line itself: a fixed step below whatever is under it (subtracted, not
// blended), 0.314 across the light axis and rim_shade_ends of that at its
// ends, falling off as a power of the angle.
vec3 ios27Shade(vec3 c, float along, float hairline) {
    if (hairline <= 0.0)
        return c;
    float across = pow(max(1.0 - along * along, 0.0), 1.75);
    float shade = rim_shade * 0.314 * mix(rim_shade_ends, 1.0, across);
    return max(c - vec3(shade * hairline), 0.0);
}
