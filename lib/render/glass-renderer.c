/* glass-renderer.c — the full renderer (design.md §8), from the S1 spike.
 *
 * Inside GlassView's snapshot:
 *
 *   per capture (panels next to each other share one):
 *     capture   gsk_renderer_render_texture() of the backdrop under them, at
 *               1/blur-downscale of the surface scale
 *     download  GdkTextureDownloader            (no public zero-copy path);
 *               the adaptive statistics are taken from these pixels
 *     upload    glTexSubImage2D into our own GL context
 *     blur      the extension's Gaussian, H then V (separate targets: a DAG)
 *   per panel:
 *     glass     the shader core (shaders/core) with 4x supersampling
 *     hand-off  GdkGLTextureBuilder -> the view appends the texture
 *
 * Two caches (design.md §7.5): a capture and its blur are reused while the
 * content node, the captured rectangle and the blur are the same; a panel's
 * finished texture is reused while, in addition, everything its glass pass
 * reads is.
 *
 * SPDX-License-Identifier: MIT
 */
#include "glass-renderer.h"
#include "glass-gl.h"
#include "glass-private.h"

#include <math.h>
#include <string.h>

#define OUTPUT_POOL_SIZE 4

/* Room kept around the panel for the drop shadow, on top of shadow_radius:
 * the shader's boundsMask starts fading at 85% of shadow_max_radius; with this
 * margin that point lies past the penumbra, so the room never clips it. */
#define SHADOW_ROOM_EXTRA 8.0

/* Room for the antialiased silhouette when there is no shadow. */
#define EDGE_ROOM 2.0

/* glass_params.glsl's EDGE_LENS_REACH, logical px. The lens samples up to
 * this far inwards from the rim, which on a panel thinner than that lands
 * past its far side (docs/memo.md 地雷10). */
#define LENS_REACH 96.0

/* ── Output textures ───────────────────────────────────────────────────────
 * GTK holds the texture of the last frame (and possibly the one before)
 * while we draw the next; each panel cycles through a few. A slot comes back
 * when GTK drops the GdkGLTexture. The release callback can run while GTK's
 * own GL renderer is drawing with ITS context current, so it makes no GL
 * calls (docs/memo.md 地雷2): the sync object is deleted on the next frame.
 * Only after the panel went away ("dead") does it switch contexts to free
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

/* A blur program and its uniforms' locations. */
typedef struct {
  GLuint program;
  GLint  src, inv_size, kernel_scale, uv_rect;
} BlurPass;

typedef struct {
  BlurPass h, v;
} BlurPrograms;

struct _GlassRenderer {
  grefcount         ref;
  GtkNative        *native;          /* weak */
  GdkGLContext     *ctx;
  gboolean          use_es;
  char             *info;
  char             *vertex_source;
  GLuint            vao;
  GLuint            prog_glass;
  GLuint            prog_copy;       /* one texture, as is (compositing layers) */
  GLint             copy_src, copy_uv_rect;
  /* The glass program's uniforms, by the address of their (static) names:
   * a pass sets some 70, and hashing each string every time added up. */
  GHashTable       *uniforms;        /* static name -> location + 1 */
  GHashTable       *blur_programs;   /* sigma * 1000 -> BlurPrograms* */
  GskRenderer      *capture_renderer; /* a private GL renderer, or NULL: the window's */
  gboolean          capture_renderer_tried;
  guint8           *atlas;           /* the last download, all regions */
  gsize             atlas_size;
  GlassRenderStats  stats;
};

struct _GlassCapture {
  GLuint          capture_tex, capture_fbo;
  GLuint          blur_h_tex, blur_h_fbo;
  GLuint          blur_v_tex, blur_v_fbo;
  int             tex_w, tex_h;
  GLuint          backdrop_tex;      /* what the passes sample */
  guint8         *pixels;            /* the last download, for the adaptive colours */
  gsize           pixels_size;
  gboolean        fresh;             /* captured anew by the last call */

  /* cache key */
  gboolean        valid;
  GskRenderNode  *key_leaf;
  float           key_dx, key_dy;
  guint           key_extra;
  graphene_rect_t rect;              /* C, snapped to the capture's pixels */
  double          capture_scale;
  double          blur_radius;
  int             downscale;
  guint           gen;               /* bumped on every new capture */
  guint64         compose_key;       /* glass_renderer_compose(): what it was made of */
};

typedef struct {
  graphene_rect_t P, O, C;
  double          scale;
  double          radius;
  gboolean        has_corner_radii;
  double          corner_radii[4];
  double          params[GLASS_N_PARAMS];
  float           tint[4];
  gboolean        has_shadow;
  gconstpointer   capture;
  guint           capture_gen;
  guint           n_shapes;
  graphene_rect_t shapes[GLASS_MAX_SHAPES];
  float           radii[GLASS_MAX_SHAPES];
  float           merge_k;
  gboolean        supersample;
  gboolean        measured_footprint;
} PassKey;

struct _GlassPanelRender {
  OutputPool     *pool;
  GdkTexture     *last_texture;
  gboolean        pass_valid;
  PassKey         pass_key;
  guint64         output_gen;        /* bumped on every new output texture */
  GLuint          output_tex;        /* last_texture's GL name (its slot stays in use while we hold it) */
};

/* ── GPU timing (GLASS_DEBUG=gpu-time) ─────────────────────────────────────
 * EXT_disjoint_timer_query around each pass; results are read back, without
 * waiting, a few frames later and summed per kind and size. Measurement
 * only: it is off unless asked for. */

typedef struct {
  GLuint query;
  char   label[48];
} GpuTimer;

#define GPU_TIMERS 64

static GpuTimer gpu_timers[GPU_TIMERS];
static guint gpu_timer_head, gpu_timer_tail;
static GHashTable *gpu_totals;            /* label -> gint64[2] (ns, count) */
static gint64 gpu_last_report;

static gboolean
gpu_timing (void)
{
  static int enabled = -1;

  if (enabled < 0)
    enabled = (glass_get_debug_flags () & GLASS_DEBUG_GPU_TIME) &&
              epoxy_has_gl_extension ("GL_EXT_disjoint_timer_query");
  return enabled;
}

