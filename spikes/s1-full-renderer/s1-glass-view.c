/* s1-glass-view.c — the S1 spike's glass host widget.
 *
 * S1 answers one question before the library is built: can a GTK 4.22 widget
 * draw the extension's glass.frag over its own content, in the same frame as
 * that content, fast enough? It follows design.md §7–§8:
 *
 *   snapshot:
 *     1. backdrop colour + content                    -> appended as usual
 *     2. per panel:
 *          capture   gsk_renderer_render_texture() of the backdrop, at 1/2 scale
 *          download  GdkTextureDownloader            (no public zero-copy path)
 *          upload    glTexSubImage2D into our own GL context
 *          blur      the extension's Gaussian, H then V (separate targets: DAG)
 *          glass     the unmodified reference glass.frag via cogl_prelude.glsl
 *          hand-off  GdkGLTextureBuilder -> gtk_snapshot_append_texture()
 *     3. overlay children (the panels' foreground)
 *
 * The glass body is drawn by the view, never by the panel: GTK caches each
 * widget's render node, so a panel's own snapshot does not run when only the
 * content under it scrolled. The view is snapshotted whenever any descendant
 * changed, so glass and content can never be a frame apart.
 *
 * SPDX-License-Identifier: MIT
 */
#include "s1-glass-view.h"
#include "s1-gl.h"

#include <math.h>
#include <string.h>

#define RESOURCE_PREFIX "/io/github/ryohsuke1231/GlassLib/s1/"

#define OUTPUT_POOL_SIZE 4

/* Room kept around the panel for the drop shadow, on top of shadow_radius.
 * glass.frag's boundsMask starts fading at 85% of shadow_max_radius; with this
 * margin that point lies past the penumbra, so the room never clips it. */
#define SHADOW_ROOM_EXTRA 8.0

/* glass.frag reads two values off the size of the surface it is drawn on
 * (design.md §10.2): gradientStep() = clamp(min(res) / 560, 0.45, 1.2) and the
 * displacement cap 0.30 * min(res). The look was signed off on full-monitor
 * FBOs, i.e. 1.2 and 324 px on 1080p. S1 runs the reference shader unmodified,
 * so it reproduces that by placing the output inside a virtual 1920x1080
 * canvas. The output sits in the middle of it: stabilizedUV() damps the
 * refraction within ~3% of the canvas edge, which would otherwise hit the
 * top-left of every panel. */
/* In-app shadow. The extension's shadow (radius 30, intensity 0.55) was
 * tuned for the dock and menus floating over a wallpaper; on a small capsule
 * sitting on a light window its umbra reads as a hard dark ring hugging the
 * glass. Controls inside an app want far less (user feedback, 2026-09-24).
 * Same shadow maths, smaller values - the extension's pair stays available
 * with --shadow-radius=30 --shadow-intensity=0.55 or the H key. */
#define IN_APP_SHADOW_RADIUS 16.0
#define IN_APP_SHADOW_INTENSITY 0.20

/* glass_params.glsl's EDGE_LENS_REACH, logical px. */
#define LENS_REACH 96.0

#define CANVAS_W 1920.0
#define CANVAS_H 1080.0
#define CANVAS_EDGE 64.0

/* The extension's gschema defaults (Dock: blur 5, white tint 0.12; the global
 * glass-* keys for the rest). Lengths are logical px, scaled per surface. */
typedef struct {
  double max_z, displacement_scale, edge_smoothing, profile_shape_n, ior, chroma_strength;
  double specular_intensity, shininess;
  double rim_width, rim_intensity, rim_directional_power, rim_power, rim_light_color_intensity;
  double sheen_intensity, light_angle_deg;
  double shadow_radius, shadow_intensity, ao_intensity, ao_radius;
  double tint_r, tint_g, tint_b, tint_strength;
  double brightness, contrast, saturation;
} GlassParams;

static const GlassParams default_params = {
  .max_z = 25.0, .displacement_scale = 78.5, .edge_smoothing = 2.0,
  .profile_shape_n = 7.0, .ior = 2.40, .chroma_strength = 1.5,
  .specular_intensity = 0.0, .shininess = 42.0,
  .rim_width = 5.0, .rim_intensity = 0.6, .rim_directional_power = 2.7,
  .rim_power = 6.0, .rim_light_color_intensity = 1.4,
  .sheen_intensity = 0.32, .light_angle_deg = 50.0,
  .shadow_radius = 30.0, .shadow_intensity = 0.55,
  .ao_intensity = 0.25, .ao_radius = 7.5,
  .tint_r = 1.0, .tint_g = 1.0, .tint_b = 1.0, .tint_strength = 0.12,
  .brightness = 1.0, .contrast = 1.0, .saturation = 1.0,
};

/* Edge presets for in-app glass (user requests, 2026-09-24/25: "the edge
 * should be sharper, cleaner, thinner", then "thinner's edge, but keep the
 * glass"). The extension's values are absolute px tuned for large glass (a
 * dock, a menu); on a 40 px capsule the same 5 px rim, 7.5 px inner shadow,
 * 2 px feather and 1.5 px colour fringe make the edge a fifth of the height.
 * Same shader, same parameters, other values - chosen side by side from
 * sweeps (docs/memo.md 追記4・5). Two groups of values:
 *
 * The edge's lines - thin in every in-app preset:
 *   edge_smoothing   silhouette antialiasing width (+-px): crisper outline
 *   rim_width        width of the rim-light band
 *   rim_power        Fresnel exponent: keeps the light on the outermost pixels
 *   ao_radius/ao_intensity   the dark band just inside the edge
 *   chroma_strength  the colour fringe
 *
 * The lens - what makes it read as glass: the background bending over the
 * last ~5-10 px. "thinner" squeezes it into the outer 2 px (profile_shape_n
 * 24), where it is only a line and the glass looks flat; the crisp presets
 * keep the extension's dome shape (profile_shape_n 7) at three strengths:
 *   max_z            dome height, i.e. how steep the normals get
 *   displacement_scale   optical thickness
 */
typedef struct {
  const char *name;
  const char *params;   /* name=value,... applied on top of default_params */
} EdgePreset;

#define THIN_LINES "edge_smoothing=0.75,rim_width=2,rim_power=9,ao_radius=3,ao_intensity=0.10,chroma_strength=0.8"

/* In E-key order; the first is the default (the user's choice, 2026-09-25). */
static const EdgePreset edge_presets[] = {
  { "crisp-soft",   THIN_LINES ",profile_shape_n=7,max_z=14,displacement_scale=45" },
  { "crisp",        THIN_LINES ",profile_shape_n=7,max_z=20,displacement_scale=60" },
  { "crisp-strong", THIN_LINES },   /* the extension's lens */
  { "thinner",      THIN_LINES ",profile_shape_n=24,max_z=14,displacement_scale=30" },
  { "extension",    "" },
};

