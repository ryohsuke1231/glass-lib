/* glass-renderer.h — the full renderer (private; design.md §8).
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <epoxy/gl.h>
#include <gtk/gtk.h>

#include "glass-params-table.h"
#include "adaptive/glass-adaptive.h"

G_BEGIN_DECLS

/* One per GtkNative (window), shared by every GlassView in it: the GL
 * context, the programs and the vertex array. */
typedef struct _GlassRenderer GlassRenderer;

/* A captured and blurred piece of a view's backdrop. Panels next to each
 * other share one (design.md §8.2): the capture's fixed cost dominates. */
typedef struct _GlassCapture GlassCapture;

/* One per panel: its output textures and the finished-glass cache. */
typedef struct _GlassPanelRender GlassPanelRender;

typedef struct {
  guint  snapshots;
  guint  captures;            /* render_texture() calls */
  guint  regions;             /* captured regions (several per call) */
  guint  capture_hits;
  guint  capture_unchanged;   /* captured, but the pixels had not changed */
  guint  composes;            /* backdrops composed from our own textures */
  guint  compose_hits;
  guint  passes;
  guint  pass_hits;
  gint64 us_total;
  gint64 us_render_texture;
  gint64 us_download;
  gint64 us_upload;
  gint64 us_gl;
} GlassRenderStats;

typedef struct {
  GskRenderNode  *backdrop;        /* what the glass sees: colour + content + edge effects */
  GskRenderNode  *content_key;     /* unwrapped content node: "did the content change" */
  float           key_dx, key_dy;
  guint           key_extra;       /* anything else backdrop depends on (edge effects, colour) */
  graphene_rect_t rect;            /* C, view coordinates, inside the view */
  double          scale;           /* surface scale */
  double          blur_radius;     /* logical px */
  int             downscale;       /* 1, 2 or 4 */
} GlassCaptureRequest;

#define GLASS_MAX_SHAPES 8
#define GLASS_MAX_BRIDGES 12   /* shaders/core/glass_shape.glsl */

typedef struct {
  graphene_rect_t view_rect;
  graphene_rect_t panel;           /* P, in view coordinates (fused: the bounds of the shapes) */
  double          scale;           /* surface scale */
  double          corner_radius;   /* logical px; < 0 = capsule */
  /* Per corner (top-left, top-right, bottom-right, bottom-left), logical px;
   * a negative one is corner_radius. Only with has_corner_radii. */
  gboolean        has_corner_radii;
  double          corner_radii[4];
  const double   *params;          /* resolved, [GLASS_N_PARAMS] */
  float           tint[4];         /* rgb, strength */
  gboolean        has_shadow;
  /* Fused shapes (GlassGroup): 0 = the one rounded rectangle `panel`. */
  guint           n_shapes;
  graphene_rect_t shapes[GLASS_MAX_SHAPES];
  float           radii[GLASS_MAX_SHAPES];
  float           merge_k;
} GlassRenderRequest;

typedef struct {
  GdkTexture     *texture;         /* transfer none: owned by the panel render */
  graphene_rect_t rect;            /* where to draw it, view coordinates */
} GlassRenderResult;

GlassRenderer    *glass_renderer_acquire       (GtkNative *native);
void              glass_renderer_release       (GlassRenderer *self);
gboolean          glass_renderer_failed        (GtkNative *native);
void              glass_renderer_make_current  (GlassRenderer *self);
const char       *glass_renderer_get_info      (GlassRenderer *self);
GlassRenderStats *glass_renderer_get_stats     (GlassRenderer *self);

/* The part of the backdrop a panel needs: P plus the blur's and the lens's
 * reach, inside the view. FALSE if none of it is. */
gboolean          glass_capture_rect_for_panel (const graphene_rect_t *panel,
                                                double                 blur_radius,
                                                const graphene_rect_t *view_rect,
                                                graphene_rect_t       *out);

GlassCapture     *glass_capture_new            (void);
void              glass_capture_free           (GlassCapture  *capture,
                                                GlassRenderer *renderer);
void              glass_capture_release_gl     (GlassCapture *capture);
/* Captures and blurs every stale one of @captures in one render (an atlas),
 * keeping the others. Our context must be current; it is again on return. */
gboolean          glass_renderer_capture_all   (GlassRenderer             *self,
                                                GlassCapture             **captures,
                                                const GlassCaptureRequest *reqs,
                                                guint                      n);
/* One of our own textures, as a layer of a composed backdrop. */
typedef struct {
  GLuint          tex;             /* premultiplied RGBA, row 0 = top */
  graphene_rect_t rect;            /* what it covers, view coordinates */
  guint64         id;              /* changes whenever its content does */
} GlassLayerSource;

/* The unblurred capture, as a layer. */
void              glass_capture_get_source     (GlassCapture     *capture,
                                                GlassLayerSource *out);
/* The panel's last glass texture, as a layer. FALSE if there is none. */
gboolean          glass_panel_render_get_output (GlassPanelRender *pr,
                                                 GlassLayerSource *out);
/* Makes @capture's backdrop from our own textures, bottom first, then blurs
 * it: glass over glass without asking GTK to draw anything (design.md §6.7).
 * Reused while the sources are the same. Our context must be current. */
gboolean          glass_renderer_compose       (GlassRenderer          *self,
                                                GlassCapture           *capture,
                                                const graphene_rect_t  *rect,
                                                double                  scale,
                                                int                     downscale,
                                                double                  blur_radius,
                                                const GlassLayerSource *sources,
                                                guint                   n);

/* TRUE if the last glass_renderer_capture_all() captured anew (as opposed to
 * reusing): only then are there new pixels to measure. */
gboolean          glass_capture_is_fresh       (GlassCapture *capture);

gboolean          glass_capture_measure        (GlassCapture          *capture,
                                                const graphene_rect_t *panel,
                                                const float            tint[4],
                                                GlassLumaStats        *out);

GlassPanelRender *glass_panel_render_new       (void);
/* Frees GL objects with the renderer's context (NULL: there are none). */
void              glass_panel_render_free      (GlassPanelRender *pr,
                                                GlassRenderer    *renderer);
/* Drops the GL objects (the renderer's context must be current). */
void              glass_panel_render_release_gl (GlassPanelRender *pr);

/* The glass pass of one panel over its capture. Our context must be current. */
gboolean          glass_renderer_render_panel  (GlassRenderer            *self,
                                                GlassPanelRender         *pr,
                                                GlassCapture             *capture,
                                                const GlassRenderRequest *req,
                                                GlassRenderResult        *res);

/* Sends the queued passes to the GPU: once a view or a standalone pane is
 * done drawing (the output textures' fences only work once flushed). */
void              glass_renderer_flush         (GlassRenderer            *self);

/* Strips the translate-only wrappers gtk_widget_snapshot_child() puts around
 * a child's cached node. */
GskRenderNode    *glass_unwrap_node            (GskRenderNode *node,
                                                float         *dx,
                                                float         *dy);

G_END_DECLS
