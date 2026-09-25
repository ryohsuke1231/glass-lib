/* glass-renderer.h — the full renderer (private; design.md §8).
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

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
  guint  captures;
  guint  capture_hits;
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

typedef struct {
  graphene_rect_t view_rect;
  graphene_rect_t panel;           /* P, in view coordinates */
  double          scale;           /* surface scale */
  double          corner_radius;   /* logical px; < 0 = capsule */
  const double   *params;          /* resolved, [GLASS_N_PARAMS] */
  float           tint[4];         /* rgb, strength */
  gboolean        has_shadow;
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
/* Captures and blurs, or keeps the last one if nothing it depends on
 * changed. Our context must be current; it is again on return. */
gboolean          glass_renderer_capture       (GlassRenderer             *self,
                                                GlassCapture              *capture,
                                                const GlassCaptureRequest *req);
/* TRUE if the last glass_renderer_capture() captured anew (as opposed to
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

/* Strips the translate-only wrappers gtk_widget_snapshot_child() puts around
 * a child's cached node. */
GskRenderNode    *glass_unwrap_node            (GskRenderNode *node,
                                                float         *dx,
                                                float         *dy);

G_END_DECLS