/* Name -> field, for tuning from the command line (--param). */
static const struct {
  const char *name;
  gsize       offset;
} param_fields[] = {
#define FIELD(f) { #f, G_STRUCT_OFFSET (GlassParams, f) }
  FIELD (max_z), FIELD (displacement_scale), FIELD (edge_smoothing), FIELD (profile_shape_n),
  FIELD (ior), FIELD (chroma_strength), FIELD (specular_intensity), FIELD (shininess),
  FIELD (rim_width), FIELD (rim_intensity), FIELD (rim_directional_power), FIELD (rim_power),
  FIELD (rim_light_color_intensity), FIELD (sheen_intensity), FIELD (light_angle_deg),
  FIELD (shadow_radius), FIELD (shadow_intensity), FIELD (ao_intensity), FIELD (ao_radius),
  FIELD (tint_r), FIELD (tint_g), FIELD (tint_b), FIELD (tint_strength),
  FIELD (brightness), FIELD (contrast), FIELD (saturation),
#undef FIELD
};

/* ── Output texture pool ───────────────────────────────────────────────────
 *
 * A GdkGLTexture's id must stay untouched until GTK releases the texture, so
 * each finished glass image lives in its own slot until the release callback
 * returns it. The callback may run while another GL context is current (GTK's
 * own GL renderer), so it does not touch GL (docs/memo.md 地雷2): it parks
 * the sync object, which is deleted the next time our context is current.
 * Only after the view unrealized ("dead") does it switch contexts to free
 * what is left. */

typedef struct _OutputPool OutputPool;

typedef struct {
  OutputPool *pool;
  GLuint      tex;
  GLuint      fbo;
  int         w, h;
  gboolean    in_use;
  GLsync      released_sync;
} OutputSlot;

struct _OutputPool {
  grefcount     ref;
  GdkGLContext *ctx;
  gboolean      dead;
  OutputSlot    slots[OUTPUT_POOL_SIZE];
};

typedef struct {
  OutputSlot *slot;
  GLsync      sync;
} ReleaseData;

static OutputPool *
pool_new (GdkGLContext *ctx)
{
  OutputPool *pool = g_new0 (OutputPool, 1);

  g_ref_count_init (&pool->ref);
  pool->ctx = g_object_ref (ctx);
  for (int i = 0; i < OUTPUT_POOL_SIZE; i++)
    pool->slots[i].pool = pool;

  return pool;
}

static void
pool_unref (OutputPool *pool)
{
  if (!g_ref_count_dec (&pool->ref))
    return;

  g_object_unref (pool->ctx);
  g_free (pool);
}

static void
slot_delete_gl (OutputSlot *slot)
{
  if (slot->released_sync)
    glDeleteSync (slot->released_sync);
  if (slot->fbo)
    glDeleteFramebuffers (1, &slot->fbo);
  if (slot->tex)
    glDeleteTextures (1, &slot->tex);
  slot->released_sync = NULL;
  slot->fbo = slot->tex = 0;
  slot->w = slot->h = 0;
}

static void
output_released (gpointer data)
{
  ReleaseData *rd = data;
  OutputSlot *slot = rd->slot;
  OutputPool *pool = slot->pool;

  if (pool->dead)
    {
      GdkGLContext *prev = gdk_gl_context_get_current ();

      if (prev)
        g_object_ref (prev);
      gdk_gl_context_make_current (pool->ctx);
      glDeleteSync (rd->sync);
      slot_delete_gl (slot);
      if (prev)
        {
          gdk_gl_context_make_current (prev);
          g_object_unref (prev);
        }
      else
        gdk_gl_context_clear_current ();
    }
  else
    {
      g_assert (slot->released_sync == NULL);
      slot->released_sync = rd->sync;
    }

  slot->in_use = FALSE;
  g_free (rd);
  pool_unref (pool);
}

/* Our context must be current. */
static void
pool_reap (OutputPool *pool)
{
  for (int i = 0; i < OUTPUT_POOL_SIZE; i++)
    {
      OutputSlot *slot = &pool->slots[i];

      if (!slot->in_use && slot->released_sync)
        {
          glDeleteSync (slot->released_sync);
          slot->released_sync = NULL;
        }
    }
}

static void
alloc_texture (GLuint *tex, int w, int h)
{
  if (*tex == 0)
    glGenTextures (1, tex);
  glBindTexture (GL_TEXTURE_2D, *tex);
  glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
}

