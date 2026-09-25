/* s1-glass-view.h — the S1 spike's glass host widget.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define S1_TYPE_GLASS_VIEW (s1_glass_view_get_type ())
G_DECLARE_FINAL_TYPE (S1GlassView, s1_glass_view, S1, GLASS_VIEW, GtkWidget)

typedef struct {
  gboolean glass_enabled;     /* FALSE = draw only the panels' foreground (A/B) */
  gboolean use_cache;         /* reuse the capture / the finished glass when nothing changed */
  gboolean private_renderer;  /* capture with a dedicated GskRenderer instead of the window's */
  double   blur_radius;       /* px; in-app default 2 (the extension's dock uses 5) */
  int      downscale;         /* capture scale 1/2 (default) or 1/4 */
  gboolean capture_full;      /* capture at full resolution and box-downsample in GL, as the extension does */
  int      debug_view;        /* glass.frag's debug_view: 0 off, 1 masks, 2 raw masks */
  gboolean supersample;       /* 4 rotated-grid samples of glass.frag per pixel */
  int      rim_samples;       /* samples per pixel where the lens acts (only if > the above) */
  gboolean measured_footprint;  /* the core's measured lens footprint instead of the reference's estimate */
  double   footprint_px;      /* its half-width per sample, px; 0 = half the sample spacing */
  gboolean use_reference;     /* the unmodified reference shader on a virtual canvas, instead of the core */
  double   shadow_radius;     /* px */
  double   shadow_intensity;
} S1Options;

typedef struct {
  guint  snapshots;           /* times the view was snapshotted */
  guint  glass_passes;        /* glass.frag runs */
  guint  captures;            /* render_texture + download + upload + blur */
  guint  capture_hits;        /* capture and blur reused, glass re-run */
  guint  full_hits;           /* finished glass texture reused as-is */
  guint  pool_exhausted;      /* no free output texture: the previous one was reused */
  gint64 us_total;            /* glass work per snapshot, summed */
  gint64 us_total_max;
  gint64 us_render_texture;
  gint64 us_download;
  gint64 us_upload;
  gint64 us_gl;               /* blur + glass pass submission */
} S1Stats;

GtkWidget  *s1_glass_view_new             (void);
void        s1_glass_view_set_content     (S1GlassView     *self,
                                           GtkWidget       *content);
void        s1_glass_view_add_panel       (S1GlassView     *self,
                                           GtkWidget       *panel);
S1Options  *s1_glass_view_get_options     (S1GlassView     *self);
void        s1_glass_view_options_changed (S1GlassView     *self);
void        s1_glass_view_take_stats      (S1GlassView     *self,
                                           S1Stats         *out);
const char *s1_glass_view_get_gl_info     (S1GlassView     *self);
/* The optical parameters (the extension's names without the glass- prefix,
 * underscores: rim_width, edge_smoothing, ...). Lengths in logical px. */
gboolean    s1_glass_view_set_param       (S1GlassView     *self,
                                           const char      *name,
                                           double           value);
double      s1_glass_view_get_param       (S1GlassView     *self,
                                           const char      *name);
void        s1_glass_view_reset_params    (S1GlassView     *self);
/* "crisp-soft" (in-app default), "crisp", "crisp-strong", "thinner",
 * "extension" (the extension's values). */
gboolean    s1_glass_view_apply_preset    (S1GlassView     *self,
                                           const char      *name);
const char *s1_glass_view_next_preset     (const char      *name);

G_END_DECLS