static void
gpu_collect (void)
{
  while (gpu_timer_tail != gpu_timer_head)
    {
      GpuTimer *t = &gpu_timers[gpu_timer_tail % GPU_TIMERS];
      GLuint available = 0;
      GLuint64 ns = 0;
      gint64 *total;

      glGetQueryObjectuivEXT (t->query, GL_QUERY_RESULT_AVAILABLE_EXT, &available);
      if (!available)
        break;
      glGetQueryObjectui64vEXT (t->query, GL_QUERY_RESULT_EXT, &ns);
      if (gpu_totals == NULL)
        gpu_totals = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
      total = g_hash_table_lookup (gpu_totals, t->label);
      if (total == NULL)
        {
          total = g_new0 (gint64, 2);
          g_hash_table_insert (gpu_totals, g_strdup (t->label), total);
        }
      total[0] += (gint64) ns;
      total[1]++;
      gpu_timer_tail++;
    }

  if (gpu_totals && g_get_monotonic_time () - gpu_last_report > 2 * G_USEC_PER_SEC)
    {
      GHashTableIter iter;
      gpointer key, value;
      GString *report = g_string_new ("gpu (ms per second of wall time):");
      double secs = gpu_last_report ? (g_get_monotonic_time () - gpu_last_report) / (double) G_USEC_PER_SEC : 2.0;

      g_hash_table_iter_init (&iter, gpu_totals);
      while (g_hash_table_iter_next (&iter, &key, &value))
        {
          gint64 *total = value;

          g_string_append_printf (report, "\n  %-24s %7.2f ms/s  (%5.0f/s, %.3f ms each)", (char *) key,
                                  total[0] / 1e6 / secs, total[1] / secs, total[0] / 1e6 / MAX (total[1], 1));
        }
      g_debug ("%s", report->str);
      g_string_free (report, TRUE);
      g_hash_table_remove_all (gpu_totals);
      gpu_last_report = g_get_monotonic_time ();
    }
}

static void
gpu_begin (const char *kind, int w, int h)
{
  GpuTimer *t;

  if (!gpu_timing ())
    return;
  gpu_collect ();
  if (gpu_timer_head - gpu_timer_tail >= GPU_TIMERS)
    return;
  t = &gpu_timers[gpu_timer_head % GPU_TIMERS];
  if (t->query == 0)
    glGenQueriesEXT (1, &t->query);
  g_snprintf (t->label, sizeof t->label, "%s %dx%d", kind, w, h);
  glBeginQueryEXT (GL_TIME_ELAPSED_EXT, t->query);
  gpu_timer_head++;
}

static void
gpu_end (void)
{
  if (gpu_timing ())
    glEndQueryEXT (GL_TIME_ELAPSED_EXT);
}

/* ── Output pool ──────────────────────────────────────────────────────────── */

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

/* Our context must be current. Frees what GTK no longer holds; the rest is
 * freed when GTK releases it. */
static void
pool_kill (OutputPool *pool)
{
  for (int i = 0; i < OUTPUT_POOL_SIZE; i++)
    if (!pool->slots[i].in_use)
      slot_delete_gl (&pool->slots[i]);
  pool->dead = TRUE;
  pool_unref (pool);
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
    g_warning ("framebuffer incomplete");
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

/* ── Panel render state and captures ────────────────────────────────────── */

GlassPanelRender *
glass_panel_render_new (void)
{
  return g_new0 (GlassPanelRender, 1);
}

void
glass_panel_render_release_gl (GlassPanelRender *pr)
{
  pr->pass_valid = FALSE;
  g_clear_object (&pr->last_texture);
  if (pr->pool)
    {
      pool_kill (pr->pool);
      pr->pool = NULL;
    }
}

void
glass_panel_render_free (GlassPanelRender *pr,
                         GlassRenderer    *renderer)
{
  if (pr == NULL)
    return;

  if (renderer && pr->pool)
    {
      glass_renderer_make_current (renderer);
      glass_panel_render_release_gl (pr);
    }
  g_clear_object (&pr->last_texture);
  g_free (pr);
}

GlassCapture *
glass_capture_new (void)
{
  return g_new0 (GlassCapture, 1);
}

void
glass_capture_release_gl (GlassCapture *cap)
{
  GLuint textures[3] = { cap->capture_tex, cap->blur_h_tex, cap->blur_v_tex };
  GLuint fbos[3] = { cap->capture_fbo, cap->blur_h_fbo, cap->blur_v_fbo };

  glDeleteTextures (3, textures);
  glDeleteFramebuffers (3, fbos);
  cap->capture_tex = cap->blur_h_tex = cap->blur_v_tex = 0;
  cap->capture_fbo = cap->blur_h_fbo = cap->blur_v_fbo = 0;
  cap->backdrop_tex = 0;
  cap->tex_w = cap->tex_h = 0;
  cap->valid = FALSE;
  g_clear_pointer (&cap->key_leaf, gsk_render_node_unref);
}

void
glass_capture_free (GlassCapture  *cap,
                    GlassRenderer *renderer)
{
  if (cap == NULL)
    return;

  if (renderer && cap->capture_tex)
    {
      glass_renderer_make_current (renderer);
      glass_capture_release_gl (cap);
    }
  g_clear_pointer (&cap->key_leaf, gsk_render_node_unref);
  g_free (cap->pixels);
  g_free (cap);
}

gboolean
glass_capture_is_fresh (GlassCapture *cap)
{
  return cap->fresh;
}


/* ── Renderer ─────────────────────────────────────────────────────────────── */

static void
blur_programs_free (gpointer data)
{
  BlurPrograms *bp = data;

  /* The context is current when the table is destroyed. */
  if (bp->h.program)
    glDeleteProgram (bp->h.program);
  if (bp->v.program)
    glDeleteProgram (bp->v.program);
  g_free (bp);
}

static GlassRenderer *
renderer_new (GtkNative *native)
{
  g_autoptr (GError) error = NULL;
  g_autofree char *core_src = NULL;
  GdkSurface *surface = gtk_native_get_surface (native);
  GlassRenderer *self;
  GdkGLContext *ctx;
  int major = 0, minor = 0;
  gboolean es;

  if (surface == NULL)
    return NULL;

  ctx = gdk_surface_create_gl_context (surface, &error);
  if (ctx == NULL || !gdk_gl_context_realize (ctx, &error))
    {
      g_warning ("no GL context, glass falls back to CSS: %s", error->message);
      g_clear_object (&ctx);
      return NULL;
    }

  gdk_gl_context_make_current (ctx);
  gdk_gl_context_get_version (ctx, &major, &minor);
  es = gdk_gl_context_get_use_es (ctx);

  if (es ? major < 3 : major * 10 + minor < 33)
    {
      g_warning ("glass needs OpenGL ES 3.0 or OpenGL 3.3, got %s %d.%d; falling back to CSS",
                 es ? "OpenGL ES" : "OpenGL", major, minor);
      gdk_gl_context_clear_current ();
      g_object_unref (ctx);
      return NULL;
    }

  self = g_new0 (GlassRenderer, 1);
  g_ref_count_init (&self->ref);
  self->ctx = ctx;
  self->use_es = es;
  self->vertex_source = glass_gl_load_shader ("quad.vert");
  core_src = glass_gl_load_shader ("glass-core.frag");

  if (self->vertex_source && core_src)
    {
      const char *parts[] = { core_src, NULL };

      self->prog_glass = glass_gl_program_new (es, self->vertex_source, parts, &error);
    }
  if (self->prog_glass == 0)
    {
      g_warning ("the glass shader did not build, glass falls back to CSS: %s",
                 error ? error->message : "shader resources missing");
      g_free (self->vertex_source);
      g_free (self);
      gdk_gl_context_clear_current ();
      g_object_unref (ctx);
      return NULL;
    }

  {
    static const char copy_src[] =
      "uniform sampler2D u_src;\n"
      "in vec4 v_tex_coord;\n"
      "out vec4 frag_color;\n"
      "void main() { frag_color = texture(u_src, v_tex_coord.st); }\n";
    const char *copy_parts[] = { copy_src, NULL };

    self->prog_copy = glass_gl_program_new (es, self->vertex_source, copy_parts, NULL);
    if (self->prog_copy)
      {
        self->copy_src = glGetUniformLocation (self->prog_copy, "u_src");
        self->copy_uv_rect = glGetUniformLocation (self->prog_copy, "u_uv_rect");
      }
  }

  glGenVertexArrays (1, &self->vao);
  self->uniforms = g_hash_table_new (g_direct_hash, g_direct_equal);
  self->blur_programs = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, blur_programs_free);
  self->native = native;
  self->info = g_strdup_printf ("%s %d.%d, %s", es ? "OpenGL ES" : "OpenGL", major, minor,
                                (const char *) glGetString (GL_RENDERER));
  g_debug ("full renderer: %s", self->info);

  return self;
}