static void
attach_fbo (GLuint *fbo, GLuint tex)
{
  if (*fbo == 0)
    glGenFramebuffers (1, fbo);
  glBindFramebuffer (GL_FRAMEBUFFER, *fbo);
  glFramebufferTexture2D (GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
  if (glCheckFramebufferStatus (GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    g_warning ("glass: framebuffer incomplete");
  glBindFramebuffer (GL_FRAMEBUFFER, 0);
}

/* Our context must be current. NULL when every slot is still held by GTK. */
static OutputSlot *
pool_acquire (OutputPool *pool, int w, int h)
{
  OutputSlot *spare = NULL;

  for (int i = 0; i < OUTPUT_POOL_SIZE; i++)
    {
      OutputSlot *slot = &pool->slots[i];

      if (slot->in_use)
        continue;
      if (slot->tex && slot->w == w && slot->h == h)
        return slot;
      if (spare == NULL)
        spare = slot;
    }

  if (spare == NULL)
    return NULL;

  alloc_texture (&spare->tex, w, h);
  attach_fbo (&spare->fbo, spare->tex);
  spare->w = w;
  spare->h = h;

  return spare;
}

/* ── Per-panel state ──────────────────────────────────────────────────────── */

typedef struct {
  GtkWidget     *widget;          /* the overlay child; owned by the view */

  /* capture -> blur chain, all at capture resolution */
  GLuint         capture_tex, capture_fbo;
  GLuint         full_tex;        /* --capture-full: the full-resolution capture */
  int            full_w, full_h;
  GLuint         blur_h_tex, blur_h_fbo;
  GLuint         blur_v_tex, blur_v_fbo;
  int            tex_w, tex_h;
  GLuint         backdrop_tex;    /* what glass.frag samples: blur_v_tex, or capture_tex without blur */
  guint8        *pixels;
  gsize          pixels_size;

  /* capture cache key */
  gboolean       capture_valid;
  GskRenderNode *key_leaf;
  float          key_dx, key_dy;
  graphene_rect_t key_capture;
  double         key_capture_scale;
  guint          key_blur_gen;

  /* finished-glass cache key */
  GdkTexture    *last_texture;
  graphene_rect_t last_out;
  graphene_rect_t key_panel;
  double         key_scale;
  guint          key_params_gen;
} PanelState;

static void
panel_state_release_gl (PanelState *ps)
{
  GLuint textures[4] = { ps->capture_tex, ps->full_tex, ps->blur_h_tex, ps->blur_v_tex };
  GLuint fbos[3] = { ps->capture_fbo, ps->blur_h_fbo, ps->blur_v_fbo };

  glDeleteTextures (4, textures);
  glDeleteFramebuffers (3, fbos);
  ps->capture_tex = ps->full_tex = ps->blur_h_tex = ps->blur_v_tex = 0;
  ps->capture_fbo = ps->blur_h_fbo = ps->blur_v_fbo = 0;
  ps->full_w = ps->full_h = 0;
  ps->backdrop_tex = 0;
  ps->tex_w = ps->tex_h = 0;
  ps->capture_valid = FALSE;
  g_clear_pointer (&ps->key_leaf, gsk_render_node_unref);
  g_clear_object (&ps->last_texture);
}

static void
panel_state_free (PanelState *ps)
{
  g_clear_pointer (&ps->key_leaf, gsk_render_node_unref);
  g_clear_object (&ps->last_texture);
  g_free (ps->pixels);
  g_free (ps);
}

/* ── The widget ───────────────────────────────────────────────────────────── */

struct _S1GlassView {
  GtkWidget     parent_instance;

  GtkWidget    *content;
  GPtrArray    *overlays;         /* GtkWidget*, parented to us */
  GPtrArray    *panels;           /* PanelState* */
  GdkRGBA       backdrop_color;

  S1Options     opts;
  GlassParams   params;
  guint         params_gen;       /* bumped whenever anything the glass pass reads changes */
  S1GaussKernel kernel;
  gboolean      have_blur;
  guint         blur_gen;         /* bumped whenever the capture/blur chain changes */

  GdkGLContext *ctx;
  gboolean      gl_failed;
  gboolean      use_es;
  char         *gl_info;
  char         *vertex_source;
  GLuint        vao;
  GLuint        prog_glass;       /* the shader core (the library's) */
  GLuint        prog_reference;   /* the reference shader, wrapped */
  GLuint        prog_current;     /* the one glass_uniforms describes */
  GHashTable   *glass_uniforms;   /* static name -> location + 1, for prog_current */
  GLuint        prog_blur_h, prog_blur_v;
  GLuint        prog_copy;        /* bilinear passthrough: exact 2x2 box at a 2:1 ratio */
  int           blur_pairs_built;
  OutputPool   *pool;

  GskRenderer  *private_renderer;

  S1Stats       stats;
};

G_DEFINE_FINAL_TYPE (S1GlassView, s1_glass_view, GTK_TYPE_WIDGET)

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

static gboolean
ensure_gl (S1GlassView *self)
{
  g_autoptr (GError) error = NULL;
  g_autofree char *core_src = NULL;
  g_autofree char *reference_src = NULL;
  GtkNative *native;
  GdkSurface *surface;
  GdkGLContext *ctx;
  int major = 0, minor = 0;
  gboolean es;

  if (self->ctx)
    return TRUE;
  if (self->gl_failed)
    return FALSE;

  native = gtk_widget_get_native (GTK_WIDGET (self));
  surface = native ? gtk_native_get_surface (native) : NULL;
  if (surface == NULL)
    return FALSE;

  ctx = gdk_surface_create_gl_context (surface, &error);
  if (ctx == NULL || !gdk_gl_context_realize (ctx, &error))
    {
      g_warning ("glass: no GL context, drawing without glass: %s", error->message);
      g_clear_object (&ctx);
      self->gl_failed = TRUE;
      return FALSE;
    }

  gdk_gl_context_make_current (ctx);
  gdk_gl_context_get_version (ctx, &major, &minor);
  es = gdk_gl_context_get_use_es (ctx);

  if (es ? major < 3 : major * 10 + minor < 33)
    {
      g_warning ("glass: needs OpenGL ES 3.0 or OpenGL 3.3, got %s %d.%d",
                 es ? "OpenGL ES" : "OpenGL", major, minor);
      gdk_gl_context_clear_current ();
      g_object_unref (ctx);
      self->gl_failed = TRUE;
      return FALSE;
    }

  self->vertex_source = load_resource ("quad.vert");
  core_src = load_resource ("glass-core.frag");
  reference_src = load_resource ("glass-reference.frag");

  {
    const char *core_parts[] = { core_src, NULL };
    const char *reference_parts[] = { reference_src, NULL };

    self->prog_glass = s1_gl_program_new (es, self->vertex_source, core_parts, &error);
    if (self->prog_glass)
      self->prog_reference = s1_gl_program_new (es, self->vertex_source, reference_parts, &error);
  }
  if (self->prog_glass == 0 || self->prog_reference == 0)
    {
      g_warning ("glass: glass.frag did not build: %s", error->message);
      g_clear_pointer (&self->vertex_source, g_free);
      gdk_gl_context_clear_current ();
      g_object_unref (ctx);
      self->gl_failed = TRUE;
      return FALSE;
    }

  {
    static const char copy_src[] =
      "uniform sampler2D u_src;\n"
      "in vec4 v_tex_coord;\n"
      "out vec4 frag_color;\n"
      "void main() { frag_color = texture(u_src, v_tex_coord.st); }\n";
    const char *copy_parts[] = { copy_src, NULL };

    self->prog_copy = s1_gl_program_new (es, self->vertex_source, copy_parts, &error);
    g_assert (self->prog_copy != 0);
  }

  glGenVertexArrays (1, &self->vao);
  self->glass_uniforms = g_hash_table_new (g_str_hash, g_str_equal);
  self->pool = pool_new (ctx);
  self->ctx = ctx;
  self->use_es = es;
  g_free (self->gl_info);
  self->gl_info = g_strdup_printf ("%s %d.%d, %s", es ? "OpenGL ES" : "OpenGL", major, minor,
                                   (const char *) glGetString (GL_RENDERER));

  return TRUE;
}

/* Our context must be current. */
static gboolean
ensure_blur_programs (S1GlassView *self)
{
  g_autoptr (GError) error = NULL;
  g_autofree char *h_src = NULL;
  g_autofree char *v_src = NULL;

  if (!self->have_blur || self->blur_pairs_built == self->kernel.fetch_pairs)
    return TRUE;

  if (self->prog_blur_h)
    glDeleteProgram (self->prog_blur_h);
  if (self->prog_blur_v)
    glDeleteProgram (self->prog_blur_v);
  self->prog_blur_h = self->prog_blur_v = 0;
  self->blur_pairs_built = 0;

  /* The weights are baked in; only the fetch count forces a rebuild. The
   * sigma inside the same fetch count goes through kernel_scale. */
  h_src = s1_gauss_fragment_source (&self->kernel, TRUE);
  v_src = s1_gauss_fragment_source (&self->kernel, FALSE);

  {
    const char *h_parts[] = { h_src, NULL };
    const char *v_parts[] = { v_src, NULL };

    self->prog_blur_h = s1_gl_program_new (self->use_es, self->vertex_source, h_parts, &error);
    if (self->prog_blur_h)
      self->prog_blur_v = s1_gl_program_new (self->use_es, self->vertex_source, v_parts, &error);
  }

  if (self->prog_blur_v == 0)
    {
      g_warning ("glass: blur did not build: %s", error->message);
      return FALSE;
    }

  self->blur_pairs_built = self->kernel.fetch_pairs;
  return TRUE;
}

static void
release_gl (S1GlassView *self)
{
  if (self->ctx)
    {
      gdk_gl_context_make_current (self->ctx);

      for (guint i = 0; i < self->panels->len; i++)
        panel_state_release_gl (g_ptr_array_index (self->panels, i));

      if (self->prog_glass)
        glDeleteProgram (self->prog_glass);
      if (self->prog_reference)
        glDeleteProgram (self->prog_reference);
      self->prog_reference = self->prog_current = 0;
      if (self->prog_blur_h)
        glDeleteProgram (self->prog_blur_h);
      if (self->prog_blur_v)
        glDeleteProgram (self->prog_blur_v);
      if (self->prog_copy)
        glDeleteProgram (self->prog_copy);
      self->prog_copy = 0;
      if (self->vao)
        glDeleteVertexArrays (1, &self->vao);
      self->prog_glass = self->prog_blur_h = self->prog_blur_v = 0;
      self->vao = 0;
      self->blur_pairs_built = 0;

      /* Free what GTK no longer holds; the rest is freed on release. */
      for (int i = 0; i < OUTPUT_POOL_SIZE; i++)
        {
          OutputSlot *slot = &self->pool->slots[i];

          if (!slot->in_use)
            slot_delete_gl (slot);
        }
      self->pool->dead = TRUE;
      g_clear_pointer (&self->pool, pool_unref);

      g_clear_pointer (&self->glass_uniforms, g_hash_table_unref);
      gdk_gl_context_clear_current ();
      g_clear_object (&self->ctx);
    }

  g_clear_pointer (&self->vertex_source, g_free);

  if (self->private_renderer)
    {
      gsk_renderer_unrealize (self->private_renderer);
      g_clear_object (&self->private_renderer);
    }
}

static GLint
glass_uniform (S1GlassView *self, const char *name)
{
  gpointer value;
  GLint location;

  if (g_hash_table_lookup_extended (self->glass_uniforms, name, NULL, &value))
    return GPOINTER_TO_INT (value) - 1;

  location = glGetUniformLocation (self->prog_current, name);
  g_hash_table_insert (self->glass_uniforms, (gpointer) name, GINT_TO_POINTER (location + 1));

  return location;
}

static void
u1f (S1GlassView *self, const char *name, double value)
{
  GLint location = glass_uniform (self, name);

  if (location >= 0)
    glUniform1f (location, (float) value);
}

static GskRenderer *
capture_renderer (S1GlassView *self)
{
  GtkNative *native = gtk_widget_get_native (GTK_WIDGET (self));
  GskRenderer *window_renderer = gtk_native_get_renderer (native);
  g_autoptr (GError) error = NULL;
  GskRenderer *renderer;

  if (!self->opts.private_renderer)
    return window_renderer;
  if (self->private_renderer)
    return self->private_renderer;

  renderer = g_object_new (G_OBJECT_TYPE (window_renderer), NULL);
  if (!gsk_renderer_realize_for_display (renderer, gtk_widget_get_display (GTK_WIDGET (self)), &error))
    {
      g_warning ("glass: private renderer failed, using the window's: %s", error->message);
      g_object_unref (renderer);
      self->opts.private_renderer = FALSE;
      return window_renderer;
    }

  self->private_renderer = renderer;
  return renderer;
}

/* Grows r outwards to whole pixels of a grid of `scale` pixels per logical
 * px, and returns its pixel size. */
static void
snap_rect (graphene_rect_t *r, double scale, int *px_w, int *px_h)
{
  double x0 = floor (r->origin.x * scale);
  double y0 = floor (r->origin.y * scale);
  double x1 = ceil ((r->origin.x + r->size.width) * scale);
  double y1 = ceil ((r->origin.y + r->size.height) * scale);

  *px_w = (int) (x1 - x0);
  *px_h = (int) (y1 - y0);
  graphene_rect_init (r, x0 / scale, y0 / scale, (x1 - x0) / scale, (y1 - y0) / scale);
}

/* Strips the translate-only transform nodes and single-child containers that
 * gtk_widget_snapshot_child() wraps around a child's cached node. The node
 * underneath is the same object as long as the child did not redraw, which
 * makes it a cheap and exact "did the content change" key. Anything else
 * (a scale, a real container) stops the unwrapping and simply never matches
 * the previous frame: the safe direction. */
static GskRenderNode *
unwrap_node (GskRenderNode *node, float *dx, float *dy)
{
  *dx = *dy = 0.0f;

  while (node != NULL)
    {
      GskRenderNodeType type = gsk_render_node_get_node_type (node);

      if (type == GSK_TRANSFORM_NODE)
        {
          GskTransform *transform = gsk_transform_node_get_transform (node);
          float x, y;

          if (gsk_transform_get_category (transform) < GSK_TRANSFORM_CATEGORY_2D_TRANSLATE)
            break;
          gsk_transform_to_translate (transform, &x, &y);
          *dx += x;
          *dy += y;
          node = gsk_transform_node_get_child (node);
        }
      else if (type == GSK_CONTAINER_NODE && gsk_container_node_get_n_children (node) == 1)
        node = gsk_container_node_get_child (node, 0);
      else
        break;
    }

  return node;
}

static void
run_blur_pass (S1GlassView *self, GLuint program, GLuint src, GLuint dst_fbo, int w, int h)
{
  glBindFramebuffer (GL_FRAMEBUFFER, dst_fbo);
  glViewport (0, 0, w, h);
  glUseProgram (program);
  glActiveTexture (GL_TEXTURE0);
  glBindTexture (GL_TEXTURE_2D, src);
  glUniform1i (glGetUniformLocation (program, "u_src"), 0);
  glUniform2f (glGetUniformLocation (program, "inv_size"), 1.0f / w, 1.0f / h);
  glUniform1f (glGetUniformLocation (program, "kernel_scale"), (float) self->kernel.scale);
  glUniform4f (glGetUniformLocation (program, "u_uv_rect"), 0.0f, 0.0f, 1.0f, 1.0f);
  glDrawArrays (GL_TRIANGLE_STRIP, 0, 4);
}

/* Renders the backdrop under C into ps->backdrop_tex, blurred. Our context
 * must be current. */
static gboolean
capture_and_blur (S1GlassView           *self,
                  PanelState            *ps,
                  GskRenderNode         *backdrop,
                  const graphene_rect_t *C,
                  double                 capture_scale,
                  int                    c_w,
                  int                    c_h)
{
  GskRenderer *renderer = capture_renderer (self);
  GskRenderNode *clip, *node;
  GskTransform *transform;
  GdkTextureDownloader *downloader;
  GdkTexture *texture;
  gint64 t0, t1, t2, t3, t4;
  int w, h, full_w, full_h;
  gsize stride;
  /* --capture-full renders at twice the capture scale and box-filters in GL,
   * which is the extension's chain: a full-resolution capture, then a
   * bilinear 2:1 pre-pass (an exact 2x2 box). */
  int factor = self->opts.capture_full ? 2 : 1;

  clip = gsk_clip_node_new (backdrop, C);
  transform = gsk_transform_scale (NULL, capture_scale * factor, capture_scale * factor);
  transform = gsk_transform_translate (transform, &GRAPHENE_POINT_INIT (-C->origin.x, -C->origin.y));
  node = gsk_transform_node_new (clip, transform);
  gsk_transform_unref (transform);
  gsk_render_node_unref (clip);

  t0 = g_get_monotonic_time ();
  texture = gsk_renderer_render_texture (renderer, node,
                                         &GRAPHENE_RECT_INIT (0, 0, c_w * factor, c_h * factor));
  t1 = g_get_monotonic_time ();
  gsk_render_node_unref (node);

  if (texture == NULL)
    return FALSE;

  full_w = gdk_texture_get_width (texture);
  full_h = gdk_texture_get_height (texture);
  w = full_w / factor;
  h = full_h / factor;
  stride = (gsize) full_w * 4;
  if (ps->pixels_size < stride * full_h)
    {
      g_free (ps->pixels);
      ps->pixels_size = stride * full_h;
      ps->pixels = g_malloc (ps->pixels_size);
    }

  downloader = gdk_texture_downloader_new (texture);
  gdk_texture_downloader_set_format (downloader, GDK_MEMORY_R8G8B8A8_PREMULTIPLIED);
  gdk_texture_downloader_download_into (downloader, ps->pixels, stride);
  gdk_texture_downloader_free (downloader);
  g_object_unref (texture);

  /* [docs/memo.md 地雷1] GTK's GL renderer makes ITS context current to
   * render and download, and leaves it that way. Textures and programs are
   * shared between the two contexts, framebuffer objects and VAOs are not:
   * without this, our blur and glass passes bind ids that mean nothing (or
   * something else) in GTK's context and draw into the wrong framebuffer.
   * The Vulkan renderer never touches GL, which is why only GSK_RENDERER=gl
   * showed it. */
  gdk_gl_context_make_current (self->ctx);
  t2 = g_get_monotonic_time ();

  if (ps->tex_w != w || ps->tex_h != h)
    {
      alloc_texture (&ps->capture_tex, w, h);
      alloc_texture (&ps->blur_h_tex, w, h);
      alloc_texture (&ps->blur_v_tex, w, h);
      attach_fbo (&ps->capture_fbo, ps->capture_tex);
      attach_fbo (&ps->blur_h_fbo, ps->blur_h_tex);
      attach_fbo (&ps->blur_v_fbo, ps->blur_v_tex);
      ps->tex_w = w;
      ps->tex_h = h;
    }

  glPixelStorei (GL_UNPACK_ALIGNMENT, 4);
  glPixelStorei (GL_UNPACK_ROW_LENGTH, 0);
  if (factor == 1)
    {
      glBindTexture (GL_TEXTURE_2D, ps->capture_tex);
      glTexSubImage2D (GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, ps->pixels);
    }
  else
    {
      if (ps->full_w != full_w || ps->full_h != full_h)
        {
          alloc_texture (&ps->full_tex, full_w, full_h);
          ps->full_w = full_w;
          ps->full_h = full_h;
        }
      glBindTexture (GL_TEXTURE_2D, ps->full_tex);
      glTexSubImage2D (GL_TEXTURE_2D, 0, 0, 0, full_w, full_h, GL_RGBA, GL_UNSIGNED_BYTE, ps->pixels);

      /* One bilinear fetch per output texel, centred between four source
       * texels: the exact 2x2 average. */
      glBindVertexArray (self->vao);
      glDisable (GL_BLEND);
      glBindFramebuffer (GL_FRAMEBUFFER, ps->capture_fbo);
      glViewport (0, 0, w, h);
      glUseProgram (self->prog_copy);
      glActiveTexture (GL_TEXTURE0);
      glBindTexture (GL_TEXTURE_2D, ps->full_tex);
      glUniform1i (glGetUniformLocation (self->prog_copy, "u_src"), 0);
      glUniform4f (glGetUniformLocation (self->prog_copy, "u_uv_rect"), 0.0f, 0.0f, 1.0f, 1.0f);
      glDrawArrays (GL_TRIANGLE_STRIP, 0, 4);
      glBindFramebuffer (GL_FRAMEBUFFER, 0);
    }
  t3 = g_get_monotonic_time ();

  if (self->have_blur && ensure_blur_programs (self))
    {
      glBindVertexArray (self->vao);
      glDisable (GL_BLEND);
      run_blur_pass (self, self->prog_blur_h, ps->capture_tex, ps->blur_h_fbo, w, h);
      run_blur_pass (self, self->prog_blur_v, ps->blur_h_tex, ps->blur_v_fbo, w, h);
      glBindFramebuffer (GL_FRAMEBUFFER, 0);
      ps->backdrop_tex = ps->blur_v_tex;
    }
  else
    ps->backdrop_tex = ps->capture_tex;
  t4 = g_get_monotonic_time ();

  self->stats.us_render_texture += t1 - t0;
  self->stats.us_download += t2 - t1;
  self->stats.us_upload += t3 - t2;
  self->stats.us_gl += t4 - t3;
  self->stats.captures++;

  return TRUE;
}

/* Runs the glass pass into a pool slot. Our context must be current.
 *
 * Two mappings:
 *  - the core (default, what the library does): the surface IS the output
 *    texture, and the values the reference read off its FBO size are passed
 *    explicitly (design.md §10.2);
 *  - --reference: the unmodified reference shader, placed in the middle of a
 *    virtual 1920x1080 canvas so its FBO-size-derived values come out as on
 *    a full-monitor FBO (docs/memo.md 地雷3). */
static void
run_glass_pass (S1GlassView           *self,
                PanelState            *ps,
                OutputSlot            *slot,
                const graphene_rect_t *P,
                const graphene_rect_t *O,
                const graphene_rect_t *C,
                double                 scale)
{
  const GlassParams *gp = &self->params;
  gboolean reference = self->opts.use_reference;
  GLuint program = reference ? self->prog_reference : self->prog_glass;
  double canvas_w, canvas_h, off_x, off_y;
  double shadow_room = self->opts.shadow_radius + SHADOW_ROOM_EXTRA;

  if (reference)
    {
      canvas_w = MAX (CANVAS_W, slot->w + 2.0 * CANVAS_EDGE);
      canvas_h = MAX (CANVAS_H, slot->h + 2.0 * CANVAS_EDGE);
      off_x = floor ((canvas_w - slot->w) / 2.0);
      off_y = floor ((canvas_h - slot->h) / 2.0);
    }
  else
    {
      canvas_w = slot->w;
      canvas_h = slot->h;
      off_x = off_y = 0.0;
    }

  if (program != self->prog_current)
    {
      g_hash_table_remove_all (self->glass_uniforms);
      self->prog_current = program;
    }

  glBindFramebuffer (GL_FRAMEBUFFER, slot->fbo);
  glViewport (0, 0, slot->w, slot->h);
  glDisable (GL_BLEND);
  glUseProgram (program);
  glBindVertexArray (self->vao);

  /* Both shaders sample only the blurred backdrop. The reference also
   * declares an unused layer 0, bound to the same texture as the extension
   * does. */
  glActiveTexture (GL_TEXTURE0);
  glBindTexture (GL_TEXTURE_2D, ps->backdrop_tex);
  glActiveTexture (GL_TEXTURE1);
  glBindTexture (GL_TEXTURE_2D, ps->backdrop_tex);
  glUniform1i (glass_uniform (self, "glass_backdrop"), 0);
  glUniform1i (glass_uniform (self, "cogl_sampler1"), 0);
  glUniform1i (glass_uniform (self, "cogl_sampler0"), 1);

  glUniform4f (glass_uniform (self, "u_uv_rect"),
               (float) (off_x / canvas_w), (float) (off_y / canvas_h),
               (float) ((off_x + slot->w) / canvas_w), (float) ((off_y + slot->h) / canvas_h));

  /* Everything below is in device px of the canvas. */
  u1f (self, "resolution_x", canvas_w);
  u1f (self, "resolution_y", canvas_h);
  /* The shape: glass_rect for the core, dock_* (+ padding 0) for the reference. */
  glUniform4f (glass_uniform (self, "glass_rect"),
               (float) (off_x + (P->origin.x - O->origin.x) * scale),
               (float) (off_y + (P->origin.y - O->origin.y) * scale),
               (float) (P->size.width * scale), (float) (P->size.height * scale));
  u1f (self, "dock_x", off_x + (P->origin.x - O->origin.x) * scale);
  u1f (self, "dock_y", off_y + (P->origin.y - O->origin.y) * scale);
  u1f (self, "dock_w", P->size.width * scale);
  u1f (self, "dock_h", P->size.height * scale);
  u1f (self, "padding", 0.0);
  u1f (self, "isDock", 0.0);

  /* The core's explicit replacements for the FBO-size-derived values: the
   * signed-off full-monitor figures (1.2 px, 324 px at 1080p), in device px,
   * the lens at its logical size, no damping at the output's edge. */
  u1f (self, "gradient_step", 1.2 * scale);
  u1f (self, "max_displacement_px", 324.0 * scale);
  u1f (self, "lens_px_scale", scale);
  u1f (self, "edge_damping", 0.0);
  /* Half the sample spacing: 4x RGSS samples sit ~1/4 px apart. */
  u1f (self, "lens_footprint_px",
       !self->opts.measured_footprint ? 0.0 :
       self->opts.footprint_px > 0.0 ? self->opts.footprint_px :
       self->opts.supersample ? 0.25 : 0.5);
  u1f (self, "corner_radius", MIN (P->size.width, P->size.height) / 2.0 * scale);

  u1f (self, "blur_rect_x", off_x + (C->origin.x - O->origin.x) * scale);
  u1f (self, "blur_rect_y", off_y + (C->origin.y - O->origin.y) * scale);
  u1f (self, "blur_rect_w", C->size.width * scale);
  u1f (self, "blur_rect_h", C->size.height * scale);
  u1f (self, "blur_tex_w", ps->tex_w);
  u1f (self, "blur_tex_h", ps->tex_h);

  u1f (self, "max_z", gp->max_z * scale);
  u1f (self, "displacement_scale", gp->displacement_scale * scale);
  u1f (self, "edge_smoothing", gp->edge_smoothing * scale);
  u1f (self, "profile_shape_n", gp->profile_shape_n);
  u1f (self, "ior", gp->ior);
  u1f (self, "chroma_strength", gp->chroma_strength * scale);
  u1f (self, "specular_intensity", gp->specular_intensity);
  u1f (self, "shininess", gp->shininess);
  u1f (self, "rim_width", gp->rim_width * scale);
  u1f (self, "rim_intensity", gp->rim_intensity);
  u1f (self, "rim_directional_power", gp->rim_directional_power);
  u1f (self, "rim_power", gp->rim_power);
  u1f (self, "rim_light_color_intensity", gp->rim_light_color_intensity);
  u1f (self, "sheen_intensity", gp->sheen_intensity);
  u1f (self, "light_angle_deg", gp->light_angle_deg);
  u1f (self, "surface_light_enabled", 1.0);
  u1f (self, "ao_intensity", gp->ao_intensity);
  u1f (self, "ao_radius", gp->ao_radius * scale);
  u1f (self, "shadow_radius", self->opts.shadow_radius * scale);
  u1f (self, "shadow_intensity", self->opts.shadow_intensity);
  u1f (self, "shadow_max_radius", shadow_room * scale);
  u1f (self, "tint_r", gp->tint_r);
  u1f (self, "tint_g", gp->tint_g);
  u1f (self, "tint_b", gp->tint_b);
  u1f (self, "tint_strength", gp->tint_strength);
  u1f (self, "brightness", gp->brightness);
  u1f (self, "contrast", gp->contrast);
  u1f (self, "saturation", gp->saturation);

  /* The same seeds LiquidEffect._init() writes: an unset uniform reads 0.0,
   * which would switch the early exits and the footprint taps off. */
  u1f (self, "early_exit_enabled", 1.0);
  u1f (self, "edge_taps_enabled", 1.0);
  u1f (self, "debug_view", self->opts.debug_view);
  u1f (self, "multi_region_mode", 0.0);
  u1f (self, "region_count", 0.0);
  u1f (self, "panel_bg_a", 0.0);
  u1f (self, "pointer_x", -100.0);
  u1f (self, "pointer_y", -100.0);

  u1f (self, "u_supersample", self->opts.supersample ? 4.0 : 1.0);
  u1f (self, "u_rim_samples", self->opts.rim_samples);
  glUniform2f (glass_uniform (self, "u_pixel_uv"), (float) (1.0 / canvas_w), (float) (1.0 / canvas_h));

  glDrawArrays (GL_TRIANGLE_STRIP, 0, 4);
  glBindFramebuffer (GL_FRAMEBUFFER, 0);
}

static void
render_panel (S1GlassView           *self,
              PanelState            *ps,
              GtkSnapshot           *snapshot,
              GskRenderNode         *backdrop,
              GskRenderNode         *content_leaf,
              float                  leaf_dx,
              float                  leaf_dy,
              const graphene_rect_t *view_rect,
              double                 scale)
{
  double capture_scale = scale / self->opts.downscale;
  double blur_margin = ceil (3.0 * self->opts.blur_radius) + 2.0;
  double shadow_room = self->opts.shadow_radius + SHADOW_ROOM_EXTRA;
  graphene_rect_t P, O, C;
  int o_w, o_h, c_w, c_h;
  gboolean capture_hit;
  OutputSlot *slot;
  GdkGLTextureBuilder *builder;
  ReleaseData *release;
  GdkTexture *texture;
  GLsync sync;
  gint64 t0;

  if (!gtk_widget_get_mapped (ps->widget))
    return;
  /* Read in the snapshot, after this frame's allocation (design §5.3-2). */
  if (!gtk_widget_compute_bounds (ps->widget, GTK_WIDGET (self), &P))
    return;
  if (P.size.width < 1.0f || P.size.height < 1.0f)
    return;

  /* O: the panel plus room for its shadow, the texture handed to GTK. */
  O = P;
  graphene_rect_inset (&O, -shadow_room, -shadow_room);
  if (!graphene_rect_intersection (&O, view_rect, &O))
    return;
  snap_rect (&O, scale, &o_w, &o_h);

  /* C: the panel plus the blur's reach, the part of the backdrop captured.
   * The lens samples inwards up to EDGE_LENS_REACH (96 px) from the rim, which
   * on a panel thinner than that lands past its far side: without this room
   * the rim of a 50 px capsule reads the clamped border of the capture, a
   * smeared strip, instead of what is there. */
  C = P;
  {
    double lens_room = MAX (0.0, LENS_REACH - MIN (P.size.width, P.size.height));

    graphene_rect_inset (&C, -(blur_margin + lens_room), -(blur_margin + lens_room));
  }
  if (!graphene_rect_intersection (&C, view_rect, &C))
    return;
  snap_rect (&C, capture_scale, &c_w, &c_h);
  if (o_w < 1 || o_h < 1 || c_w < 1 || c_h < 1)
    return;

  capture_hit = self->opts.use_cache && ps->capture_valid &&
                content_leaf == ps->key_leaf &&
                leaf_dx == ps->key_dx && leaf_dy == ps->key_dy &&
                graphene_rect_equal (&C, &ps->key_capture) &&
                capture_scale == ps->key_capture_scale &&
                self->blur_gen == ps->key_blur_gen;

  if (capture_hit && ps->last_texture != NULL &&
      graphene_rect_equal (&P, &ps->key_panel) &&
      graphene_rect_equal (&O, &ps->last_out) &&
      scale == ps->key_scale &&
      self->params_gen == ps->key_params_gen)
    {
      gtk_snapshot_append_texture (snapshot, ps->last_texture, &O);
      self->stats.full_hits++;
      return;
    }

  pool_reap (self->pool);

  if (!capture_hit)
    {
      if (!capture_and_blur (self, ps, backdrop, &C, capture_scale, c_w, c_h))
        return;

      if (content_leaf)
        gsk_render_node_ref (content_leaf);
      g_clear_pointer (&ps->key_leaf, gsk_render_node_unref);
      ps->key_leaf = content_leaf;
      ps->key_dx = leaf_dx;
      ps->key_dy = leaf_dy;
      ps->key_capture = C;
      ps->key_capture_scale = capture_scale;
      ps->key_blur_gen = self->blur_gen;
      ps->capture_valid = TRUE;
    }
  else
    self->stats.capture_hits++;

  slot = pool_acquire (self->pool, o_w, o_h);
  if (slot == NULL)
    {
      if (ps->last_texture)
        gtk_snapshot_append_texture (snapshot, ps->last_texture, &ps->last_out);
      self->stats.pool_exhausted++;
      return;
    }

  t0 = g_get_monotonic_time ();
  run_glass_pass (self, ps, slot, &P, &O, &ps->key_capture, scale);

  sync = glFenceSync (GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
  glFlush ();

  release = g_new0 (ReleaseData, 1);
  release->slot = slot;
  release->sync = sync;
  slot->in_use = TRUE;
  g_ref_count_inc (&self->pool->ref);

  builder = gdk_gl_texture_builder_new ();
  gdk_gl_texture_builder_set_context (builder, self->ctx);
  gdk_gl_texture_builder_set_id (builder, slot->tex);
  gdk_gl_texture_builder_set_width (builder, slot->w);
  gdk_gl_texture_builder_set_height (builder, slot->h);
  gdk_gl_texture_builder_set_format (builder, GDK_MEMORY_R8G8B8A8_PREMULTIPLIED);
  gdk_gl_texture_builder_set_sync (builder, sync);
  texture = gdk_gl_texture_builder_build (builder, output_released, release);
  g_object_unref (builder);

  self->stats.us_gl += g_get_monotonic_time () - t0;
  self->stats.glass_passes++;

  gtk_snapshot_append_texture (snapshot, texture, &O);

  g_clear_object (&ps->last_texture);
  ps->last_texture = texture;
  ps->last_out = O;
  ps->key_panel = P;
  ps->key_scale = scale;
  ps->key_params_gen = self->params_gen;
}

static void
s1_glass_view_snapshot (GtkWidget *widget, GtkSnapshot *snapshot)
{
  S1GlassView *self = S1_GLASS_VIEW (widget);
  graphene_rect_t view_rect = GRAPHENE_RECT_INIT (0, 0,
                                                  gtk_widget_get_width (widget),
                                                  gtk_widget_get_height (widget));
  GskRenderNode *content_node = NULL;
  GskRenderNode *nodes[2];
  GskRenderNode *backdrop;
  int n_nodes = 0;

  /* 1. Backdrop colour + content. Transparent parts of the content would
   * otherwise capture as transparent and darken the glass (design §7.3). */
  if (self->content)
    {
      GtkSnapshot *content_snapshot = gtk_snapshot_new ();

      gtk_widget_snapshot_child (widget, self->content, content_snapshot);
      content_node = gtk_snapshot_free_to_node (content_snapshot);
    }

  nodes[n_nodes++] = gsk_color_node_new (&self->backdrop_color, &view_rect);
  if (content_node)
    nodes[n_nodes++] = content_node;
  backdrop = gsk_container_node_new (nodes, n_nodes);
  gtk_snapshot_append_node (snapshot, backdrop);

  self->stats.snapshots++;

  /* 2. The glass bodies. */
  if (self->opts.glass_enabled && self->panels->len > 0 && ensure_gl (self))
    {
      GdkSurface *surface = gtk_native_get_surface (gtk_widget_get_native (widget));
      double scale = gdk_surface_get_scale (surface);
      gint64 t0 = g_get_monotonic_time ();
      gint64 elapsed;
      GskRenderNode *leaf;
      float dx, dy;

      leaf = unwrap_node (content_node, &dx, &dy);
      gdk_gl_context_make_current (self->ctx);

      for (guint i = 0; i < self->panels->len; i++)
        render_panel (self, g_ptr_array_index (self->panels, i), snapshot,
                      backdrop, leaf, dx, dy, &view_rect, scale);

      elapsed = g_get_monotonic_time () - t0;
      self->stats.us_total += elapsed;
      self->stats.us_total_max = MAX (self->stats.us_total_max, elapsed);
    }

  /* 3. Overlay children: the panels' foreground. */
  for (guint i = 0; i < self->overlays->len; i++)
    gtk_widget_snapshot_child (widget, g_ptr_array_index (self->overlays, i), snapshot);

  gsk_render_node_unref (nodes[0]);
  if (content_node)
    gsk_render_node_unref (content_node);
  gsk_render_node_unref (backdrop);
}

static void
s1_glass_view_measure (GtkWidget      *widget,
                       GtkOrientation  orientation,
                       int             for_size,
                       int            *minimum,
                       int            *natural,
                       int            *minimum_baseline,
                       int            *natural_baseline)
{
  S1GlassView *self = S1_GLASS_VIEW (widget);

  *minimum = *natural = 0;
  if (self->content && gtk_widget_should_layout (self->content))
    gtk_widget_measure (self->content, orientation, for_size, minimum, natural, NULL, NULL);
}

/* GtkOverlay's rule: the content fills the view, and each overlay child is
 * given the whole view and places itself by its own halign / valign /
 * margins (gtk_widget_allocate() applies them). */
static void
s1_glass_view_size_allocate (GtkWidget *widget, int width, int height, int baseline)
{
  S1GlassView *self = S1_GLASS_VIEW (widget);

  if (self->content && gtk_widget_should_layout (self->content))
    gtk_widget_allocate (self->content, width, height, baseline, NULL);

  for (guint i = 0; i < self->overlays->len; i++)
    {
      GtkWidget *child = g_ptr_array_index (self->overlays, i);

      if (gtk_widget_should_layout (child))
        gtk_widget_allocate (child, width, height, -1, NULL);
    }
}

static void
s1_glass_view_unrealize (GtkWidget *widget)
{
  release_gl (S1_GLASS_VIEW (widget));
  GTK_WIDGET_CLASS (s1_glass_view_parent_class)->unrealize (widget);
}

static void
s1_glass_view_dispose (GObject *object)
{
  S1GlassView *self = S1_GLASS_VIEW (object);

  release_gl (self);
  g_ptr_array_set_size (self->panels, 0);
  g_clear_pointer (&self->content, gtk_widget_unparent);
  for (guint i = 0; i < self->overlays->len; i++)
    gtk_widget_unparent (g_ptr_array_index (self->overlays, i));
  g_ptr_array_set_size (self->overlays, 0);

  G_OBJECT_CLASS (s1_glass_view_parent_class)->dispose (object);
}

static void
s1_glass_view_finalize (GObject *object)
{
  S1GlassView *self = S1_GLASS_VIEW (object);

  g_ptr_array_unref (self->panels);
  g_ptr_array_unref (self->overlays);
  g_free (self->gl_info);

  G_OBJECT_CLASS (s1_glass_view_parent_class)->finalize (object);
}

static void
s1_glass_view_class_init (S1GlassViewClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = s1_glass_view_dispose;
  object_class->finalize = s1_glass_view_finalize;

  widget_class->snapshot = s1_glass_view_snapshot;
  widget_class->measure = s1_glass_view_measure;
  widget_class->size_allocate = s1_glass_view_size_allocate;
  widget_class->unrealize = s1_glass_view_unrealize;

  gtk_widget_class_set_css_name (widget_class, "glassview");
}

static void
s1_glass_view_init (S1GlassView *self)
{
  self->overlays = g_ptr_array_new ();
  self->panels = g_ptr_array_new_with_free_func ((GDestroyNotify) panel_state_free);
  gdk_rgba_parse (&self->backdrop_color, "#eeeeec");

  self->opts.glass_enabled = TRUE;
  self->opts.use_cache = TRUE;
  self->opts.private_renderer = FALSE;
  self->opts.blur_radius = 2.0;   /* in-app (user, 2026-09-25); the extension's dock uses 5 */
  self->opts.downscale = 2;
  self->opts.debug_view = 0;
  self->opts.supersample = TRUE;
  self->opts.measured_footprint = TRUE;
  self->params = default_params;
  self->opts.shadow_radius = IN_APP_SHADOW_RADIUS;
  self->opts.shadow_intensity = IN_APP_SHADOW_INTENSITY;

  gtk_widget_set_overflow (GTK_WIDGET (self), GTK_OVERFLOW_HIDDEN);
  s1_glass_view_options_changed (self);
}

GtkWidget *
s1_glass_view_new (void)
{
  return g_object_new (S1_TYPE_GLASS_VIEW, NULL);
}

void
s1_glass_view_set_content (S1GlassView *self, GtkWidget *content)
{
  g_clear_pointer (&self->content, gtk_widget_unparent);
  if (content)
    {
      self->content = content;
      gtk_widget_insert_after (content, GTK_WIDGET (self), NULL);
    }
}

void
s1_glass_view_add_panel (S1GlassView *self, GtkWidget *panel)
{
  PanelState *ps = g_new0 (PanelState, 1);

  ps->widget = panel;
  g_ptr_array_add (self->overlays, panel);
  g_ptr_array_add (self->panels, ps);
  gtk_widget_set_parent (panel, GTK_WIDGET (self));
}

S1Options *
s1_glass_view_get_options (S1GlassView *self)
{
  return &self->opts;
}

void
s1_glass_view_options_changed (S1GlassView *self)
{
  self->opts.downscale = self->opts.downscale >= 4 ? 4 : 2;
  self->have_blur = s1_gauss_kernel_for_radius (self->opts.blur_radius, self->opts.downscale, &self->kernel);
  self->blur_gen++;
  self->params_gen++;
  gtk_widget_queue_draw (GTK_WIDGET (self));
}

void
s1_glass_view_take_stats (S1GlassView *self, S1Stats *out)
{
  *out = self->stats;
  memset (&self->stats, 0, sizeof self->stats);
}

gboolean
s1_glass_view_set_param (S1GlassView *self, const char *name, double value)
{
  for (guint i = 0; i < G_N_ELEMENTS (param_fields); i++)
    if (g_str_equal (param_fields[i].name, name))
      {
        G_STRUCT_MEMBER (double, &self->params, param_fields[i].offset) = value;
        self->params_gen++;
        gtk_widget_queue_draw (GTK_WIDGET (self));
        return TRUE;
      }
  return FALSE;
}

double
s1_glass_view_get_param (S1GlassView *self, const char *name)
{
  for (guint i = 0; i < G_N_ELEMENTS (param_fields); i++)
    if (g_str_equal (param_fields[i].name, name))
      return G_STRUCT_MEMBER (double, &self->params, param_fields[i].offset);
  return NAN;
}

gboolean
s1_glass_view_apply_preset (S1GlassView *self, const char *name)
{
  for (guint i = 0; i < G_N_ELEMENTS (edge_presets); i++)
    if (g_str_equal (edge_presets[i].name, name))
      {
        g_auto (GStrv) pairs = g_strsplit (edge_presets[i].params, ",", -1);

        self->params = default_params;
        for (int j = 0; pairs[j] && *pairs[j]; j++)
          {
            g_auto (GStrv) kv = g_strsplit (pairs[j], "=", 2);
            s1_glass_view_set_param (self, kv[0], g_ascii_strtod (kv[1], NULL));
          }
        self->params_gen++;
        gtk_widget_queue_draw (GTK_WIDGET (self));
        return TRUE;
      }
  return FALSE;
}

const char *
s1_glass_view_next_preset (const char *name)
{
  for (guint i = 0; i < G_N_ELEMENTS (edge_presets); i++)
    if (g_str_equal (edge_presets[i].name, name))
      return edge_presets[(i + 1) % G_N_ELEMENTS (edge_presets)].name;
  return edge_presets[0].name;
}

void
s1_glass_view_reset_params (S1GlassView *self)
{
  self->params = default_params;
  self->params_gen++;
  gtk_widget_queue_draw (GTK_WIDGET (self));
}

const char *
s1_glass_view_get_gl_info (S1GlassView *self)
{
  return self->gl_info;
}
