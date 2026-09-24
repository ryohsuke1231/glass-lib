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
