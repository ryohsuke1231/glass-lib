// glass_optics.glsl — refraction and backdrop sampling.
//
// The target defines GLASS_SAMPLE(uv), returning the blurred backdrop at a
// texture coordinate of the blurred texture (a vec4).

// UV displacement caused by refraction through the surface normal (Snell,
// air -> glass). displacement_scale is in px on both axes, so the same value
// means the same displacement on every surface.
vec2 getDisplacement(float d, vec3 normal, vec2 resolution) {
    if (d > 0.0)
        return vec2(0.0);

    vec3 viewDir = vec3(0.0, 0.0, -1.0);
    float eta = 1.0 / max(ior, 1.001);
    vec3 refractedRay = refract(viewDir, normal, eta);

    // Total internal reflection returns (0, 0, 0).
    if (length(refractedRay) < 0.0001)
        return vec2(0.0);

    // Keeps the division below from exploding where the surface is nearly
    // vertical.
    float safe_z = max(-refractedRay.z, 0.15);

    vec2 displacement = (refractedRay.xy / safe_z) *
                        (displacement_scale / max(resolution, vec2(1.0)));

    // The cap, in px. The reference took 0.30 of the FBO's shorter side here.
    float max_disp_px = max_displacement_px;
    vec2 dispPx = displacement * resolution;
    float dispLenPx = length(dispPx);
    if (dispLenPx > max_disp_px) {
        displacement *= max_disp_px / dispLenPx;
    }

    return displacement;
}

// The bevel's lens displacement at a point, in px: the refraction weighted by
// the lens ramp and capped at the lens reach. Mirrors the main path of
// glass_shade() step for step; used only to measure the footprint
// (lens_footprint_px > 0), so the reference path does not go through it.
// box_center turns local_pos back into surface px for fused shapes.
vec2 lensDisplacementPx(vec2 local_pos, vec2 box_size, vec2 box_center, vec2 resolution) {
    float d;
    vec3 normal;
    float bevelPx;

    if (glass_shape_count > 0.5) {
        float r;
        vec2 p = local_pos + box_center;
        d = fusedSD(p, r);
        normal = getNormal(heightGradientFused(p, max_z));
        bevelPx = max(r, 1.0);
    } else {
        float rOut = outlineRadius(local_pos);
        d = sdRoundRect(local_pos, box_size, rOut);
        normal = getNormal(heightGradient2(local_pos, box_size, rOut, corner_radius, max_z));
        bevelPx = max(min(corner_radius, min(box_size.x, box_size.y)), 1.0);
    }
    vec2 disp = getDisplacement(d, normal, resolution);

    float edgeT = clamp(1.0 - max(-d, 0.0) / bevelPx, 0.0, 1.0);
    vec2 dispPx = disp * resolution * pow(edgeT, EDGE_LENS_FALLOFF);

    float dispLenPx = length(dispPx);
    float lensReach = EDGE_LENS_REACH * lens_px_scale;
    if (dispLenPx > lensReach) {
        dispPx = dispPx / max(dispLenPx, 1.0e-4) * lensReach;
    }
    return dispPx;
}

// Keeps a sampling coordinate inside the surface. With edge_damping on (the
// reference behaviour) the refraction is also faded out within ~3% of the
// surface edge, where bilinear filtering would pull in the void beyond it.
vec2 stabilizedUV(vec2 candidate, vec2 fallback) {
    vec2 clamped = clamp(candidate, vec2(0.001), vec2(0.999));
    if (edge_damping < 0.5)
        return clamped;
    float edgeDist = min(min(candidate.x, candidate.y), min(1.0 - candidate.x, 1.0 - candidate.y));
    float keep = smoothstep(-0.04, 0.03, edgeDist);
    return mix(fallback, clamped, keep);
}

// Maps a surface UV (0..1 across `resolution`) into the blurred sub-rect's
// own UV, clamped 1.2 texels inside it.
vec2 blurUV(vec2 fullUV, vec2 resolution) {
    vec2 uv;
    vec2 size;
    if (blur_rect_w < 1.0 || blur_rect_h < 1.0) {
        vec2 mFull = vec2(1.2) / max(resolution, vec2(1.0));
        uv = clamp(fullUV, mFull, vec2(1.0) - mFull);
        size = max(resolution, vec2(1.0));
    } else {
        size = vec2(blur_rect_w, blur_rect_h);
        vec2 m = vec2(1.2) / size;
        uv = clamp((fullUV * resolution - vec2(blur_rect_x, blur_rect_y)) / size,
                   m, vec2(1.0) - m);
    }

    // Smooth-bilinear reconstruction of a magnified texture. Plain bilinear
    // is continuous in value but not in slope at every texel centre, which
    // reads as a staircase along diagonals and faint creases in gradients.
    // Warping the coordinate inside its texel by a smoothstep before the
    // hardware interpolates gives a C1 (cubic-like) kernel for 4 ALU ops.
    // Only where the texture is actually magnified: at 1:1 the warp would
    // move samples off their texel centres for no benefit.
    vec2 texSize = vec2(blur_tex_w, blur_tex_h);
    if (texSize.x >= 2.0 && texSize.y >= 2.0 &&
        min(size.x / texSize.x, size.y / texSize.y) > 1.05) {
        vec2 t = uv * texSize - 0.5;
        vec2 f = fract(t);
        f = f * f * (3.0 - 2.0 * f);
        uv = (floor(t) + 0.5 + f) / texSize;
    }
    return uv;
}

