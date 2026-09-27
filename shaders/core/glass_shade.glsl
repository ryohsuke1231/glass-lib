// glass_shade.glsl — one evaluation of the glass at a surface UV.
//
// Returns the PREMULTIPLIED colour of the glass, its drop shadow and nothing
// else: out = src.rgb + dst.rgb * (1 - src.a) composites it over whatever is
// behind. The reference's main(), minus the extension-only paths.

vec4 glass_shade(vec2 uv) {
    vec2 resolution = vec2(resolution_x, resolution_y);
    vec2 pixel_coord = uv * resolution;

    // Geometry antialiasing width.
    float edgeFeather = max(edge_smoothing, 0.75);

    vec2 box_center = vec2(glass_rect.x + glass_rect.z * 0.5, glass_rect.y + glass_rect.w * 0.5);
    vec2 local_pos = pixel_coord - box_center;
    vec2 box_size = max(vec2(glass_rect.z, glass_rect.w) * 0.5, vec2(1.0));

    // corner_radius itself with glass_corner_mode 0 (the reference).
    float outline_radius = outlineRadius(local_pos);
    float d = sdRoundRect(local_pos, box_size, outline_radius);

    // Fused shapes (glass-lib's GlassGroup): the smooth union of several
    // rounded rectangles instead of glass_rect. Off (0) in the reference.
    bool fused = glass_shape_count > 0.5;
    float fusedRadius = corner_radius;
    if (fused) {
        d = fusedSD(pixel_coord, fusedRadius);
    }

    // The bevel: a fixed band along the edge (EDGE_LENS_BAND), and the factor
    // the whole lens is scaled down by on a glass too small to hold it.
    float lensBand = fused ? lensBandFor(fusedRadius)
                           : lensBandFor(min(box_size.x, box_size.y));
    float lensScale = lensScaleFor(lensBand);

    // Inside = 1, outside = 0, over +-edgeFeather. Written as the complement
    // of an increasing smoothstep: smoothstep() with edge0 > edge1 is
    // undefined, and some drivers evaluate it as branches that misclassify
    // everything far outside as inside (a flat tint over the whole surface).
    float outsideTransition = smoothstep(-edgeFeather, edgeFeather, d);
    float insideMask = 1.0 - outsideTransition;
    float outsideMask = outsideTransition;

    // ── Early exit 1: beyond the shadow's reach ──────────────────────────
    // alpha, shadow and colour are all exactly 0 past maxRadius.
    float shadowReach = max(shadow_max_radius, 5.0);
    if (early_exit_enabled > 0.5 && debug_view < 0.5 && d >= max(shadowReach, edgeFeather)) {
        return vec4(0.0);
    }

    // ── Early exit 2: the flat interior ──────────────────────────────────
    // Deep enough inside that the height has reached its plateau in a whole
    // gradient_step neighbourhood: the normal is (0, 0, 1), the refraction is
    // 0, and every edge term (rim, AO, shadow) vanishes. What is left is
    // constant: specular and sheen at N = (0, 0, 1). The threshold clears all
    // of those terms at once (the SDF is 1-Lipschitz, so a step of e moves d
    // by at most e).
    float smoothZoneEarly = max(edge_smoothing, 1.0);
    float interiorThreshold = max(
        max(lensBand + gradient_step + smoothZoneEarly,
            edgeFeather * 4.0),
        max(ao_radius, rim_width));
    if (early_exit_enabled > 0.5 && debug_view < 0.5 && !fused && -d >= interiorThreshold) {
        vec2 uvFlat = stabilizedUV(uv, uv);

        // The full path's four-tap pattern at its interior spread (0.75 px).
        vec2 texelFlat = vec2(0.75) / resolution;
        vec3 flatRgb = (
            GLASS_SAMPLE(blurUV(uvFlat + vec2( 0.375, -0.125) * texelFlat, resolution)).rgb +
            GLASS_SAMPLE(blurUV(uvFlat + vec2( 0.125,  0.375) * texelFlat, resolution)).rgb +
            GLASS_SAMPLE(blurUV(uvFlat + vec2(-0.375,  0.125) * texelFlat, resolution)).rgb +
            GLASS_SAMPLE(blurUV(uvFlat + vec2(-0.125, -0.375) * texelFlat, resolution)).rgb
        ) * 0.25;

        flatRgb = applySCB(flatRgb, brightness, contrast, saturation);
        flatRgb = mix(flatRgb, vec3(tint_r, tint_g, tint_b), tint_strength);

        // Specular and sheen at N = (0, 0, 1): dot(reflect(-L, N), V) and the
        // sheen's facing term are both L.z; the specular mask reduces to 0.65.
        vec3 lightDirFlat = normalize(vec3(cos(radians(light_angle_deg)),
                                           sin(radians(light_angle_deg)), 0.38));
        float facing = max(lightDirFlat.z, 0.0);
        float specFlat = pow(facing, max(shininess, 1.0)) * specular_intensity * 0.65;
        float sheenFlat = pow(facing, 1.65) * sheen_intensity;
        vec3 addedFlat = vec3(specFlat + sheenFlat) * surface_light_enabled;

        // Screen blend, then the same overflow normalisation as the full path.
        vec3 litFlat = flatRgb + addedFlat - (flatRgb * addedFlat);
        float maxChannelFlat = max(litFlat.r, max(litFlat.g, litFlat.b));
        if (maxChannelFlat > 1.0) {
            litFlat /= maxChannelFlat;
        }
        litFlat = max(litFlat, 0.0);

        litFlat = max(litFlat + ditherLSB(pixel_coord), 0.0);

        return vec4(litFlat, 1.0);
    }

    // ── Drop shadow ──────────────────────────────────────────────────────
    // A tight dark umbra at the edge, a wider soft penumbra, slightly longer
    // on the side away from the light, tinted cool rather than black.

    float lightAngleRad = radians(light_angle_deg);
    vec2 lightDir2D = vec2(cos(lightAngleRad), -sin(lightAngleRad));
    vec2 shadowDir   = -lightDir2D;

    vec2 outwardDir = fused ? fusedDir(pixel_coord) : normalize(local_pos + vec2(1e-4));
    float lightAlignment = max(dot(outwardDir, shadowDir), 0.0);

    // 85% on the lit side, 100% on the shadow side.
    float dirRadius    = 0.85 + lightAlignment * 0.15;
    float dirIntensity = 0.85 + lightAlignment * 0.15;

    float maxRadius = max(shadow_max_radius, 5.0);
    float effectiveRadius    = min(shadow_radius * dirRadius, maxRadius);

    // shadow_radius = 0 really means off: the umbra's divisor below is
    // floored, which would otherwise keep a thin dark band at the edge.
    float radiusEnable = smoothstep(0.0, 0.75, shadow_radius);
    float effectiveIntensity = shadow_intensity * dirIntensity * radiusEnable;

    // Avoids 0/0 (NaN on the boundary itself) when the radius collapses.
    float safeRadius = max(effectiveRadius, 0.001);

    // Umbra: linear over 0.4 * radius.
    float umbra_t = clamp(d / max(safeRadius * 0.40, 0.5), 0.0, 1.0);
    float umbra  = (1.0 - umbra_t) * 0.80;

    // Penumbra: quintic smootherstep, so value, slope and curvature all reach
    // 0 at the outer radius - no visible ring where it meets "no shadow".
    float penumbra_t = clamp(d / safeRadius, 0.0, 1.0);
    float penumbraFade = 1.0 - penumbra_t;
    float penumbraEase = penumbraFade * penumbraFade * penumbraFade *
        (penumbraFade * (penumbraFade * 6.0 - 15.0) + 10.0);
    float penumbra = penumbraEase * 0.55;

    float shadowAlpha = clamp(
        (umbra + penumbra) * outsideMask * effectiveIntensity,
        0.0, 1.0
    );

    // Only engages in the last 15% before maxRadius, so it cannot bend the
    // penumbra; guarantees no hard clip at the edge of the shadow's room.
    float boundsFade = 1.0 - smoothstep(maxRadius * 0.85, maxRadius, d);
    float boundsMask = boundsFade * boundsFade * boundsFade *
        (boundsFade * (boundsFade * 6.0 - 15.0) + 10.0);
    shadowAlpha *= boundsMask;

    // Hard cutoff independent of any smoothstep() implementation.
    shadowAlpha *= 1.0 - step(maxRadius, d);

    // [DEBUG] Masks, fully opaque: RED = shadow, GREEN = shape. Mode 1 lifts
    // faint values with a gamma so "no shadow at all" stays unambiguous.
    if (debug_view > 0.5 && debug_view < 2.5) {
        float dbgShadow = (debug_view < 1.5) ? pow(shadowAlpha, 0.35) : shadowAlpha;
        float dbgInside = (debug_view < 1.5) ? pow(insideMask, 0.35) : insideMask;
        return vec4(dbgShadow, dbgInside, 0.0, 1.0);
    }

    vec3 shadowColor = vec3(0.03, 0.04, 0.08);

    // ── Early exit 3: outside the silhouette (glass-lib) ─────────────────
    // Past the feather the coverage is exactly 0: everything below is
    // multiplied by it, and what is left is the shadow. The same result as
    // the full path, bit for bit, without its texture fetches - and the
    // shadow's room around a small capsule is larger than the capsule.
    if (early_exit_enabled > 0.5 && debug_view < 0.5 && insideMask <= 0.0) {
        vec3 shadowRgb = shadowColor * shadowAlpha;
        return vec4(max(shadowRgb + ditherLSB(pixel_coord) * shadowAlpha, 0.0), shadowAlpha);
    }

    vec2 gradH = fused ? heightGradientFused(pixel_coord, max_z)
                       : heightGradient2(local_pos, box_size, outline_radius, lensBand, max_z * lensScale);
    vec3 normal = getNormal(gradH);

    vec2 disp = getDisplacement(d, normal, resolution);

    // ── Edge lensing ─────────────────────────────────────────────────────
    // A weight that GROWS towards the rim, so the displacement grows towards
    // the rim too and the bend has one direction across the whole bevel:
    //     D(u) = D_raw(u) * (1 - u/bevel)^falloff
    // (pulling D back to 0 at the edge would make the outermost pixels bend
    // the other way). The mapping may fold - that is what a thick glass edge
    // does - which costs sampling density, paid for by the footprint taps.
    float bevelPx = lensBand;
    float depthPx = max(-d, 0.0);
    float edgeT = clamp(1.0 - depthPx / bevelPx, 0.0, 1.0);

    float lensShape = pow(edgeT, EDGE_LENS_FALLOFF);

    vec2 dispPx = disp * resolution * lensShape * lensScale;
    float dispLenPx = length(dispPx);
    vec2 dispDirPx = dispPx / max(dispLenPx, 1.0e-4);
    float lensReach = EDGE_LENS_REACH * lens_px_scale;
    if (dispLenPx > lensReach) {
        dispLenPx = lensReach;
        dispPx = dispDirPx * lensReach;
    }

    // Inward, always.
    disp = dispPx / max(resolution, vec2(1.0));

    // [DEBUG] The lens displacement: 0.5 grey = none, R/G = x/y, full scale
    // at the lens reach.
    if (debug_view > 2.5) {
        vec2 v = 0.5 + 0.5 * dispPx / lensReach;
        return vec4(vec3(v, 0.5) * insideMask, 1.0);
    }

    // How far the sample point travels per screen pixel - this pixel's
    // footprint in the source - from the weight's own derivative plus the raw
    // field's (spread over roughly the bevel), with 2x headroom for the raw
    // term's steepness at the rim. Analytic: no dFdx, which the GLES2 path
    // Cogl can take does not guarantee.
    float shapeRate = EDGE_LENS_FALLOFF * pow(edgeT, EDGE_LENS_FALLOFF - 1.0) / bevelPx;
    float footprintPx = 2.0 * dispLenPx * (shapeRate + 1.0 / bevelPx);
    vec2 footprintExt = (footprintPx > 2.0 && edge_taps_enabled > 0.5)
        ? dispDirPx * (min(footprintPx, 64.0 * lens_px_scale) * 0.5) / resolution
        : vec2(0.0);

    // Measured footprint (lens_footprint_px > 0, glass-lib). Two things
    // alias near the rim with a handful of samples per pixel:
    //  - just inside it the dome turns from steep to flat within a pixel or
    //    two and the field collapses: one pixel sweeps up to ~50 px of
    //    source, five to fifteen times the estimate above, and unevenly;
    //  - at the edge itself the refraction stops dead (getDisplacement() is
    //    0 outside): the colour jumps from the backdrop tens of px inside to
    //    the backdrop right here, in the middle of the feathered silhouette.
    // Either way neighbouring samples land on unrelated spots - jaggies over
    // a sharp backdrop. So each sample follows the lens across its own
    // footprint, +-lens_footprint_px along the lens direction: the part
    // inside the edge is averaged along the path its landing point traces
    // (four segments), the part outside is the plain backdrop, each weighted
    // by its share of the footprint.
    bool usePath = false;
    vec2 path0 = vec2(0.0);
    vec2 path1 = vec2(0.0);
    vec2 path2 = vec2(0.0);
    vec2 path3 = vec2(0.0);
    vec2 path4 = vec2(0.0);
    float pathInside = 1.0;
    vec2 outsideUv = uv;
    float texelPx = (blur_rect_w >= 1.0 && blur_tex_w >= 1.0)
        ? max(blur_rect_w / blur_tex_w, 0.5) : 1.0;
    float fh = lens_footprint_px;
    // Deeper in, only where the lens moves the sample at all: the
    // displacement only grows towards the rim, so where it is under half a
    // pixel here it is under a pixel across the whole footprint.
    if (fh > 0.0 && edge_taps_enabled > 0.5 && d < fh - 0.01 &&
        (dispLenPx > 0.5 || d > -fh)) {
        vec2 dirOut = fused ? fusedDir(pixel_coord) : sdRoundRectDir(local_pos, box_size, outline_radius);
        // The inside part, as offsets along dirOut: from the inner end of
        // the footprint to its outer end or to just short of the edge.
        float tA = -fh;
        float tB = min(fh, -d - 0.01);
        // Landing points relative to this sample's own, px.
        vec2 landA = tA * dirOut + lensDisplacementPx(local_pos + tA * dirOut, box_size, box_center, resolution) - dispPx;
        vec2 landB = tB * dirOut + lensDisplacementPx(local_pos + tB * dirOut, box_size, box_center, resolution) - dispPx;

        pathInside = (tB - tA) / (2.0 * fh);
        if (pathInside < 0.999 || length(landA) + length(landB) > LENS_PATH_TAP_TEXELS * texelPx) {
            float t1 = mix(tA, tB, 0.25);
            float t2 = mix(tA, tB, 0.5);
            float t3 = mix(tA, tB, 0.75);

            usePath = true;
            path0 = landA / resolution;
            path1 = (t1 * dirOut + lensDisplacementPx(local_pos + t1 * dirOut, box_size, box_center, resolution) - dispPx) / resolution;
            path2 = (t2 * dirOut + lensDisplacementPx(local_pos + t2 * dirOut, box_size, box_center, resolution) - dispPx) / resolution;
            path3 = (t3 * dirOut + lensDisplacementPx(local_pos + t3 * dirOut, box_size, box_center, resolution) - dispPx) / resolution;
            path4 = landB / resolution;
            // The middle of the outside part, unrefracted.
            outsideUv = uv + dirOut * (0.5 * (tB + fh)) / resolution;
        }
    }
    vec2 tapExt = (lens_footprint_px > 0.0) ? vec2(0.0) : footprintExt;

    vec2 refractedUv = stabilizedUV(uv + disp, uv);

    vec2 chromaDir = length(disp) > 0.00001 ? normalize(disp) : vec2(0.0);

    // Chromatic aberration, chroma_strength px, scaled with the lens.
    vec2 chromaVec = chromaDir * (chroma_strength / resolution) * lensShape;
    vec2 uvG = refractedUv;

    // Below a hundredth of a pixel the three channels resolve to the same
    // texels, and the 12-fetch path is pure waste.
    vec2 chromaPx = chromaVec * resolution;
    bool chromaActive = dot(chromaPx, chromaPx) > 1.0e-4;

    // Rotated-grid taps, spread wider near the edge.
    float edgeProximity = 1.0 - smoothstep(0.0, edgeFeather * 4.0, -d);
    float aa_spread = mix(0.75, 2.5, edgeProximity);
    vec2 texel = vec2(aa_spread) / resolution;

    vec3 refractedRgb;
    if (usePath) {
        if (chromaActive) {
            vec2 uvR = stabilizedUV(refractedUv + chromaVec, refractedUv);
            vec2 uvB = stabilizedUV(refractedUv - chromaVec, refractedUv);

            refractedRgb = sampleBackdropPathRGB(uvR, uvG, uvB, path0, path1, path2, path3, path4,
                                                 texel, resolution, texelPx);
        } else {
            refractedRgb = sampleBackdropPath(uvG, path0, path1, path2, path3, path4, texel, resolution, texelPx);
        }
        if (pathInside < 0.999) {
            refractedRgb = mix(sampleBackdrop(outsideUv, vec2(0.0), texel, resolution),
                               refractedRgb, pathInside);
        }
    } else if (chromaActive) {
        vec2 uvR = stabilizedUV(refractedUv + chromaVec, refractedUv);
        vec2 uvB = stabilizedUV(refractedUv - chromaVec, refractedUv);

        refractedRgb = vec3(
            sampleBackdrop(uvR, tapExt, texel, resolution).r,
            sampleBackdrop(uvG, tapExt, texel, resolution).g,
            sampleBackdrop(uvB, tapExt, texel, resolution).b
        );
    } else {
        refractedRgb = sampleBackdrop(uvG, tapExt, texel, resolution);
    }

    vec3 refracted = applySCB(refractedRgb, brightness, contrast, saturation);

    vec3 baseColor = mix(refracted, vec3(tint_r, tint_g, tint_b), tint_strength);

    // NOT multiplied by insideMask here: the final composite multiplies by it
    // exactly once (premultiplied alpha). Doing it twice under-premultiplies
    // the antialiased edge band and draws a dark ring at the boundary.

    // Inner shadow: dark band just inside the edge, 1 at d = 0 fading to 0 at
    // ao_radius px inward. Applied below, where the rim light is known.
    float aoMask = 1.0 - smoothstep(0.0, max(ao_radius, 0.001), -d);

    vec3 lightDir = normalize(vec3(cos(lightAngleRad), sin(lightAngleRad), 0.38));
    vec3 viewDir = vec3(0.0, 0.0, 1.0);
    vec3 reflectDir = reflect(-lightDir, normal);
    float response = 1.0;

    // Rim band. smoothstep() with edge0 == edge1 is undefined too, so the
    // width is floored inside it and "rim_width = 0 means off" is enforced by
    // a separate step() on the unclamped value.
    float safeRimWidth = max(rim_width, 0.001);
    float edgeBand = (1.0 - smoothstep(0.0, safeRimWidth, abs(d))) * step(0.0005, rim_width);

    float rimDot = 1.0 - max(dot(normal, viewDir), 0.0);
    float rimFresnel = pow(max(rimDot, 0.0), max(rim_power, 0.001));
    float lightMask = pow(abs(dot(normal, lightDir)), max(rim_directional_power, 1.0));

    // The inner shadow falls where the rim light does not. On macOS 27 the
    // outermost ring of the glass is bright where the edge faces the light
    // axis and dark (about 0.4x the backdrop) where it runs along it, and
    // neither shows where the other does (docs/memo.md 追記16). With the
    // surface light off (application windows) there is no rim light to make
    // room for, and the shadow runs all round as before.
    float aoLight = mix(1.0, 1.0 - lightMask, surface_light_enabled);
    baseColor *= (1.0 - aoMask * ao_intensity * aoLight);

    // Fresnel mixed with the band keeps the light strictly on the bevel.
    float rimShape = mix(pow(edgeBand, 0.85), rimFresnel, 0.55) * edgeBand;
    float finalRimLight = rimShape * lightMask * rim_intensity * rim_light_color_intensity;
    finalRimLight *= response;

    float specularDot = max(dot(reflectDir, viewDir), 0.0);
    float specularLight = pow(specularDot, max(shininess, 1.0));
    specularLight *= specular_intensity * response;
    float specMask = mix(0.25, 1.0, insideMask) * clamp(edgeBand + insideMask * 0.65, 0.0, 1.0);
    specularLight *= specMask;

    float idleRim = edgeBand * 0.008;

    float sheenFacing = max(dot(normal, lightDir), 0.0);
    float surfaceSheen = pow(sheenFacing, 1.65);
    surfaceSheen *= mix(1.0, 0.55, edgeBand);
    vec3 sheenColor = vec3(1.0) * surfaceSheen * sheen_intensity;

    float alpha = insideMask;

    // All additive light, gated as one group.
    vec3 addedLight = (vec3(specularLight + finalRimLight + idleRim) + sheenColor) * surface_light_enabled;

    // Screen blend (A + B - AB): saturates smoothly instead of blowing out.
    vec3 litColor = baseColor + addedLight - (baseColor * addedLight);

    // Hue-preserving clamp: scale all channels down together.
    float maxChannel = max(litColor.r, max(litColor.g, litColor.b));
    if (maxChannel > 1.0) {
        litColor /= maxChannel;
    }
    litColor = max(litColor, 0.0);

    // Glass OVER shadow, premultiplied:
    //   rgb = A.rgb*A.a + B.rgb*B.a*(1 - A.a),  a = A.a + B.a*(1 - A.a)
    // At zero coverage this is exactly 0 - no tinted rectangle around the glass.
    float shadowContribution = shadowAlpha * (1.0 - alpha);
    vec3 finalRgb   = litColor * alpha + shadowColor * shadowContribution;
    float finalAlpha = alpha + shadowContribution;

    // Dither scaled by the coverage: finalRgb is premultiplied.
    finalRgb = max(finalRgb + ditherLSB(pixel_coord) * finalAlpha, 0.0);

    return vec4(finalRgb, finalAlpha);
}
