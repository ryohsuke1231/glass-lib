/* glass-golden.c — the shader core must reproduce the reference shader.
 *
 * Renders shaders/reference/glass.frag (unmodified, wrapped by the compat
 * prelude/postlude) and the shader core (shaders/core, GL target) with the
 * same inputs into offscreen targets of a headless EGL context, and compares
 * them pixel by pixel. design.md §10.5: every pixel within 1/255.
 *
 * The inputs follow how glass-lib drives the core (and how S1 drove the
 * reference): the output rectangle sits in the middle of a virtual canvas,
 * the backdrop is a half-resolution texture covering a capture rectangle
 * around the shape. The core receives the values the reference derived from
 * its canvas (gradient_step, max_displacement_px) computed the same way, the
 * lens at scale 1 and the edge damping on - i.e. the reference's behaviour.
 *
 * glass-lib's iOS 27 material (design.md §10.3.2) has no reference: it is
 * checked against its own arithmetic over flat backdrops instead
 * (check_ios27()).
 *
 *   glass-golden [--write DIR] [--verbose]
 *
 * --write saves reference / core / amplified-difference PNGs of every case
 * that fails (or of every case, with --verbose).
 *
 * SPDX-License-Identifier: MIT
 */
#include <epoxy/egl.h>
#include <epoxy/gl.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gio/gio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define RESOURCE_PREFIX "/io/github/ryohsuke1231/GlassLib/tests/"

/* ── Test matrix ─────────────────────────────────────────────────────────── */

typedef struct {
  const char *name;
  double w, h, radius;
} Shape;

static const Shape shapes[] = {
  { "capsule", 220, 44, 22 },
  { "panel", 420, 120, 24 },
  { "large", 900, 320, 30 },
  { "square", 64, 64, 16 },
  { "pill", 120, 24, 12 },   /* thinner than EDGE_LENS_BAND: the lens is scaled down */
};

typedef struct {
  const char *name;
  double scale;             /* multiplies every px-valued parameter and the shape */
  double shadow_radius, shadow_intensity;
  double chroma, specular;
  double debug_view;
  double early_exit, edge_taps;
  double supersample;
  double canvas_w, canvas_h;
  gboolean near_edge;       /* place the output at the canvas corner (edge damping) */
} Variant;

static const Variant variants[] = {
  { "extension",    1, 30, 0.55, 1.5, 0.0, 0, 1, 1, 1, 1920, 1080, FALSE },
  { "in-app",       1, 16, 0.20, 1.5, 0.4, 0, 1, 1, 1, 1920, 1080, FALSE },
  { "no-chroma",    1, 30, 0.55, 0.0, 0.0, 0, 1, 0, 1, 1920, 1080, FALSE },
  { "no-exits",     1, 30, 0.55, 1.5, 0.4, 0, 0, 1, 1, 1920, 1080, FALSE },
  { "debug-1",      1, 30, 0.55, 1.5, 0.0, 1, 1, 1, 1, 1920, 1080, FALSE },
  { "debug-2",      1, 30, 0.55, 1.5, 0.0, 2, 1, 1, 1, 1920, 1080, FALSE },
  { "supersample",  1, 16, 0.20, 1.5, 0.0, 0, 1, 1, 4, 1920, 1080, FALSE },
  { "scale-2",      2, 16, 0.20, 1.5, 0.0, 0, 1, 1, 1, 3840, 2160, FALSE },
  { "small-canvas", 1, 16, 0.20, 1.5, 0.0, 0, 1, 1, 1, 0, 0, FALSE },   /* canvas = output: gradientStep < 1.2 */
  { "near-edge",    1, 30, 0.55, 1.5, 0.0, 0, 1, 1, 1, 1920, 1080, TRUE },
};

typedef void (*FillFunc) (guint8 *px, int w, int h);

static guint32
hash (guint32 x)
{
  x ^= x >> 16; x *= 0x7feb352d;
  x ^= x >> 15; x *= 0x846ca68b;
  x ^= x >> 16;
  return x;
}

