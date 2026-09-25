// glass_shape.glsl — the glass body: its outline (a signed distance field)
// and its surface (a superellipse dome), and the normal of that surface.

// Signed distance to a rounded rectangle centred at the origin with half-size
// b and corner radius r: negative inside, positive outside, 0 on the edge.
float sdRoundRect(vec2 p, vec2 b, float r) {
    vec2 d = abs(p) - b + vec2(r);
    return min(max(d.x, d.y), 0.0) + length(max(d, 0.0)) - r;
}

// Depth below the edge, normalised over the corner radius: the height builds
// up only across that band instead of curving all the way to the centre.
float normalizedDepth(float d, vec2 b, float r) {
    float maxDepth = max(r, 1.0);
    float interiorDepth = max(-d, 0.0);
    return clamp(interiorDepth / maxDepth, 0.0, 1.0);
}

// Superellipse profile: h = H * (1 - (1 - t)^n)^(1/n), t: edge = 0 -> centre = 1.
float profileHeight(float t, float zScale) {
    float n = max(profile_shape_n, 1.01);
    float invT = clamp(1.0 - t, 0.0, 1.0);
    float inner = max(1.0 - pow(invT, n), 0.0);
    float h = pow(inner, 1.0 / n);
    return h * zScale;
}

// Height of the surface at p. The fade over +-edge_smoothing px replaces a
// hard step at d = 0: a discontinuous height field makes the finite
// difference in heightGradient() spike right at the boundary, which shows as
// jagged displacement against busy backgrounds.
float getHeight(vec2 p, vec2 b, float r, float zScale) {
    float d = sdRoundRect(p, b, r);

    float smoothZone = max(edge_smoothing, 1.0);
    if (d > smoothZone)
        return 0.0;

    float t = normalizedDepth(d, b, r);
    float h = profileHeight(t, zScale);

    // smoothstep() needs edge0 < edge1 (undefined otherwise), hence the
    // complement instead of swapped edges.
    float fade = 1.0 - smoothstep(-smoothZone, smoothZone, d);
    return h * fade;
}

// Unit gradient of sdRoundRect() at p ("straight out of the shape"), in
// closed form: along the nearest side inside the cross, radially from the
// corner circle's centre in the corner quadrant.
vec2 sdRoundRectDir(vec2 p, vec2 b, float r) {
    vec2 q = abs(p) - b + vec2(r);
    if (max(q.x, q.y) < 0.0) {
        return (q.x > q.y) ? vec2(sign(p.x), 0.0) : vec2(0.0, sign(p.y));
    }
    return sign(p) * normalize(max(q, 0.0) + vec2(1e-6));
}

// Height gradient at p. The height is a function of the signed distance
// alone, so grad(H) = H'(d) * grad(d): grad(d) is closed-form above and only
// H'(d) is estimated, with one central difference along that direction.
//
// H'(d) is deliberately NOT evaluated in closed form: the superellipse has an
// infinite slope at the edge (t = 0), and the finite difference over
// gradient_step px is what keeps it bounded - the smoothing the look depends
// on. That is also why gradient_step must not follow the surface size.
vec2 heightGradient(vec2 p, vec2 b, float r, float zScale) {
    vec2 dir = sdRoundRectDir(p, b, r);
    float e = gradient_step;

    float hOut = getHeight(p + dir * e, b, r, zScale);
    float hIn  = getHeight(p - dir * e, b, r, zScale);

    return dir * ((hOut - hIn) / (2.0 * e));
}

vec3 getNormal(vec2 gradH) {
    return normalize(vec3(-gradH.x, -gradH.y, 1.0));
}

// ── Fused shapes (glass-lib's GlassGroup; glass_shape_count > 0) ─────────
//
// Several rounded rectangles merged by a smooth union: close together they
// flow into one body like drops of water, apart they stay separate. With a
// count of 0 none of this runs and the shape is the reference's glass_rect.
// Everything here is in px of the surface (pixel_coord), not local_pos.

#define GLASS_MAX_SHAPES 8
#define GLASS_MAX_BRIDGES 12

uniform float glass_shape_count;                   // 0 = the single glass_rect
uniform vec4  glass_shapes[GLASS_MAX_SHAPES];      // x, y, w, h
uniform float glass_shape_radii[GLASS_MAX_SHAPES];
uniform float glass_merge_k;                       // width of the smooth union, px
uniform float glass_bridge_count;
uniform vec4  glass_bridges[GLASS_MAX_BRIDGES];    // shape a, shape b, inset px, join px

