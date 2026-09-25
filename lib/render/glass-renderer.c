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

#define OUTPUT_POOL_SIZE 3

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

typedef struct {
  GLuint h, v;
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
  GHashTable       *uniforms;        /* static name -> location + 1 */
  GHashTable       *blur_programs;   /* sigma * 1000 -> BlurPrograms* */
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
};

typedef struct {
  graphene_rect_t P, O, C;
  double          scale;
  double          radius;
  double          params[GLASS_N_PARAMS];
  float           tint[4];
  gboolean        has_shadow;
  gconstpointer   capture;
  guint           capture_gen;
  gboolean        supersample;
  gboolean        measured_footprint;
} PassKey;

struct _GlassPanelRender {
  OutputPool     *pool;
  GdkTexture     *last_texture;
  gboolean        pass_valid;
  PassKey         pass_key;
};

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
  if (bp->h)
    glDeleteProgram (bp->h);
  if (bp->v)
    glDeleteProgram (bp->v);
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

  glGenVertexArrays (1, &self->vao);
  self->uniforms = g_hash_table_new (g_str_hash, g_str_equal);
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
  gdk_gl_context_make_current (self->ctx);
  g_hash_table_destroy (self->blur_programs);
  if (self->prog_glass)
    glDeleteProgram (self->prog_glass);
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
    return bp->v ? bp : NULL;

  bp = g_new0 (BlurPrograms, 1);
  g_hash_table_insert (self->blur_programs, key, bp);

  h_src = glass_gauss_fragment_source (kernel, TRUE);
  v_src = glass_gauss_fragment_source (kernel, FALSE);
  {
    const char *h_parts[] = { h_src, NULL };
    const char *v_parts[] = { v_src, NULL };

    bp->h = glass_gl_program_new (self->use_es, self->vertex_source, h_parts, &error);
    if (bp->h)
      bp->v = glass_gl_program_new (self->use_es, self->vertex_source, v_parts, &error);
  }
  if (bp->v == 0)
    {
      g_warning ("the blur shader did not build, glass is drawn unblurred: %s", error->message);
      return NULL;
    }

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
run_blur_pass (GlassRenderer *self, GLuint program, double kernel_scale,
               GLuint src, GLuint dst_fbo, int w, int h)
{
  glBindFramebuffer (GL_FRAMEBUFFER, dst_fbo);
  glViewport (0, 0, w, h);
  glUseProgram (program);
  glActiveTexture (GL_TEXTURE0);
  glBindTexture (GL_TEXTURE_2D, src);
  glUniform1i (glGetUniformLocation (program, "u_src"), 0);
  glUniform2f (glGetUniformLocation (program, "inv_size"), 1.0f / w, 1.0f / h);
  glUniform1f (glGetUniformLocation (program, "kernel_scale"), (float) kernel_scale);
  glUniform4f (glGetUniformLocation (program, "u_uv_rect"), 0.0f, 0.0f, 1.0f, 1.0f);
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

gboolean
glass_renderer_capture (GlassRenderer             *self,
                        GlassCapture              *cap,
                        const GlassCaptureRequest *req)
{
  GskRenderer *renderer = gtk_native_get_renderer (self->native);
  int downscale = req->downscale >= 4 ? 4 : req->downscale >= 2 ? 2 : 1;
  double capture_scale = req->scale / downscale;
  graphene_rect_t C = req->rect;
  GskRenderNode *clip, *node;
  GskTransform *transform;
  GdkTextureDownloader *downloader;
  GdkTexture *texture;
  GlassGaussKernel kernel;
  gint64 t0, t1, t2, t3, t4;
  int c_w, c_h, w, h;
  gsize stride;

  cap->fresh = FALSE;
  snap_rect (&C, capture_scale, &c_w, &c_h);
  if (c_w < 1 || c_h < 1)
    return FALSE;

  if (!(glass_get_debug_flags () & GLASS_DEBUG_NO_CACHE) && cap->valid &&
      req->content_key == cap->key_leaf &&
      req->key_dx == cap->key_dx && req->key_dy == cap->key_dy &&
      req->key_extra == cap->key_extra &&
      graphene_rect_equal (&C, &cap->rect) &&
      capture_scale == cap->capture_scale &&
      req->blur_radius == cap->blur_radius &&
      downscale == cap->downscale)
    {
      self->stats.capture_hits++;
      return TRUE;
    }

  clip = gsk_clip_node_new (req->backdrop, &C);
  transform = gsk_transform_scale (NULL, capture_scale, capture_scale);
  transform = gsk_transform_translate (transform, &GRAPHENE_POINT_INIT (-C.origin.x, -C.origin.y));
  node = gsk_transform_node_new (clip, transform);
  gsk_transform_unref (transform);
  gsk_render_node_unref (clip);

  t0 = g_get_monotonic_time ();
  texture = gsk_renderer_render_texture (renderer, node, &GRAPHENE_RECT_INIT (0, 0, c_w, c_h));
  t1 = g_get_monotonic_time ();
  gsk_render_node_unref (node);

  if (texture == NULL)
    {
      gdk_gl_context_make_current (self->ctx);
      cap->valid = FALSE;
      return FALSE;
    }

  w = gdk_texture_get_width (texture);
  h = gdk_texture_get_height (texture);
  stride = (gsize) w * 4;
  if (cap->pixels_size < stride * h)
    {
      g_free (cap->pixels);
      cap->pixels_size = stride * h;
      cap->pixels = g_malloc (cap->pixels_size);
    }

  downloader = gdk_texture_downloader_new (texture);
  gdk_texture_downloader_set_format (downloader, GDK_MEMORY_R8G8B8A8_PREMULTIPLIED);
  gdk_texture_downloader_download_into (downloader, cap->pixels, stride);
  gdk_texture_downloader_free (downloader);
  g_object_unref (texture);

  /* [docs/memo.md 地雷1] GTK's GL renderer makes ITS context current to
   * render and download, and leaves it that way. Textures and programs are
   * shared between the two contexts, framebuffer objects and VAOs are not:
   * without this, the blur and glass passes bind ids that mean nothing (or
   * something else) in GTK's context and draw into the wrong framebuffer. */
  gdk_gl_context_make_current (self->ctx);
  t2 = g_get_monotonic_time ();

  if (cap->tex_w != w || cap->tex_h != h)
    {
      alloc_texture (&cap->capture_tex, w, h);
      alloc_texture (&cap->blur_h_tex, w, h);
      alloc_texture (&cap->blur_v_tex, w, h);
      attach_fbo (&cap->capture_fbo, cap->capture_tex);
      attach_fbo (&cap->blur_h_fbo, cap->blur_h_tex);
      attach_fbo (&cap->blur_v_fbo, cap->blur_v_tex);
      cap->tex_w = w;
      cap->tex_h = h;
    }

  glPixelStorei (GL_UNPACK_ALIGNMENT, 4);
  glPixelStorei (GL_UNPACK_ROW_LENGTH, 0);
  glBindTexture (GL_TEXTURE_2D, cap->capture_tex);
  glTexSubImage2D (GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, cap->pixels);
  t3 = g_get_monotonic_time ();

  cap->backdrop_tex = cap->capture_tex;
  if (glass_gauss_kernel_for_radius (req->blur_radius * req->scale, downscale, &kernel))
    {
      BlurPrograms *bp = blur_programs (self, &kernel);

      if (bp)
        {
          glBindVertexArray (self->vao);
          glDisable (GL_BLEND);
          run_blur_pass (self, bp->h, kernel.scale, cap->capture_tex, cap->blur_h_fbo, w, h);
          run_blur_pass (self, bp->v, kernel.scale, cap->blur_h_tex, cap->blur_v_fbo, w, h);
          glBindFramebuffer (GL_FRAMEBUFFER, 0);
          cap->backdrop_tex = cap->blur_v_tex;
        }
    }
  t4 = g_get_monotonic_time ();

  if (req->content_key)
    gsk_render_node_ref (req->content_key);
  g_clear_pointer (&cap->key_leaf, gsk_render_node_unref);
  cap->key_leaf = req->content_key;
  cap->key_dx = req->key_dx;
  cap->key_dy = req->key_dy;
  cap->key_extra = req->key_extra;
  cap->rect = C;
  cap->capture_scale = capture_scale;
  cap->blur_radius = req->blur_radius;
  cap->downscale = downscale;
  cap->valid = TRUE;
  cap->fresh = TRUE;
  cap->gen++;

  self->stats.us_render_texture += t1 - t0;
  self->stats.us_download += t2 - t1;
  self->stats.us_upload += t3 - t2;
  self->stats.us_gl += t4 - t3;
  self->stats.captures++;

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
  u1f (self, "corner_radius", radius * S);

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

  shadow_room = req->has_shadow ? params[GLASS_PARAM_SHADOW_RADIUS] + SHADOW_ROOM_EXTRA : EDGE_ROOM;

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
  memcpy (key.params, params, sizeof key.params);
  memcpy (key.tint, req->tint, sizeof key.tint);
  key.has_shadow = req->has_shadow;
  key.capture = cap;
  key.capture_gen = cap->gen;
  key.supersample = !(debug & GLASS_DEBUG_NO_SUPERSAMPLE);
  key.measured_footprint = !(debug & GLASS_DEBUG_ESTIMATED_FOOTPRINT);

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
      /* Every texture is still held by GTK: show the previous one. */
      if (pr->last_texture == NULL)
        return FALSE;
      res->texture = pr->last_texture;
      res->rect = pr->pass_key.O;
      return TRUE;
    }

  t0 = g_get_monotonic_time ();
  run_glass_pass (self, cap, slot, req, &key, shadow_room);

  sync = glFenceSync (GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
  glFlush ();

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

  res->texture = texture;
  res->rect = O;
  return TRUE;
}