static void
put (guint8 *p, double r, double g, double b)
{
  p[0] = (guint8) CLAMP (lround (r * 255.0), 0, 255);
  p[1] = (guint8) CLAMP (lround (g * 255.0), 0, 255);
  p[2] = (guint8) CLAMP (lround (b * 255.0), 0, 255);
  p[3] = 255;
}

static void
fill_stripes (guint8 *px, int w, int h)
{
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++)
      {
        double v = (x / 6) % 2 ? 0.96 : 0.07;
        put (px + (y * w + x) * 4, v, v, v);
      }
}

static void
fill_checker (guint8 *px, int w, int h)
{
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++)
      {
        double v = ((x / 9) + (y / 9)) % 2 ? 0.98 : 0.13;
        put (px + (y * w + x) * 4, v, v, v);
      }
}

static void
fill_gradient (guint8 *px, int w, int h)
{
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++)
      put (px + (y * w + x) * 4, (double) x / w, (double) y / h, 1.0 - (double) x / w);
}

static void
fill_rings (guint8 *px, int w, int h)
{
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++)
      {
        double r = hypot (x - w * 0.4, y - h * 0.6);
        put (px + (y * w + x) * 4, 0.5 + 0.5 * sin (r * 0.7), 0.5 + 0.5 * sin (r * 0.45 + 1.0), 0.6);
      }
}

static void
fill_noise (guint8 *px, int w, int h)
{
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++)
      {
        guint32 v = hash ((guint32) (y * 7919 + x));
        put (px + (y * w + x) * 4, (v & 255) / 255.0, ((v >> 8) & 255) / 255.0, ((v >> 16) & 255) / 255.0);
      }
}

typedef struct {
  const char *name;
  FillFunc fill;
} Backdrop;

static const Backdrop backdrops[] = {
  { "stripes", fill_stripes },
  { "checker", fill_checker },
  { "gradient", fill_gradient },
  { "rings", fill_rings },
  { "noise", fill_noise },
};

/* The extension's defaults (the optics under test). The sheen is on, although
 * the defaults have it off, so that its arithmetic is compared too. */
static const struct { const char *name; double value; gboolean px; } optics[] = {
  { "max_z", 88.0, TRUE },
  { "displacement_scale", 10.5, TRUE },
  { "edge_smoothing", 0.5, TRUE },
  { "profile_shape_n", 3.6, FALSE },
  { "ior", 2.40, FALSE },
  { "shininess", 42.0, FALSE },
  { "rim_width", 2.3, TRUE },
  { "rim_intensity", 0.5, FALSE },
  { "rim_directional_power", 1.9, FALSE },
  { "rim_power", 3.0, FALSE },
  { "rim_light_color_intensity", 1.0, FALSE },
  { "sheen_intensity", 0.08, FALSE },
  { "light_angle_deg", 90.0, FALSE },
  { "ao_intensity", 0.65, FALSE },
  { "ao_radius", 1.0, TRUE },
  { "tint_r", 1.0, FALSE },
  { "tint_g", 1.0, FALSE },
  { "tint_b", 1.0, FALSE },
  { "tint_strength", 0.12, FALSE },
  { "brightness", 1.0, FALSE },
  { "contrast", 1.0, FALSE },
  { "saturation", 1.0, FALSE },
  { "surface_light_enabled", 1.0, FALSE },
};

/* glass-lib's iOS 27 material, light (spec/params.json: REGULAR, surface and
 * outline ios27). NULL: the reference's look, as in every compared case. */
typedef struct {
  double tint, tint_strength, saturation;
  double frost_opacity, frost_clamp;
  double rim_shade, rim_shade_ends, rim_light, edge_absorption;
} Ios27;

static const Ios27 ios27_light = { 0.973, 0.53, 2.1, 0.73, 0.4, 1.0, 0.2, 1.0, 0.035 };
static const Ios27 *ios27;

/* ── GL plumbing ─────────────────────────────────────────────────────────── */