static void
renderer_destroy (GlassRenderer *self)
{
  if (self->capture_renderer)
    {
      gsk_renderer_unrealize (self->capture_renderer);
      g_clear_object (&self->capture_renderer);
    }
  g_free (self->atlas);
  gdk_gl_context_make_current (self->ctx);
  g_hash_table_destroy (self->blur_programs);
  if (self->prog_glass)
    glDeleteProgram (self->prog_glass);
  if (self->prog_copy)
    glDeleteProgram (self->prog_copy);
  if (self->vao)
    glDeleteVertexArrays (1, &self->vao);
  g_hash_table_destroy (self->uniforms);
  gdk_gl_context_clear_current ();
  g_object_unref (self->ctx);
  g_free (self->vertex_source);
  g_free (self->info);
  g_free (self);
}

#define RENDERER_KEY "glass-renderer"
#define RENDERER_FAILED_KEY "glass-renderer-failed"

GlassRenderer *
glass_renderer_acquire (GtkNative *native)
{
  GlassRenderer *self = g_object_get_data (G_OBJECT (native), RENDERER_KEY);

  if (self)
    {
      g_ref_count_inc (&self->ref);
      return self;
    }
  if (glass_renderer_failed (native))
    return NULL;

  self = renderer_new (native);
  if (self == NULL)
    {
      g_object_set_data (G_OBJECT (native), RENDERER_FAILED_KEY, GINT_TO_POINTER (1));
      return NULL;
    }
  g_object_set_data (G_OBJECT (native), RENDERER_KEY, self);
  return self;
}

void
glass_renderer_release (GlassRenderer *self)
{
  if (self == NULL || !g_ref_count_dec (&self->ref))
    return;

  if (self->native)
    g_object_set_data (G_OBJECT (self->native), RENDERER_KEY, NULL);
  renderer_destroy (self);
}

gboolean
glass_renderer_failed (GtkNative *native)
{
  return g_object_get_data (G_OBJECT (native), RENDERER_FAILED_KEY) != NULL;
}

void
glass_renderer_make_current (GlassRenderer *self)
{
  gdk_gl_context_make_current (self->ctx);
}

const char *
glass_renderer_get_info (GlassRenderer *self)
{
  return self->info;
}

GlassRenderStats *
glass_renderer_get_stats (GlassRenderer *self)
{
  return &self->stats;
}

static GLint
uniform (GlassRenderer *self, const char *name)
{
  gpointer value;
  GLint location;

  if (g_hash_table_lookup_extended (self->uniforms, name, NULL, &value))
    return GPOINTER_TO_INT (value) - 1;

  location = glGetUniformLocation (self->prog_glass, name);
  g_hash_table_insert (self->uniforms, (gpointer) name, GINT_TO_POINTER (location + 1));

  return location;
}

static void
u1f (GlassRenderer *self, const char *name, double value)
{
  GLint location = uniform (self, name);

  if (location >= 0)
    glUniform1f (location, (float) value);
}

static void
blur_pass_locate (BlurPass *pass)
{
  pass->src = glGetUniformLocation (pass->program, "u_src");
  pass->inv_size = glGetUniformLocation (pass->program, "inv_size");
  pass->kernel_scale = glGetUniformLocation (pass->program, "kernel_scale");
  pass->uv_rect = glGetUniformLocation (pass->program, "u_uv_rect");
}

/* Our context must be current. The weights are baked into the program, so
 * one program pair per sigma (materials differ). */