// Polynomial smooth minimum: equals min() when a and b differ by more than
// k, and bulges the join by at most k/4 otherwise.
float sminPoly(float a, float b, float k) {
    float h = max(k - abs(a - b), 0.0) / max(k, 1.0e-4);
    return min(a, b) - h * h * k * 0.25;
}

// Signed distance to the fused body, and the corner radius there (blended
// towards the nearest shapes, so the height profile has one scale).
float fusedSD(vec2 p, out float radius) {
    float ds[GLASS_MAX_SHAPES];
    float d = 1.0e6;
    float dmin = 1.0e6;
    float rsum = 0.0;
    float wsum = 0.0;

    for (int i = 0; i < GLASS_MAX_SHAPES; i++) {
        if (float(i) >= glass_shape_count)
            break;
        vec2 half_size = max(glass_shapes[i].zw * 0.5, vec2(1.0));
        float di = sdRoundRect(p - (glass_shapes[i].xy + half_size), half_size, glass_shape_radii[i]);
        ds[i] = di;
        d = (i == 0) ? di : sminPoly(d, di, glass_merge_k);
        dmin = min(dmin, di);
    }
    // Bridges: shape a swept into shape b (their hull when the two are in a
    // row), inset the more the farther apart they are. Touching shapes
    // become one body with straight sides - a row of round buttons, one
    // capsule - instead of beads; the smooth union alone always leaves a
    // waist at each join. The CPU picks the pairs (glass-renderer.c).
    for (int i = 0; i < GLASS_MAX_BRIDGES; i++) {
        if (float(i) >= glass_bridge_count)
            break;
        vec4 br = glass_bridges[i];
        int a = int(br.x);
        int b = int(br.y);
        vec2 ha = max(glass_shapes[a].zw * 0.5, vec2(1.0));
        vec2 hb = max(glass_shapes[b].zw * 0.5, vec2(1.0));
        vec2 ca = glass_shapes[a].xy + ha;
        vec2 ab = glass_shapes[b].xy + hb - ca;
        float t = clamp(dot(p - ca, ab) / max(dot(ab, ab), 1.0e-4), 0.0, 1.0);
        float db = sdRoundRect(p - (ca + ab * t), mix(ha, hb, t),
                               mix(glass_shape_radii[a], glass_shape_radii[b], t)) + br.z;
        d = sminPoly(d, db, br.w);
    }
    for (int i = 0; i < GLASS_MAX_SHAPES; i++) {
        if (float(i) >= glass_shape_count)
            break;
        float w = exp(-(ds[i] - dmin) / max(glass_merge_k * 0.5, 1.0));
        rsum += w * glass_shape_radii[i];
        wsum += w;
    }
    radius = rsum / max(wsum, 1.0e-4);
    return d;
}

float fusedSD(vec2 p) {
    float r;
    return fusedSD(p, r);
}

// Unit gradient of fusedSD ("straight out of the shape"), by central
// differences: the smooth union has no closed form that is cheap.
vec2 fusedDir(vec2 p) {
    const float e = 0.5;
    vec2 g = vec2(fusedSD(p + vec2(e, 0.0)) - fusedSD(p - vec2(e, 0.0)),
                  fusedSD(p + vec2(0.0, e)) - fusedSD(p - vec2(0.0, e)));
    float len = length(g);
    return len > 1.0e-5 ? g / len : vec2(1.0, 0.0);
}

// getHeight() / heightGradient() of the fused body.
float getHeightFused(vec2 p, float zScale) {
    float r;
    float d = fusedSD(p, r);
    float smoothZone = max(edge_smoothing, 1.0);
    if (d > smoothZone)
        return 0.0;

    float t = clamp(max(-d, 0.0) / max(r, 1.0), 0.0, 1.0);
    float h = profileHeight(t, zScale);
    float fade = 1.0 - smoothstep(-smoothZone, smoothZone, d);
    return h * fade;
}

vec2 heightGradientFused(vec2 p, float zScale) {
    vec2 dir = fusedDir(p);
    float e = gradient_step;

    float hOut = getHeightFused(p + dir * e, zScale);
    float hIn  = getHeightFused(p - dir * e, zScale);

    return dir * ((hOut - hIn) / (2.0 * e));
}