static char *
load_resource (const char *name)
{
  g_autofree char *path = g_strconcat (RESOURCE_PREFIX, name, NULL);
  GBytes *bytes = g_resources_lookup_data (path, G_RESOURCE_LOOKUP_FLAGS_NONE, NULL);
  char *text;

  g_assert (bytes != NULL);
  text = g_strndup (g_bytes_get_data (bytes, NULL), g_bytes_get_size (bytes));
  g_bytes_unref (bytes);
  return text;
}

static GLuint
compile (GLenum type, const char *header, const char *body)
{
  const char *parts[2] = { header, body };
  GLuint shader = glCreateShader (type);
  GLint ok = GL_FALSE;

  glShaderSource (shader, 2, parts, NULL);
  glCompileShader (shader);
  glGetShaderiv (shader, GL_COMPILE_STATUS, &ok);
  if (!ok)
    {
      char log[8192];
      glGetShaderInfoLog (shader, sizeof log, NULL, log);
      g_error ("shader compile failed: %s", log);
    }
  return shader;
}

static GLuint
program_new (const char *vertex, const char *fragment)
{
  const char *header = "#version 300 es\nprecision highp float;\nprecision highp int;\n";
  GLuint program = glCreateProgram ();
  GLint ok = GL_FALSE;

  glAttachShader (program, compile (GL_VERTEX_SHADER, header, vertex));
  glAttachShader (program, compile (GL_FRAGMENT_SHADER, header, fragment));
  glLinkProgram (program);
  glGetProgramiv (program, GL_LINK_STATUS, &ok);
  if (!ok)
    {
      char log[8192];
      glGetProgramInfoLog (program, sizeof log, NULL, log);
      g_error ("link failed: %s", log);
    }
  return program;
}

static void
u1f (GLuint program, const char *name, double value)
{
  GLint location = glGetUniformLocation (program, name);

  if (location >= 0)
    glUniform1f (location, (float) value);
}

static gboolean
egl_init (void)
{
  EGLDisplay display;
  EGLContext context;
  EGLConfig config;
  EGLint major, minor, n = 0;
  static const EGLint config_attribs[] = {
    EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
    EGL_SURFACE_TYPE, 0,
    EGL_NONE,
  };
  static const EGLint context_attribs[] = {
    EGL_CONTEXT_MAJOR_VERSION, 3,
    EGL_CONTEXT_MINOR_VERSION, 0,
    EGL_NONE,
  };

  /* The EXT entry point: libepoxy cannot resolve the EGL 1.5 one before a
   * display exists (it does not know the version yet). */
  if (!epoxy_has_egl_extension (EGL_NO_DISPLAY, "EGL_EXT_platform_base") ||
      !epoxy_has_egl_extension (EGL_NO_DISPLAY, "EGL_MESA_platform_surfaceless"))
    return FALSE;
  display = eglGetPlatformDisplayEXT (EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, NULL);
  if (display == EGL_NO_DISPLAY || !eglInitialize (display, &major, &minor))
    return FALSE;
  if (!eglBindAPI (EGL_OPENGL_ES_API))
    return FALSE;
  if (!eglChooseConfig (display, config_attribs, &config, 1, &n) || n < 1)
    return FALSE;
  context = eglCreateContext (display, config, EGL_NO_CONTEXT, context_attribs);
  if (context == EGL_NO_CONTEXT)
    return FALSE;
  return eglMakeCurrent (display, EGL_NO_SURFACE, EGL_NO_SURFACE, context);
}