static BlurPrograms *
blur_programs (GlassRenderer *self, const GlassGaussKernel *kernel)
{
  gpointer key = GINT_TO_POINTER ((int) lround (kernel->sigma * 1000.0));
  BlurPrograms *bp = g_hash_table_lookup (self->blur_programs, key);
  g_autoptr (GError) error = NULL;
  g_autofree char *h_src = NULL;
  g_autofree char *v_src = NULL;

  if (bp)
    return bp->v.program ? bp : NULL;

  bp = g_new0 (BlurPrograms, 1);
  g_hash_table_insert (self->blur_programs, key, bp);

  h_src = glass_gauss_fragment_source (kernel, TRUE);
  v_src = glass_gauss_fragment_source (kernel, FALSE);
  {
    const char *h_parts[] = { h_src, NULL };
    const char *v_parts[] = { v_src, NULL };

    bp->h.program = glass_gl_program_new (self->use_es, self->vertex_source, h_parts, &error);
    if (bp->h.program)
      bp->v.program = glass_gl_program_new (self->use_es, self->vertex_source, v_parts, &error);
  }
  if (bp->v.program == 0)
    {
      g_warning ("the blur shader did not build, glass is drawn unblurred: %s", error->message);
      return NULL;
    }
  blur_pass_locate (&bp->h);
  blur_pass_locate (&bp->v);

  return bp;
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

GskRenderNode *
glass_unwrap_node (GskRenderNode *node, float *dx, float *dy)
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
run_blur_pass (GlassRenderer *self, const BlurPass *pass, double kernel_scale,
               GLuint src, GLuint dst_fbo, int w, int h)
{
  glBindFramebuffer (GL_FRAMEBUFFER, dst_fbo);
  glViewport (0, 0, w, h);
  glUseProgram (pass->program);
  glActiveTexture (GL_TEXTURE0);
  glBindTexture (GL_TEXTURE_2D, src);
  glUniform1i (pass->src, 0);
  glUniform2f (pass->inv_size, 1.0f / w, 1.0f / h);
  glUniform1f (pass->kernel_scale, (float) kernel_scale);
  glUniform4f (pass->uv_rect, 0.0f, 0.0f, 1.0f, 1.0f);
  glDrawArrays (GL_TRIANGLE_STRIP, 0, 4);
}

gboolean
glass_capture_rect_for_panel (const graphene_rect_t *panel,
                              double                 blur_radius,
                              const graphene_rect_t *view_rect,
                              graphene_rect_t       *out)
{
  double blur_margin = ceil (3.0 * blur_radius) + 2.0;
  double lens_room = MAX (0.0, LENS_REACH - MIN (panel->size.width, panel->size.height));

  *out = *panel;
  graphene_rect_inset (out, -(blur_margin + lens_room), -(blur_margin + lens_room));
  return graphene_rect_intersection (out, view_rect, out);
}

/* The renderer that draws the captures. gsk_renderer_render_texture() has a
 * fixed cost that barely depends on the size (docs/memo.md 追記7): about
 * 0.25-0.5 ms with the Vulkan renderer GTK uses by default, 0.15 ms with
 * its GL renderer. So captures go through a private GL renderer when the
 * window's is not GL already. Its render_texture() still hands back a
 * dmabuf, so the download stays; only the fixed cost goes down. */
static GskRenderer *
capture_renderer (GlassRenderer *self)
{
  GskRenderer *window_renderer = gtk_native_get_renderer (self->native);
  g_autoptr (GError) error = NULL;
  GskRenderer *renderer;

  if (self->capture_renderer)
    return self->capture_renderer;
  if (self->capture_renderer_tried || GSK_IS_GL_RENDERER (window_renderer) ||
      (glass_get_debug_flags () & GLASS_DEBUG_WINDOW_CAPTURE))
    return window_renderer;
  self->capture_renderer_tried = TRUE;

  renderer = gsk_gl_renderer_new ();
  if (!gsk_renderer_realize_for_display (renderer, gtk_widget_get_display (GTK_WIDGET (self->native)), &error))
    {
      g_debug ("no private GL renderer for captures, using the window's: %s", error->message);
      g_object_unref (renderer);
      return window_renderer;
    }
  self->capture_renderer = renderer;
  return renderer;
}

static void
ensure_textures (GlassCapture *cap, int w, int h)
{
  if (cap->tex_w == w && cap->tex_h == h)
    return;

  alloc_texture (&cap->capture_tex, w, h);
  alloc_texture (&cap->blur_h_tex, w, h);
  alloc_texture (&cap->blur_v_tex, w, h);
  attach_fbo (&cap->capture_fbo, cap->capture_tex);
  attach_fbo (&cap->blur_h_fbo, cap->blur_h_tex);
  attach_fbo (&cap->blur_v_fbo, cap->blur_v_tex);
  cap->tex_w = w;
  cap->tex_h = h;
}

/* Blurs capture_tex into backdrop_tex. Our context must be current. */
static void
blur (GlassRenderer *self,
      GlassCapture  *cap,
      int            w,
      int            h,
      double         blur_radius_px,
      int            downscale)
{
  GlassGaussKernel kernel;

  gpu_begin ("blur", w, h);
  cap->backdrop_tex = cap->capture_tex;
  if (glass_gauss_kernel_for_radius (blur_radius_px, downscale, &kernel))
    {
      BlurPrograms *bp = blur_programs (self, &kernel);

      if (bp)
        {
          glBindVertexArray (self->vao);
          glDisable (GL_BLEND);
          run_blur_pass (self, &bp->h, kernel.scale, cap->capture_tex, cap->blur_h_fbo, w, h);
          run_blur_pass (self, &bp->v, kernel.scale, cap->blur_h_tex, cap->blur_v_fbo, w, h);
          glBindFramebuffer (GL_FRAMEBUFFER, 0);
          cap->backdrop_tex = cap->blur_v_tex;
        }
    }
  gpu_end ();
}

/* Uploads cap->pixels and blurs them. Our context must be current. */
static void
upload_and_blur (GlassRenderer *self,
                 GlassCapture  *cap,
                 int            w,
                 int            h,
                 double         blur_radius_px,
                 int            downscale)
{
  ensure_textures (cap, w, h);
  glPixelStorei (GL_UNPACK_ALIGNMENT, 4);
  glPixelStorei (GL_UNPACK_ROW_LENGTH, 0);
  glBindTexture (GL_TEXTURE_2D, cap->capture_tex);
  glTexSubImage2D (GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, cap->pixels);
  blur (self, cap, w, h, blur_radius_px, downscale);
}

typedef struct {
  GlassCapture    *cap;
  const GlassCaptureRequest *req;
  graphene_rect_t  C;           /* snapped */
  double           capture_scale;
  int              downscale;
  int              w, h;
  int              y;           /* row in the atlas */
} Region;

#define ATLAS_GAP 2

gboolean
glass_renderer_capture_all (GlassRenderer             *self,
                            GlassCapture             **caps,
                            const GlassCaptureRequest *reqs,
                            guint                      n)
{
  g_autoptr (GArray) regions = g_array_new (FALSE, TRUE, sizeof (Region));
  gboolean no_cache = (glass_get_debug_flags () & GLASS_DEBUG_NO_CACHE) != 0;
  GskRenderNode **nodes;
  GskRenderNode *atlas_node;
  GdkTextureDownloader *downloader;
  GdkTexture *texture;
  int atlas_w = 0, atlas_h = 0;
  gsize stride;
  gint64 t0, t1, t2, t3;

  /* 1. Which captures are stale. */
  for (guint i = 0; i < n; i++)
    {
      GlassCapture *cap = caps[i];
      const GlassCaptureRequest *req = &reqs[i];
      Region r = { .cap = cap, .req = req };

      cap->fresh = FALSE;
      r.downscale = req->downscale >= 4 ? 4 : req->downscale >= 2 ? 2 : 1;
      r.capture_scale = req->scale / r.downscale;
      r.C = req->rect;
      snap_rect (&r.C, r.capture_scale, &r.w, &r.h);
      if (r.w < 1 || r.h < 1)
        {
          cap->valid = FALSE;
          continue;
        }

      if (!no_cache && cap->valid &&
          req->content_key == cap->key_leaf &&
          req->key_dx == cap->key_dx && req->key_dy == cap->key_dy &&
          req->key_extra == cap->key_extra &&
          graphene_rect_equal (&r.C, &cap->rect) &&
          r.capture_scale == cap->capture_scale &&
          req->blur_radius == cap->blur_radius &&
          r.downscale == cap->downscale)
        {
          self->stats.capture_hits++;
          continue;
        }

      r.y = atlas_h;
      atlas_h += r.h + ATLAS_GAP;
      atlas_w = MAX (atlas_w, r.w);
      g_array_append_val (regions, r);
    }

  if (regions->len == 0)
    return TRUE;

  /* 2. One render of every stale region, stacked in an atlas: the fixed
   * cost of render_texture() is paid once per view and frame, not once per
   * group of panels (docs/memo.md 追記7). */
  nodes = g_new (GskRenderNode *, regions->len);
  for (guint i = 0; i < regions->len; i++)
    {
      Region *r = &g_array_index (regions, Region, i);
      GskRenderNode *clip = gsk_clip_node_new (r->req->backdrop, &r->C);
      GskTransform *transform = gsk_transform_translate (NULL, &GRAPHENE_POINT_INIT (0, r->y));

      transform = gsk_transform_scale (transform, r->capture_scale, r->capture_scale);
      transform = gsk_transform_translate (transform, &GRAPHENE_POINT_INIT (-r->C.origin.x, -r->C.origin.y));
      nodes[i] = gsk_transform_node_new (clip, transform);
      gsk_transform_unref (transform);
      gsk_render_node_unref (clip);
    }
  atlas_node = gsk_container_node_new (nodes, regions->len);
  for (guint i = 0; i < regions->len; i++)
    gsk_render_node_unref (nodes[i]);
  g_free (nodes);

  t0 = g_get_monotonic_time ();
  texture = gsk_renderer_render_texture (capture_renderer (self), atlas_node,
                                         &GRAPHENE_RECT_INIT (0, 0, atlas_w, atlas_h));
  t1 = g_get_monotonic_time ();
  gsk_render_node_unref (atlas_node);

  if (texture == NULL)
    {
      gdk_gl_context_make_current (self->ctx);
      for (guint i = 0; i < regions->len; i++)
        g_array_index (regions, Region, i).cap->valid = FALSE;
      return FALSE;
    }

  atlas_w = gdk_texture_get_width (texture);
  atlas_h = gdk_texture_get_height (texture);
  stride = (gsize) atlas_w * 4;
  if (self->atlas_size < stride * atlas_h)
    {
      g_free (self->atlas);
      self->atlas_size = stride * atlas_h;
      self->atlas = g_malloc (self->atlas_size);
    }
  downloader = gdk_texture_downloader_new (texture);
  gdk_texture_downloader_set_format (downloader, GDK_MEMORY_R8G8B8A8_PREMULTIPLIED);
  gdk_texture_downloader_download_into (downloader, self->atlas, stride);
  gdk_texture_downloader_free (downloader);
  g_object_unref (texture);

  /* [docs/memo.md 地雷1] render_texture() and the download can leave
   * another GL context current (GTK's GL renderer, or the private one). */
  gdk_gl_context_make_current (self->ctx);
  t2 = g_get_monotonic_time ();

  /* 3. Per region: skip everything if the pixels did not change (the node
   * changed, the picture did not: a sidebar next to scrolling content);
   * otherwise upload and blur. */
  for (guint i = 0; i < regions->len; i++)
    {
      Region *r = &g_array_index (regions, Region, i);
      GlassCapture *cap = r->cap;
      const GlassCaptureRequest *req = r->req;
      gsize row = (gsize) r->w * 4;
      gsize size = row * r->h;
      gboolean same = cap->valid && cap->tex_w == r->w && cap->tex_h == r->h &&
                      graphene_rect_equal (&r->C, &cap->rect) &&
                      r->capture_scale == cap->capture_scale &&
                      req->blur_radius == cap->blur_radius && r->downscale == cap->downscale;

      if (cap->pixels_size < size)
        {
          g_free (cap->pixels);
          cap->pixels_size = size;
          cap->pixels = g_malloc (size);
          same = FALSE;
        }
      for (int y = 0; y < r->h; y++)
        {
          const guint8 *src = self->atlas + (gsize) (r->y + y) * stride;
          guint8 *dst = cap->pixels + (gsize) y * row;

          if (same && memcmp (src, dst, row) != 0)
            same = FALSE;
          if (!same)
            memcpy (dst, src, row);
        }

      if (req->content_key)
        gsk_render_node_ref (req->content_key);
      g_clear_pointer (&cap->key_leaf, gsk_render_node_unref);
      cap->key_leaf = req->content_key;
      cap->key_dx = req->key_dx;
      cap->key_dy = req->key_dy;
      cap->key_extra = req->key_extra;

      if (same)
        {
          self->stats.capture_unchanged++;
          continue;
        }

      upload_and_blur (self, cap, r->w, r->h, req->blur_radius * req->scale, r->downscale);
      cap->rect = r->C;
      cap->capture_scale = r->capture_scale;
      cap->blur_radius = req->blur_radius;
      cap->downscale = r->downscale;
      cap->valid = TRUE;
      cap->fresh = TRUE;
      cap->gen++;
    }
  t3 = g_get_monotonic_time ();

  self->stats.us_render_texture += t1 - t0;
  self->stats.us_download += t2 - t1;
  self->stats.us_gl += t3 - t2;
  self->stats.captures++;
  self->stats.regions += regions->len;
  if (glass_get_debug_flags () & GLASS_DEBUG_HUD)
    g_debug ("capture: %u regions, atlas %dx%d: render %.3f, download %.3f, upload + blur %.3f ms",
             regions->len, atlas_w, atlas_h, (t1 - t0) / 1000.0, (t2 - t1) / 1000.0, (t3 - t2) / 1000.0);

  return TRUE;
}

void
glass_capture_get_source (GlassCapture     *cap,
                          GlassLayerSource *out)
{
  out->tex = cap->valid ? cap->capture_tex : 0;
  out->rect = cap->rect;
  out->id = ((guint64) GPOINTER_TO_SIZE (cap) << 20) ^ cap->gen;
}

static guint64
mix64 (guint64 h, guint64 v)
{
  h ^= v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
  return h;
}

static guint64
rect_bits (const graphene_rect_t *r)
{
  guint32 a, b, c, d;

  memcpy (&a, &r->origin.x, 4);
  memcpy (&b, &r->origin.y, 4);
  memcpy (&c, &r->size.width, 4);
  memcpy (&d, &r->size.height, 4);
  return ((guint64) a << 32 | b) ^ ((guint64) c << 16 | d);
}

/* Draws `src` (covering src_rect, view coordinates) into the target that
 * covers C at `scale` texels per px. Our context is current, the target is
 * bound and blending is set. */
static void
draw_layer (GlassRenderer         *self,
            GLuint                 src,
            const graphene_rect_t *src_rect,
            const graphene_rect_t *C,
            double                 scale)
{
  graphene_rect_t I;
  int x0, y0, x1, y1;

  if (src == 0 || !graphene_rect_intersection (src_rect, C, &I))
    return;

  /* Row 0 of every target is the top of the image (quad.vert), so the
   * viewport is in top-down pixels too. */
  x0 = (int) lround ((I.origin.x - C->origin.x) * scale);
  y0 = (int) lround ((I.origin.y - C->origin.y) * scale);
  x1 = (int) lround ((I.origin.x + I.size.width - C->origin.x) * scale);
  y1 = (int) lround ((I.origin.y + I.size.height - C->origin.y) * scale);
  if (x1 <= x0 || y1 <= y0)
    return;

  glViewport (x0, y0, x1 - x0, y1 - y0);
  glBindTexture (GL_TEXTURE_2D, src);
  glUniform4f (self->copy_uv_rect,
               (float) ((I.origin.x - src_rect->origin.x) / src_rect->size.width),
               (float) ((I.origin.y - src_rect->origin.y) / src_rect->size.height),
               (float) ((I.origin.x + I.size.width - src_rect->origin.x) / src_rect->size.width),
               (float) ((I.origin.y + I.size.height - src_rect->origin.y) / src_rect->size.height));
  glDrawArrays (GL_TRIANGLE_STRIP, 0, 4);
}

gboolean
glass_renderer_compose (GlassRenderer          *self,
                        GlassCapture           *cap,
                        const graphene_rect_t  *rect,
                        double                  scale,
                        int                     downscale,
                        double                  blur_radius,
                        const GlassLayerSource *sources,
                        guint                   n)
{
  double capture_scale;
  graphene_rect_t C = *rect;
  guint64 key = 0x243f6a8885a308d3ull;
  int w, h;

  cap->fresh = FALSE;
  downscale = downscale >= 4 ? 4 : downscale >= 2 ? 2 : 1;
  capture_scale = scale / downscale;
  snap_rect (&C, capture_scale, &w, &h);
  if (w < 1 || h < 1 || self->prog_copy == 0)
    {
      cap->valid = FALSE;
      return FALSE;
    }

  for (guint i = 0; i < n; i++)
    {
      key = mix64 (key, sources[i].tex);
      key = mix64 (key, sources[i].id);
      key = mix64 (key, rect_bits (&sources[i].rect));
    }

  if (!(glass_get_debug_flags () & GLASS_DEBUG_NO_CACHE) && cap->valid &&
      cap->compose_key == key && graphene_rect_equal (&C, &cap->rect) &&
      capture_scale == cap->capture_scale && blur_radius == cap->blur_radius &&
      downscale == cap->downscale)
    {
      self->stats.compose_hits++;
      return TRUE;
    }

  ensure_textures (cap, w, h);
  glBindFramebuffer (GL_FRAMEBUFFER, cap->capture_fbo);
  glViewport (0, 0, w, h);
  glDisable (GL_SCISSOR_TEST);
  glClearColor (0, 0, 0, 0);
  glClear (GL_COLOR_BUFFER_BIT);

  glBindVertexArray (self->vao);
  glUseProgram (self->prog_copy);
  glActiveTexture (GL_TEXTURE0);
  glUniform1i (self->copy_src, 0);
  glEnable (GL_BLEND);
  glBlendFunc (GL_ONE, GL_ONE_MINUS_SRC_ALPHA);   /* premultiplied OVER */
  for (guint i = 0; i < n; i++)
    draw_layer (self, sources[i].tex, &sources[i].rect, &C, capture_scale);
  glDisable (GL_BLEND);
  glBindFramebuffer (GL_FRAMEBUFFER, 0);

  blur (self, cap, w, h, blur_radius * scale, downscale);

  cap->rect = C;
  cap->capture_scale = capture_scale;
  cap->blur_radius = blur_radius;
  cap->downscale = downscale;
  cap->compose_key = key;
  cap->valid = TRUE;
  cap->gen++;
  self->stats.composes++;

  return TRUE;
}

gboolean
glass_capture_measure (GlassCapture          *cap,
                       const graphene_rect_t *panel,
                       const float            tint[4],
                       GlassLumaStats        *out)
{
  double s = cap->capture_scale;

  if (!cap->valid || cap->pixels == NULL)
    return FALSE;

  /* Cells no smaller than the blur, so a cell's mean is close to what the
   * glass shows (design.md §12.2). */
  return glass_adaptive_measure (cap->pixels, (gsize) cap->tex_w * 4, cap->tex_w, cap->tex_h,
                                 (panel->origin.x - cap->rect.origin.x) * s,
                                 (panel->origin.y - cap->rect.origin.y) * s,
                                 panel->size.width * s, panel->size.height * s,
                                 MAX (2.0, cap->blur_radius * s), tint, out);
}

/* Fused shapes: the gap between two rounded rectangles, their inner boxes'
 * distance less both radii (exact for circles, and for rectangles side by
 * side). */
static double
shape_gap (const graphene_rect_t *a, double ra, const graphene_rect_t *b, double rb)
{
  double dx = fabs ((a->origin.x + a->size.width / 2.0) - (b->origin.x + b->size.width / 2.0)) -
              MAX (a->size.width / 2.0 - ra, 0.0) - MAX (b->size.width / 2.0 - rb, 0.0);
  double dy = fabs ((a->origin.y + a->size.height / 2.0) - (b->origin.y + b->size.height / 2.0)) -
              MAX (a->size.height / 2.0 - ra, 0.0) - MAX (b->size.height / 2.0 - rb, 0.0);

  dx = MAX (dx, 0.0);
  dy = MAX (dy, 0.0);
  return sqrt (dx * dx + dy * dy) - ra - rb;
}

/* The bridges of glass_shape.glsl, in device px (S per logical px): pairs
 * closer than half the merge width, where the smooth union starts to join
 * them. With s = 0 (touching) the bridge is their hull, joined with a hard
 * min; towards s = 1 it is inset until it has gone (the thicker one's half
 * width, and 2 px more so no edge of it is left), as the union's own neck
 * parts. The join is softest half-way. */
static guint
fused_bridges (const PassKey *key, double S, float out[4 * GLASS_MAX_BRIDGES])
{
  double reach = key->merge_k / 2.0;
  guint n = 0;

  for (guint a = 0; a < key->n_shapes && reach > 0.0; a++)
    for (guint b = a + 1; b < key->n_shapes && n < GLASS_MAX_BRIDGES; b++)
      {
        const graphene_rect_t *ra = &key->shapes[a], *rb = &key->shapes[b];
        double gap = shape_gap (ra, key->radii[a], rb, key->radii[b]);
        double u, s, thick;

        if (gap >= reach)
          continue;
        u = CLAMP (gap / reach, 0.0, 1.0);
        s = u * u * (3.0 - 2.0 * u);
        thick = MAX (MIN (ra->size.width, ra->size.height), MIN (rb->size.width, rb->size.height)) / 2.0;
        out[4 * n + 0] = (float) a;
        out[4 * n + 1] = (float) b;
        out[4 * n + 2] = (float) ((thick + 2.0) * s * S);
        out[4 * n + 3] = (float) (2.0 * key->merge_k * s * (1.0 - s) * S);
        n++;
      }
  return n;
}

/* The glass pass into a pool slot. Our context must be current. Everything
 * is in device px of the output texture: the surface the core's uv spans. */
static void
run_glass_pass (GlassRenderer            *self,
                GlassCapture             *cap,
                OutputSlot               *slot,
                const GlassRenderRequest *req,
                const PassKey            *key,
                double                    shadow_room)
{
  const graphene_rect_t *P = &req->panel;
  const graphene_rect_t *O = &key->O;
  const graphene_rect_t *C = &cap->rect;
  const double *p = key->params;
  double S = req->scale;
  double radius;

  glBindFramebuffer (GL_FRAMEBUFFER, slot->fbo);
  glViewport (0, 0, slot->w, slot->h);
  glDisable (GL_BLEND);
  glUseProgram (self->prog_glass);
  glBindVertexArray (self->vao);

  glActiveTexture (GL_TEXTURE0);
  glBindTexture (GL_TEXTURE_2D, cap->backdrop_tex);
  glUniform1i (uniform (self, "glass_backdrop"), 0);
  glUniform4f (uniform (self, "u_uv_rect"), 0.0f, 0.0f, 1.0f, 1.0f);

  u1f (self, "resolution_x", slot->w);
  u1f (self, "resolution_y", slot->h);
  radius = MIN (P->size.width, P->size.height) / 2.0;
  if (key->radius >= 0.0)
    radius = MIN (key->radius, radius);
  glUniform4f (uniform (self, "glass_rect"),
               (float) ((P->origin.x - O->origin.x) * S), (float) ((P->origin.y - O->origin.y) * S),
               (float) (P->size.width * S), (float) (P->size.height * S));

  /* Per-corner radii (glass_shape.glsl): the outline takes each corner's,
   * the lens band the largest. Four equal ones are the plain path. */
  {
    float radii[4];
    double half = MIN (P->size.width, P->size.height) / 2.0;
    double largest = 0.0;
    gboolean differ = FALSE;

    for (int i = 0; i < 4; i++)
      {
        double r = key->has_corner_radii && key->corner_radii[i] >= 0.0 ? MIN (key->corner_radii[i], half) : radius;

        radii[i] = (float) (r * S);
        largest = MAX (largest, r);
        differ |= r != radius;
      }
    u1f (self, "glass_corner_mode", differ ? 1.0 : 0.0);
    if (differ)
      {
        glUniform4fv (uniform (self, "glass_corner_radii"), 1, radii);
        radius = largest;
      }
  }
  u1f (self, "corner_radius", radius * S);

  /* Fused shapes (GlassGroup), in device px of the output. */
  u1f (self, "glass_shape_count", key->n_shapes);
  if (key->n_shapes > 0)
    {
      float shapes[4 * GLASS_MAX_SHAPES];
      float radii[GLASS_MAX_SHAPES];

      for (guint i = 0; i < key->n_shapes; i++)
        {
          const graphene_rect_t *r = &key->shapes[i];

          shapes[4 * i + 0] = (float) ((r->origin.x - O->origin.x) * S);
          shapes[4 * i + 1] = (float) ((r->origin.y - O->origin.y) * S);
          shapes[4 * i + 2] = (float) (r->size.width * S);
          shapes[4 * i + 3] = (float) (r->size.height * S);
          radii[i] = (float) (key->radii[i] * S);
        }
      glUniform4fv (uniform (self, "glass_shapes"), key->n_shapes, shapes);
      glUniform1fv (uniform (self, "glass_shape_radii"), key->n_shapes, radii);
      u1f (self, "glass_merge_k", key->merge_k * S);
    }
  {
    float bridges[4 * GLASS_MAX_BRIDGES];
    guint n_bridges = key->n_shapes > 1 ? fused_bridges (key, S, bridges) : 0;

    u1f (self, "glass_bridge_count", n_bridges);
    if (n_bridges > 0)
      glUniform4fv (uniform (self, "glass_bridges"), n_bridges, bridges);
  }

  /* design.md §10.2: the values the reference read off its FBO size, as the
   * signed-off full-monitor figures; the lens at its logical size; no
   * damping at the output's edge. */
  u1f (self, "gradient_step", 1.2 * S);
  u1f (self, "max_displacement_px", 324.0 * S);
  u1f (self, "lens_px_scale", S);
  u1f (self, "edge_damping", 0.0);
  /* §8.5: half the sample spacing, in device px. */
  u1f (self, "lens_footprint_px", key->measured_footprint ? (key->supersample ? 0.25 : 0.5) : 0.0);

  u1f (self, "blur_rect_x", (C->origin.x - O->origin.x) * S);
  u1f (self, "blur_rect_y", (C->origin.y - O->origin.y) * S);
  u1f (self, "blur_rect_w", C->size.width * S);
  u1f (self, "blur_rect_h", C->size.height * S);
  u1f (self, "blur_tex_w", cap->tex_w);
  u1f (self, "blur_tex_h", cap->tex_h);

  for (int i = 0; i < GLASS_N_PARAMS; i++)
    if (glass_param_specs[i].uniform)
      u1f (self, glass_param_specs[i].uniform, p[i] * (glass_param_specs[i].px ? S : 1.0));

  if (!key->has_shadow)
    {
      u1f (self, "shadow_radius", 0.0);
      u1f (self, "shadow_intensity", 0.0);
    }
  u1f (self, "shadow_max_radius", shadow_room * S);

  u1f (self, "tint_r", key->tint[0]);
  u1f (self, "tint_g", key->tint[1]);
  u1f (self, "tint_b", key->tint[2]);
  u1f (self, "tint_strength", key->tint[3]);
  u1f (self, "brightness", 1.0);
  u1f (self, "contrast", 1.0);
  u1f (self, "saturation", 1.0);
  u1f (self, "surface_light_enabled", 1.0);

  /* An unset uniform reads 0.0, which would switch these off. */
  u1f (self, "early_exit_enabled", 1.0);
  u1f (self, "edge_taps_enabled", 1.0);
  u1f (self, "debug_view", 0.0);

  u1f (self, "u_supersample", key->supersample ? 4.0 : 1.0);
  u1f (self, "u_rim_samples", 0.0);
  glUniform2f (uniform (self, "u_pixel_uv"), 1.0f / slot->w, 1.0f / slot->h);

  glDrawArrays (GL_TRIANGLE_STRIP, 0, 4);
  glBindFramebuffer (GL_FRAMEBUFFER, 0);
}

/* Sends what the passes queued to the GPU. The output textures carry fences
 * that GTK waits on in its own context, which only works once they are
 * flushed; call it when a view or a standalone pane is done drawing. */
void
glass_renderer_flush (GlassRenderer *self)
{
  glass_renderer_make_current (self);
  glFlush ();
}

gboolean
glass_renderer_render_panel (GlassRenderer            *self,
                             GlassPanelRender         *pr,
                             GlassCapture             *cap,
                             const GlassRenderRequest *req,
                             GlassRenderResult        *res)
{
  const double *params = req->params;
  GlassDebugFlags debug = glass_get_debug_flags ();
  graphene_rect_t P = req->panel, O;
  double shadow_room;
  int o_w, o_h;
  PassKey key;
  OutputSlot *slot;
  GdkGLTextureBuilder *builder;
  ReleaseData *release;
  GdkTexture *texture;
  GLsync sync;
  gint64 t0;

  memset (res, 0, sizeof *res);
  if (!cap->valid)
    return FALSE;

  shadow_room = req->has_shadow ? params[GLASS_PARAM_ID_SHADOW_RADIUS] + SHADOW_ROOM_EXTRA : EDGE_ROOM;

  /* O: the panel plus room for its shadow, the texture handed to GTK. */
  O = P;
  graphene_rect_inset (&O, -shadow_room, -shadow_room);
  if (!graphene_rect_intersection (&O, &req->view_rect, &O))
    return FALSE;
  snap_rect (&O, req->scale, &o_w, &o_h);
  if (o_w < 1 || o_h < 1)
    return FALSE;

  if (pr->pool == NULL)
    pr->pool = pool_new (self->ctx);
  pool_reap (pr->pool);

  memset (&key, 0, sizeof key);
  key.P = P;
  key.O = O;
  key.C = cap->rect;
  key.scale = req->scale;
  key.radius = req->corner_radius;
  key.has_corner_radii = req->has_corner_radii;
  if (req->has_corner_radii)
    memcpy (key.corner_radii, req->corner_radii, sizeof key.corner_radii);
  memcpy (key.params, params, sizeof key.params);
  memcpy (key.tint, req->tint, sizeof key.tint);
  key.has_shadow = req->has_shadow;
  key.capture = cap;
  key.capture_gen = cap->gen;
  key.n_shapes = MIN (req->n_shapes, GLASS_MAX_SHAPES);
  memcpy (key.shapes, req->shapes, sizeof (graphene_rect_t) * key.n_shapes);
  memcpy (key.radii, req->radii, sizeof (float) * key.n_shapes);
  key.merge_k = key.n_shapes ? req->merge_k : 0.0f;
  key.supersample = !(debug & GLASS_DEBUG_NO_SUPERSAMPLE);
  key.measured_footprint = !(debug & GLASS_DEBUG_ESTIMATED_FOOTPRINT);

  res->stale = FALSE;
  if (pr->pass_valid && pr->last_texture && memcmp (&key, &pr->pass_key, sizeof key) == 0)
    {
      res->texture = pr->last_texture;
      res->rect = O;
      self->stats.pass_hits++;
      return TRUE;
    }

  slot = pool_acquire (pr->pool, o_w, o_h);
  if (slot == NULL)
    {
      /* Every texture is still held by GTK: show the previous one for now,
       * and have the caller draw again on the next frame. Without that, a
       * resize (a sidebar sliding, a window going fullscreen) that ran out
       * of textures left its last frame's glass at the old size until
       * something else redrew (docs/memo.md 地雷42). */
      if (pr->last_texture == NULL)
        return FALSE;
      g_debug ("output textures all in use: last frame's glass, again next frame");
      res->texture = pr->last_texture;
      res->rect = pr->pass_key.O;
      res->stale = TRUE;
      return TRUE;
    }

  t0 = g_get_monotonic_time ();
  gpu_begin (key.n_shapes ? "pass-fused" : "pass", slot->w, slot->h);
  run_glass_pass (self, cap, slot, req, &key, shadow_room);
  gpu_end ();

  /* No flush here: glass_renderer_flush() sends the view's passes in one
   * submission instead of one each (a kernel call per flush). */
  sync = glFenceSync (GL_SYNC_GPU_COMMANDS_COMPLETE, 0);

  release = g_new0 (ReleaseData, 1);
  release->slot = slot;
  release->sync = sync;
  slot->in_use = TRUE;
  g_ref_count_inc (&pr->pool->ref);

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
  self->stats.passes++;

  g_clear_object (&pr->last_texture);
  pr->last_texture = texture;
  pr->pass_key = key;
  pr->pass_valid = TRUE;
  pr->output_gen++;
  pr->output_tex = slot->tex;

  res->texture = texture;
  res->rect = O;
  return TRUE;
}

gboolean
glass_panel_render_get_output (GlassPanelRender *pr,
                               GlassLayerSource *out)
{
  if (!pr->pass_valid || pr->last_texture == NULL)
    return FALSE;

  out->tex = pr->output_tex;
  out->rect = pr->pass_key.O;
  out->id = ((guint64) GPOINTER_TO_SIZE (pr) << 20) ^ pr->output_gen;
  return TRUE;
}

