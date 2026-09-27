/* glass-standalone.c — one pane of glass over a backdrop the caller hands
 * in, outside any GlassView (design.md §6.7).
 *
 * Popovers (a surface of their own, over a window) and dialogs (drawn in a
 * window, but by libadwaita's dialog host, not inside a GlassView) cannot
 * take part in a view's drawing. They draw their glass themselves, with the
 * same renderer, from a render node of what is behind them: the parent
 * window's node for a popover, the window content's for a dialog.
 *
 * SPDX-License-Identifier: MIT
 */
#include "glass-private.h"
#include "render/glass-renderer.h"

struct _GlassStandalone {
  GlassRenderer    *renderer;
  GtkNative        *native;           /* the renderer's, weak */
  GlassCapture     *capture;
  GlassPanelRender *render;
  GtkWidget        *retry_widget;     /* weak: drawing again next frame */
  guint             retry_id;
};

static gboolean
retry_tick (GtkWidget     *widget,
            GdkFrameClock *clock,
            gpointer       data)
{
  GlassStandalone *self = data;

  g_clear_weak_pointer (&self->retry_widget);
  self->retry_id = 0;
  gtk_widget_queue_draw (widget);
  return G_SOURCE_REMOVE;
}

GlassStandalone *
glass_standalone_new (void)
{
  return g_new0 (GlassStandalone, 1);
}

void
glass_standalone_release (GlassStandalone *self)
{
  if (self->renderer)
    {
      glass_renderer_make_current (self->renderer);
      if (self->capture)
        glass_capture_release_gl (self->capture);
      if (self->render)
        glass_panel_render_release_gl (self->render);
      gdk_gl_context_clear_current ();
      g_clear_pointer (&self->renderer, glass_renderer_release);
    }
  self->native = NULL;
}

void
glass_standalone_free (GlassStandalone *self)
{
  if (self == NULL)
    return;
  if (self->retry_widget)
    gtk_widget_remove_tick_callback (self->retry_widget, self->retry_id);
  g_clear_weak_pointer (&self->retry_widget);
  glass_standalone_release (self);
  glass_capture_free (self->capture, NULL);
  glass_panel_render_free (self->render, NULL);
  g_free (self);
}

gboolean
glass_standalone_draw (GlassStandalone       *self,
                       GtkWidget             *widget,
                       GtkSnapshot           *snapshot,
                       GskRenderNode         *backdrop,
                       const graphene_rect_t *rect,
                       const double           radii[4],
                       GlassMaterial          material,
                       const GdkRGBA         *theme_bg,
                       gboolean               shadow)
{
  GlassContext *context = glass_context_get_default ();
  GtkNative *native = gtk_widget_get_native (widget);
  graphene_rect_t view_rect = GRAPHENE_RECT_INIT (0, 0, gtk_widget_get_width (widget),
                                                  gtk_widget_get_height (widget));
  GlassCaptureRequest creq = { 0 };
  GlassRenderRequest req = { 0 };
  GlassRenderResult res;
  double params[GLASS_N_PARAMS];
  double scale;
  float dx, dy;

  if (native == NULL || backdrop == NULL ||
      glass_context_get_wanted_renderer (context) != GLASS_RENDERER_MODE_FULL ||
      glass_context_get_high_contrast (context))
    return FALSE;

  if (self->native != native)
    {
      glass_standalone_release (self);
      self->renderer = glass_renderer_acquire (native);
      if (self->renderer == NULL)
        return FALSE;
      self->native = native;
    }
  if (self->capture == NULL)
    self->capture = glass_capture_new ();
  if (self->render == NULL)
    self->render = glass_panel_render_new ();

  scale = gdk_surface_get_scale (gtk_native_get_surface (native));
  glass_context_resolve (context, material, params);
  glass_context_resolve_tint (context, material, params[GLASS_PARAM_ID_TINT_STRENGTH], theme_bg, req.tint);
  if (glass_context_get_reduce_transparency (context))
    glass_reduce_transparency (params, req.tint);

  if (!glass_capture_rect_for_panel (rect, params[GLASS_PARAM_ID_BLUR_RADIUS], &view_rect, &creq.rect))
    return FALSE;

  glass_renderer_make_current (self->renderer);
  creq.backdrop = backdrop;
  creq.content_key = glass_unwrap_node (backdrop, &dx, &dy);
  creq.key_dx = dx;
  creq.key_dy = dy;
  creq.scale = scale;
  creq.blur_radius = params[GLASS_PARAM_ID_BLUR_RADIUS];
  creq.downscale = (int) params[GLASS_PARAM_ID_BLUR_DOWNSCALE];
  if (!glass_renderer_capture_all (self->renderer, &self->capture, &creq, 1))
    return FALSE;

  req.view_rect = view_rect;
  req.panel = *rect;
  req.scale = scale;
  req.corner_radius = radii[0];
  req.has_corner_radii = radii[1] != radii[0] || radii[2] != radii[0] || radii[3] != radii[0];
  memcpy (req.corner_radii, radii, sizeof req.corner_radii);
  req.params = params;
  req.has_shadow = shadow;
  if (!glass_renderer_render_panel (self->renderer, self->render, self->capture, &req, &res))
    return FALSE;
  /* Last frame's glass (no free output texture): draw again next frame. */
  if (res.stale && self->retry_widget == NULL)
    {
      g_set_weak_pointer (&self->retry_widget, widget);
      self->retry_id = gtk_widget_add_tick_callback (widget, retry_tick, self, NULL);
    }
  glass_renderer_flush (self->renderer);

  gtk_snapshot_append_texture (snapshot, res.texture, &res.rect);
  return TRUE;
}

/* What `widget` sees behind it: `source`'s current render node, moved into
 * `widget`'s coordinates by `offset` (the position of `source`'s origin in
 * them). NULL if `source` has not been drawn. */
GskRenderNode *
glass_standalone_backdrop (GtkWidget             *source,
                           const graphene_point_t *offset)
{
  GdkPaintable *paintable;
  GtkSnapshot *snapshot;
  GskRenderNode *node;
  int w = gtk_widget_get_width (source);
  int h = gtk_widget_get_height (source);

  if (w <= 0 || h <= 0 || !gtk_widget_get_mapped (source))
    return NULL;

  /* The source's cached node: nothing is redrawn. */
  paintable = gtk_widget_paintable_new (source);
  snapshot = gtk_snapshot_new ();
  gtk_snapshot_translate (snapshot, offset);
  gdk_paintable_snapshot (paintable, snapshot, w, h);
  node = gtk_snapshot_free_to_node (snapshot);
  g_object_unref (paintable);

  return node;
}