static GLuint
texture_new (int w, int h, const guint8 *pixels)
{
  GLuint tex;

  glGenTextures (1, &tex);
  glBindTexture (GL_TEXTURE_2D, tex);
  glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glPixelStorei (GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
  return tex;
}

/* ── One case ────────────────────────────────────────────────────────────── */

typedef struct {
  /* geometry, all in canvas px */
  double canvas_w, canvas_h;
  double out_x, out_y;       /* output rectangle origin in the canvas */
  int    out_w, out_h;
  double glass_x, glass_y, glass_w, glass_h, radius;
  double blur_x, blur_y, blur_w, blur_h;
  int    tex_w, tex_h;
  double shadow_room;
} Layout;

static void
set_uniforms (GLuint program, gboolean reference, const Layout *l, const Variant *v)
{
  double s = v->scale;

  glUniform4f (glGetUniformLocation (program, "u_uv_rect"),
               (float) (l->out_x / l->canvas_w), (float) (l->out_y / l->canvas_h),
               (float) ((l->out_x + l->out_w) / l->canvas_w), (float) ((l->out_y + l->out_h) / l->canvas_h));
  glUniform2f (glGetUniformLocation (program, "u_pixel_uv"),
               (float) (1.0 / l->canvas_w), (float) (1.0 / l->canvas_h));
  u1f (program, "u_supersample", v->supersample);
  u1f (program, "u_rim_samples", 0.0);

  u1f (program, "resolution_x", l->canvas_w);
  u1f (program, "resolution_y", l->canvas_h);
  u1f (program, "corner_radius", l->radius);
  u1f (program, "blur_rect_x", l->blur_x);
  u1f (program, "blur_rect_y", l->blur_y);
  u1f (program, "blur_rect_w", l->blur_w);
  u1f (program, "blur_rect_h", l->blur_h);
  u1f (program, "blur_tex_w", l->tex_w);
  u1f (program, "blur_tex_h", l->tex_h);

  for (guint i = 0; i < G_N_ELEMENTS (optics); i++)
    u1f (program, optics[i].name, optics[i].value * (optics[i].px ? s : 1.0));
  u1f (program, "chroma_strength", v->chroma * s);
  u1f (program, "specular_intensity", v->specular);
  u1f (program, "shadow_radius", v->shadow_radius * s);
  u1f (program, "shadow_intensity", v->shadow_intensity);
  u1f (program, "shadow_max_radius", l->shadow_room);
  u1f (program, "early_exit_enabled", v->early_exit);
  u1f (program, "edge_taps_enabled", v->edge_taps);
  u1f (program, "debug_view", v->debug_view);

  if (reference)
    {
      glUniform1i (glGetUniformLocation (program, "cogl_sampler1"), 0);
      glUniform1i (glGetUniformLocation (program, "cogl_sampler0"), 0);
      u1f (program, "dock_x", l->glass_x);
      u1f (program, "dock_y", l->glass_y);
      u1f (program, "dock_w", l->glass_w);
      u1f (program, "dock_h", l->glass_h);
      u1f (program, "padding", 0.0);
      u1f (program, "isDock", 0.0);
      u1f (program, "multi_region_mode", 0.0);
      u1f (program, "region_count", 0.0);
      u1f (program, "panel_bg_a", 0.0);
      u1f (program, "pointer_x", -100.0);
      u1f (program, "pointer_y", -100.0);
    }
  else
    {
      double min_res = MAX (MIN (l->canvas_w, l->canvas_h), 1.0);

      glUniform1i (glGetUniformLocation (program, "glass_backdrop"), 0);
      if (ios27)
        {
          /* The frost's cloud: the same texture will do over a flat backdrop. */
          glUniform1i (glGetUniformLocation (program, "glass_frost"), 0);
          u1f (program, "glass_body_mode", 1.0);
          u1f (program, "saturation", ios27->saturation);
          u1f (program, "tint_r", ios27->tint);
          u1f (program, "tint_g", ios27->tint);
          u1f (program, "tint_b", ios27->tint);
          u1f (program, "tint_strength", ios27->tint_strength);
          u1f (program, "frost_opacity", ios27->frost_opacity);
          u1f (program, "frost_clamp", ios27->frost_clamp);
          u1f (program, "rim_shade", ios27->rim_shade);
          u1f (program, "rim_shade_ends", ios27->rim_shade_ends);
          u1f (program, "rim_light", ios27->rim_light);
          u1f (program, "edge_absorption", ios27->edge_absorption);
          /* The ios27-s edge: no macOS rim, no inner shadow, no sheen. */
          u1f (program, "rim_intensity", 0.0);
          u1f (program, "ao_intensity", 0.0);
          u1f (program, "sheen_intensity", 0.0);
        }
      glUniform4f (glGetUniformLocation (program, "glass_rect"),
                   (float) l->glass_x, (float) l->glass_y, (float) l->glass_w, (float) l->glass_h);
      /* What the reference derived from its canvas (design.md §10.2). */
      u1f (program, "gradient_step", CLAMP ((float) min_res / 560.0f, 0.45f, 1.20f));
      u1f (program, "max_displacement_px", 0.30f * (float) min_res);
      u1f (program, "lens_px_scale", 1.0);
      u1f (program, "edge_damping", 1.0);
      u1f (program, "lens_footprint_px", 0.0);
    }
}

static guint8 *
render (GLuint program, gboolean reference, GLuint vao, GLuint backdrop, const Layout *l, const Variant *v)
{
  GLuint tex, fbo;
  guint8 *pixels = g_malloc ((gsize) l->out_w * l->out_h * 4);

  tex = texture_new (l->out_w, l->out_h, NULL);
  glGenFramebuffers (1, &fbo);
  glBindFramebuffer (GL_FRAMEBUFFER, fbo);
  glFramebufferTexture2D (GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
  g_assert (glCheckFramebufferStatus (GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);

  glViewport (0, 0, l->out_w, l->out_h);
  glDisable (GL_BLEND);
  glUseProgram (program);
  glBindVertexArray (vao);
  glActiveTexture (GL_TEXTURE0);
  glBindTexture (GL_TEXTURE_2D, backdrop);
  set_uniforms (program, reference, l, v);
  glDrawArrays (GL_TRIANGLE_STRIP, 0, 4);

  glPixelStorei (GL_PACK_ALIGNMENT, 1);
  glReadPixels (0, 0, l->out_w, l->out_h, GL_RGBA, GL_UNSIGNED_BYTE, pixels);

  glBindFramebuffer (GL_FRAMEBUFFER, 0);
  glDeleteFramebuffers (1, &fbo);
  glDeleteTextures (1, &tex);
  return pixels;
}

static void
layout_for (Layout *l, const Shape *shape, const Variant *v)
{
  double s = v->scale;
  double blur_margin = 17.0 * s;           /* ceil(3 * 5) + 2, as S1 */

  l->glass_w = shape->w * s;
  l->glass_h = shape->h * s;
  l->radius = shape->radius * s;
  l->shadow_room = (v->shadow_radius + 8.0) * s;
  l->out_w = (int) ceil (l->glass_w + 2.0 * l->shadow_room);
  l->out_h = (int) ceil (l->glass_h + 2.0 * l->shadow_room);

  if (v->canvas_w > 0)
    {
      l->canvas_w = v->canvas_w;
      l->canvas_h = v->canvas_h;
    }
  else
    {
      l->canvas_w = l->out_w;
      l->canvas_h = l->out_h;
    }

  if (v->near_edge)
    {
      l->out_x = 0.0;
      l->out_y = 0.0;
    }
  else
    {
      l->out_x = floor ((l->canvas_w - l->out_w) / 2.0);
      l->out_y = floor ((l->canvas_h - l->out_h) / 2.0);
    }

  l->glass_x = l->out_x + l->shadow_room;
  l->glass_y = l->out_y + l->shadow_room;
  l->blur_x = l->glass_x - blur_margin;
  l->blur_y = l->glass_y - blur_margin;
  l->blur_w = l->glass_w + 2.0 * blur_margin;
  l->blur_h = l->glass_h + 2.0 * blur_margin;
  l->tex_w = (int) (l->blur_w / 2.0);
  l->tex_h = (int) (l->blur_h / 2.0);
}

static void
save_png (const char *dir, const char *name, const char *suffix, const guint8 *px, int w, int h)
{
  g_autofree char *file = g_strdup_printf ("%s/%s-%s.png", dir, name, suffix);
  GdkPixbuf *pixbuf = gdk_pixbuf_new_from_data (px, GDK_COLORSPACE_RGB, TRUE, 8, w, h, w * 4, NULL, NULL);

  gdk_pixbuf_save (pixbuf, file, "png", NULL, NULL);
  g_object_unref (pixbuf);
}

static int
luma8 (const guint8 *p)
{
  return (int) lround (0.2126 * p[0] + 0.7152 * p[1] + 0.0722 * p[2]);
}

/* The iOS 27 material against its own arithmetic (glass_material.glsl) over
 * flat black and white: the body is the tint laid over them, the half-point
 * line darkens the edge across the light axis, and the lit lobe (the bottom,
 * light from the top) brightens the rim. Returns the failures. */
static int
check_ios27 (GLuint core, GLuint vao)
{
  const Variant v = { "ios27", 1, 16, 0.015, 0.0, 0.0, 0, 1, 1, 4, 1920, 1080, FALSE };
  const double flat[2] = { 0.0, 1.0 };
  int failures = 0;

  ios27 = &ios27_light;
  for (int f = 0; f < 2; f++)
    {
      Layout l;
      g_autofree guint8 *texels = NULL, *px = NULL;
      GLuint backdrop;
      int gx, gy, gw, gh, cx, cy, centre, expect, edge_min = 255, rim_max = 0;
      double body, adaptive, c;

      layout_for (&l, &shapes[0], &v);
      texels = g_malloc ((gsize) l.tex_w * l.tex_h * 4);
      for (int i = 0; i < l.tex_w * l.tex_h; i++)
        put (texels + i * 4, flat[f], flat[f], flat[f]);
      backdrop = texture_new (l.tex_w, l.tex_h, texels);
      px = render (core, FALSE, vao, backdrop, &l, &v);
      glDeleteTextures (1, &backdrop);

      /* Rows run top down (quad.vert). */
      gx = (int) (l.glass_x - l.out_x);
      gy = (int) (l.glass_y - l.out_y);
      gw = (int) l.glass_w;
      gh = (int) l.glass_h;
      cx = gx + gw / 2;
      cy = gy + gh / 2;

      /* The body: the tint over the backdrop, then the saturation (grey
       * stays grey) and the +-20% adaptive nudge towards the tint. */
      body = flat[f];
      c = body + (ios27_light.tint - body) * ios27_light.tint_strength;
      adaptive = 1.2 + (0.8 - 1.2) * body;
      c += (ios27_light.tint - c) * ios27_light.tint_strength * 0.12 * (adaptive - 1.0);
      expect = (int) lround (c * 255.0);
      centre = luma8 (px + ((gsize) cy * l.out_w + cx) * 4);

      /* The line on the left edge (across the light axis): the first
       * pixels that are mostly glass. */
      for (int x = gx - 1; x < gx + 3; x++)
        {
          const guint8 *p = px + ((gsize) cy * l.out_w + x) * 4;

          if (p[3] >= 200)
            edge_min = MIN (edge_min, luma8 (p) * 255 / MAX (p[3], 1));
        }
      /* The lobe on the bottom edge, a pixel or two in. */
      for (int y = gy + gh - 4; y < gy + gh; y++)
        {
          const guint8 *p = px + ((gsize) y * l.out_w + cx) * 4;

          if (p[3] >= 250)
            rim_max = MAX (rim_max, luma8 (p));
        }

      g_print ("glass-golden: ios27 over %s: centre %d (expected %d), left edge %d, bottom rim %d\n",
               f ? "white" : "black", centre, expect, edge_min, rim_max);
      if (abs (centre - expect) > 2)
        failures++;
      if (f == 1 && edge_min > centre - 25)
        failures++;               /* the line: 0.314 below white, across the light */
      if (f == 0 && rim_max < centre + 6)
        failures++;               /* the lit lobe: about +13/255 over this body */
    }
  ios27 = NULL;
  return failures;
}

int
main (int argc, char **argv)
{
  g_autofree char *write_dir = NULL;
  gboolean verbose = FALSE;
  GOptionEntry entries[] = {
    { "write", 0, 0, G_OPTION_ARG_FILENAME, &write_dir, "Save PNGs of failing cases here", "DIR" },
    { "verbose", 0, 0, G_OPTION_ARG_NONE, &verbose, "Print (and with --write save) every case", NULL },
    { NULL }
  };
  g_autoptr (GOptionContext) options = g_option_context_new ("- glass core vs reference");
  g_autofree char *vertex = NULL, *core_src = NULL, *ref_src = NULL;
  GLuint core, reference, vao;
  int failures = 0, cases = 0, max_overall = 0;

  g_option_context_add_main_entries (options, entries, NULL);
  if (!g_option_context_parse (options, &argc, &argv, NULL))
    return 2;

  if (!egl_init ())
    {
      g_print ("glass-golden: no headless EGL / OpenGL ES 3.0 context, skipping\n");
      return 77;   /* meson: skipped */
    }
  g_print ("glass-golden: %s\n", (const char *) glGetString (GL_RENDERER));

  vertex = load_resource ("quad.vert");
  core_src = load_resource ("glass-core.frag");
  ref_src = load_resource ("glass-reference.frag");
  core = program_new (vertex, core_src);
  reference = program_new (vertex, ref_src);
  glGenVertexArrays (1, &vao);

  if (write_dir)
    g_mkdir_with_parents (write_dir, 0755);

  for (guint vi = 0; vi < G_N_ELEMENTS (variants); vi++)
    for (guint si = 0; si < G_N_ELEMENTS (shapes); si++)
      for (guint bi = 0; bi < G_N_ELEMENTS (backdrops); bi++)
        {
          const Variant *v = &variants[vi];
          Layout l;
          g_autofree guint8 *texels = NULL;
          g_autofree guint8 *a = NULL, *b = NULL;
          g_autofree char *name = NULL;
          GLuint backdrop;
          int max_diff = 0, over = 0;

          layout_for (&l, &shapes[si], v);
          texels = g_malloc ((gsize) l.tex_w * l.tex_h * 4);
          backdrops[bi].fill (texels, l.tex_w, l.tex_h);
          backdrop = texture_new (l.tex_w, l.tex_h, texels);

          a = render (reference, TRUE, vao, backdrop, &l, v);
          b = render (core, FALSE, vao, backdrop, &l, v);
          glDeleteTextures (1, &backdrop);

          for (gsize i = 0; i < (gsize) l.out_w * l.out_h * 4; i++)
            {
              int diff = abs ((int) a[i] - (int) b[i]);
              max_diff = MAX (max_diff, diff);
              if (diff > 1)
                over++;
            }

          name = g_strdup_printf ("%s-%s-%s", v->name, shapes[si].name, backdrops[bi].name);
          cases++;
          max_overall = MAX (max_overall, max_diff);
          if (max_diff > 1)
            failures++;

          if (max_diff > 1 || verbose)
            g_print ("%-5s %-40s %4dx%-4d max diff %3d, values > 1: %d\n",
                     max_diff > 1 ? "FAIL" : "ok", name, l.out_w, l.out_h, max_diff, over);

          if (write_dir && (max_diff > 1 || verbose))
            {
              g_autofree guint8 *d = g_malloc ((gsize) l.out_w * l.out_h * 4);

              for (gsize i = 0; i < (gsize) l.out_w * l.out_h; i++)
                {
                  int m = 0;
                  for (int c = 0; c < 4; c++)
                    m = MAX (m, abs ((int) a[i * 4 + c] - (int) b[i * 4 + c]));
                  d[i * 4 + 0] = d[i * 4 + 1] = d[i * 4 + 2] = (guint8) MIN (m * 32, 255);
                  d[i * 4 + 3] = 255;
                }
              save_png (write_dir, name, "reference", a, l.out_w, l.out_h);
              save_png (write_dir, name, "core", b, l.out_w, l.out_h);
              save_png (write_dir, name, "diff", d, l.out_w, l.out_h);
            }
        }

  g_print ("glass-golden: %d cases, %d failed, largest difference %d/255\n", cases, failures, max_overall);

  {
    int ios_failures = check_ios27 (core, vao);

    g_print ("glass-golden: ios27 material: %s\n", ios_failures ? "FAIL" : "ok");
    failures += ios_failures;
  }
  return failures ? 1 : 0;
}
