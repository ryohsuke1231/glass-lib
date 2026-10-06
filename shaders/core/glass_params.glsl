// glass_params.glsl — the uniforms and constants of the glass shader core.
//
// The core is the reference shader (shaders/reference/glass.frag, from the
// GNOME Shell extension) with the extension-only parts removed and the values
// it used to derive from its FBO size made explicit. Every expression keeps
// the reference's order so the two agree to the last bit where it matters
// (tests/golden checks them against each other).
//
// Units: "px" means pixels of the surface the shader is drawn on — the space
// `uv * resolution` lives in. glass-lib passes device pixels.
//
// Every uniform must be set by the caller: an unset uniform reads 0.0, and
// several of these (gradient_step, lens_px_scale, early_exit_enabled,
// edge_taps_enabled) would silently change the look at 0.

// The surface `uv` spans, in px.
uniform float resolution_x;
uniform float resolution_y;

// The glass shape: a rounded rectangle, x/y/w/h in px (no padding: the
// extension's `padding` / `isDock` adjustments are the caller's business).
uniform vec4 glass_rect;
uniform float corner_radius;
// Continuous corners (glass_shape.glsl cornerShape()), 0 = circular arcs.
// The curve starts up to (1 + corner_smoothing) times the corner radius from
// the corner, like Figma's corner smoothing. The reference multiplies it by
// its corner_smoothing_enabled; the caller passes the product.
uniform float corner_smoothing;

// Optics
uniform float max_z;
uniform float displacement_scale;
uniform float edge_smoothing;
uniform float profile_shape_n;
uniform float ior;
uniform float chroma_strength;

// Tone
uniform float tint_strength;
uniform float tint_r;
uniform float tint_g;
uniform float tint_b;
uniform float brightness;
uniform float contrast;
uniform float saturation;

// Surface lighting (the rim / specular / sheen group is gated by
// surface_light_enabled; the drop shadow and inner AO are independent of it)
uniform float specular_intensity;
uniform float shininess;
uniform float rim_width;
uniform float rim_intensity;
uniform float rim_directional_power;
uniform float rim_power;
uniform float rim_light_color_intensity;
uniform float sheen_intensity;
uniform float surface_light_enabled;
// 1 = the highlights keep the backdrop's hue instead of washing out to white
// (glass_surface.glsl backdropHighlight()).
uniform float highlight_backdrop_color;
uniform float light_angle_deg;
uniform float ao_intensity;   // 0 = no inner darkening, 1 = black at the edge
uniform float ao_radius;      // px inward over which the AO band fades out

// Drop shadow
uniform float shadow_radius;
uniform float shadow_intensity;
uniform float shadow_max_radius;   // room the shadow has outside the shape, px

// The blurred backdrop: the sub-rectangle of the surface it covers (px) and
// its real texel count (it is usually downscaled; used to reconstruct it
// smoothly when magnified). blur_rect_w/h < 1 means "the whole surface".
uniform float blur_rect_x;
uniform float blur_rect_y;
uniform float blur_rect_w;
uniform float blur_rect_h;
uniform float blur_tex_w;
uniform float blur_tex_h;

// Switches (1.0 = normal)
uniform float early_exit_enabled;
uniform float edge_taps_enabled;
uniform float debug_view;          // 0 off; 1 shadow/shape masks; 2 the same, raw; 3 lens displacement

// Values the reference derived from the size of its FBO (design.md §10.2).
// The reference computed gradientStep() = clamp(min(res) / 560, 0.45, 1.2)
// and a displacement cap of 0.30 * min(res); its look was signed off on
// full-monitor FBOs, i.e. 1.2 and 324 px at 1080p. Small in-app surfaces
// must not inherit a different value from their own size.
uniform float gradient_step;
uniform float max_displacement_px;
// Scale of the lens's fixed pixel constants (EDGE_LENS_REACH and the
// footprint cap). 1.0 = the reference; glass-lib passes the device scale so
// the lens keeps its logical size on HiDPI.
uniform float lens_px_scale;
// 1.0 = the reference's stabilizedUV(): refraction is damped within ~3% of
// the surface edge (it guarded the monitor edge in the extension). 0.0 = off,
// for surfaces that are only the glass plus a margin.
uniform float edge_damping;
// How the lens's sampling footprint is found (glass_shade.glsl).
// 0.0 = the reference's analytic estimate and its ten fixed taps. > 0.0 =
// measured: the lens is followed across this sample's own footprint,
// +-lens_footprint_px px along the lens direction, and the backdrop is
// averaged along the path it traces. The caller passes half its sample
// spacing (0.5 at one sample per pixel).
uniform float lens_footprint_px;

// ── The bevel's lens: two FIXED constants, deliberately not settings ───────
//
// EDGE_LENS_FALLOFF shapes the ramp: the raw refraction is multiplied by
// (1 - depth/bevel)^falloff, so the displacement builds towards the rim and
// dies where the bevel meets the flat interior. It overlaps with
// profile_shape_n, which is the real control for "how square is the dome";
// shipping both as settings would be two knobs fighting over one effect.
// The three optical settings divide the job between them:
//   displacement_scale — optical thickness, pure magnitude;
//   max_z              — dome height, i.e. how steep the normals get;
//   profile_shape_n    — pure shape.
#define EDGE_LENS_FALLOFF 2.4

// Hard ceiling on how far the rim may sample, in px (times lens_px_scale).
// The caller sizes the blurred region from the same figure; sampling past it
// would read the clamped border of that region and streak.
#define EDGE_LENS_REACH 96.0

// Width of the bevel - the band along the edge over which the dome rises and
// the lens acts - in px (times lens_px_scale). Measured on macOS 27 (5K
// screenshots, docs/memo.md 追記16): the lens reaches the same depth on a
// round button of radius 48 pt, on desktop widgets with 27 pt corners, and
// on the Dock, so it follows neither the size nor the corner radius. Until
// then the dome rose over corner_radius (here and in the reference), which
// made every corner radius setting a different lens. Like the two constants
// above, it is not a setting: displacement_scale / max_z / profile_shape_n
// shape the lens inside it. A glass too small to hold it (its smaller
// half-extent is less) gets the same lens scaled down as a whole - band,
// dome height and displacement together - so it neither changes shape nor
// reaches past the middle of the glass.
#define EDGE_LENS_BAND 22.0

// Most taps per segment of a measured-footprint path (lens_footprint_px > 0;
// four segments, a tap every LENS_PATH_TAP_TEXELS blurred texels). Right past
// the rim a pixel can sweep ~50 px of source, most of it inside one segment.
// The blur's sigma is at least a texel (it is floored there), and a Gaussian
// sampled every 2 sigma averages to within ~1%; closer taps only cost time
// (whole wavefronts wait for the rim's longest loop).
#define LENS_SEGMENT_MAX_TAPS 8
#define LENS_PATH_TAP_TEXELS 2.0