// Averages the blurred backdrop over the area of the source this pixel
// covers. Near the rim the refraction is a strong MINIFICATION: a few pixels
// have to represent tens of source pixels. Point-sampling that sparkles, so
// `ext` (half the footprint, as a UV vector along the compression) spreads
// six more taps across it. vec2(0) = the plain four-tap rotated grid.
vec3 sampleBackdrop(vec2 uvc, vec2 ext, vec2 texel, vec2 resolution) {
    vec2 o1 = vec2( 0.375, -0.125) * texel;
    vec2 o2 = vec2( 0.125,  0.375) * texel;
    vec2 o3 = vec2(-0.375,  0.125) * texel;
    vec2 o4 = vec2(-0.125, -0.375) * texel;

    vec3 sum =
        GLASS_SAMPLE(blurUV(uvc + o1, resolution)).rgb +
        GLASS_SAMPLE(blurUV(uvc + o2, resolution)).rgb +
        GLASS_SAMPLE(blurUV(uvc + o3, resolution)).rgb +
        GLASS_SAMPLE(blurUV(uvc + o4, resolution)).rgb;

    if (dot(ext, ext) <= 0.0)
        return sum * 0.25;

    // Uneven spacing weights the centre a little more than a box filter,
    // which keeps the refraction from looking smeared.
    sum +=
        GLASS_SAMPLE(blurUV(uvc + ext * 0.90 + o1, resolution)).rgb +
        GLASS_SAMPLE(blurUV(uvc + ext * 0.55 + o2, resolution)).rgb +
        GLASS_SAMPLE(blurUV(uvc + ext * 0.22 + o3, resolution)).rgb +
        GLASS_SAMPLE(blurUV(uvc - ext * 0.22 + o4, resolution)).rgb +
        GLASS_SAMPLE(blurUV(uvc - ext * 0.55 + o1, resolution)).rgb +
        GLASS_SAMPLE(blurUV(uvc - ext * 0.90 + o2, resolution)).rgb;

    return sum * 0.1;
}

// Averages the blurred backdrop along the path the lens traces across one
// sample's footprint: four straight segments p0-p1-p2-p3-p4 (UV offsets from
// uvc) covering equal parts of the footprint, so weighted equally, each
// sampled LENS_PATH_TAP_TEXELS blurred texels apart (1..LENS_SEGMENT_MAX_TAPS
// taps). Every tap also takes the next of the four rotated-grid offsets, so a
// path shorter than the spacing is exactly the plain four-tap grid.
vec3 sampleBackdropPath(vec2 uvc, vec2 p0, vec2 p1, vec2 p2, vec2 p3, vec2 p4,
                        vec2 texel, vec2 resolution, float texelPx) {
    vec2 o = vec2(0.375, -0.125);
    vec3 sum = vec3(0.0);

    for (int k = 0; k < 4; k++) {
        vec2 a = (k == 0) ? p0 : ((k == 1) ? p1 : ((k == 2) ? p2 : p3));
        vec2 b = (k == 0) ? p1 : ((k == 1) ? p2 : ((k == 2) ? p3 : p4));
        float n = clamp(ceil(length((b - a) * resolution) / (LENS_PATH_TAP_TEXELS * texelPx)),
                        1.0, float(LENS_SEGMENT_MAX_TAPS));
        vec3 seg = vec3(0.0);

        for (int i = 0; i < LENS_SEGMENT_MAX_TAPS; i++) {
            if (float(i) >= n)
                break;
            seg += GLASS_SAMPLE(blurUV(uvc + mix(a, b, (float(i) + 0.5) / n) + o * texel,
                                       resolution)).rgb;
            o = vec2(-o.y, o.x);
        }
        sum += seg / n;
    }
    return sum * 0.25;
}

// sampleBackdropPath() with chromatic aberration: the red, green and blue
// channels of three paths that differ only in where they start (uvR, uvG,
// uvB). Channel for channel the same sums as three calls, taken in one walk:
// the path, its tap counts and offsets are worked out once instead of three
// times (docs/memo.md 追記7).
vec3 sampleBackdropPathRGB(vec2 uvR, vec2 uvG, vec2 uvB,
                           vec2 p0, vec2 p1, vec2 p2, vec2 p3, vec2 p4,
                           vec2 texel, vec2 resolution, float texelPx) {
    vec2 o = vec2(0.375, -0.125);
    vec3 sum = vec3(0.0);

    for (int k = 0; k < 4; k++) {
        vec2 a = (k == 0) ? p0 : ((k == 1) ? p1 : ((k == 2) ? p2 : p3));
        vec2 b = (k == 0) ? p1 : ((k == 1) ? p2 : ((k == 2) ? p3 : p4));
        float n = clamp(ceil(length((b - a) * resolution) / (LENS_PATH_TAP_TEXELS * texelPx)),
                        1.0, float(LENS_SEGMENT_MAX_TAPS));
        vec3 seg = vec3(0.0);

        for (int i = 0; i < LENS_SEGMENT_MAX_TAPS; i++) {
            if (float(i) >= n)
                break;
            vec2 step = mix(a, b, (float(i) + 0.5) / n) + o * texel;
            seg.r += GLASS_SAMPLE(blurUV(uvR + step, resolution)).r;
            seg.g += GLASS_SAMPLE(blurUV(uvG + step, resolution)).g;
            seg.b += GLASS_SAMPLE(blurUV(uvB + step, resolution)).b;
            o = vec2(-o.y, o.x);
        }
        sum += seg / n;
    }
    return sum * 0.25;
}
