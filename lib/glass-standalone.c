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
};

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
  const GlassMaterialSpec *m = &glass_material_specs[CLAMP ((int) material, 0, GLASS_N_MATERIALS - 1)];
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
  memcpy (req.tint, m->tint, sizeof req.tint);
  if (m->tint_from_theme && theme_bg)
    {
      req.tint[0] = theme_bg->red;
      req.tint[1] = theme_bg->green;
      req.tint[2] = theme_bg->blue;
    }
  if (glass_context_get_reduce_transparency (context))
    {
      params[GLASS_PARAM_DISPLACEMENT_SCALE] = 0.0;
      params[GLASS_PARAM_BLUR_RADIUS] = MIN (MAX (params[GLASS_PARAM_BLUR_RADIUS] * 3.0, 8.0), 30.0);
      req.tint[3] = MAX (req.tint[3], 0.6f);
    }

  if (!glass_capture_rect_for_panel (rect, params[GLASS_PARAM_BLUR_RADIUS], &view_rect, &creq.rect))
    return FALSE;

  glass_renderer_make_current (self->renderer);
  creq.backdrop = backdrop;
  creq.content_key = glass_unwrap_node (backdrop, &dx, &dy);
  creq.key_dx = dx;
  creq.key_dy = dy;
  creq.scale = scale;
  creq.blur_radius = params[GLASS_PARAM_BLUR_RADIUS];
  creq.downscale = (int) params[GLASS_PARAM_BLUR_DOWNSCALE];
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
