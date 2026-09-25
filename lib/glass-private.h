/* glass-private.h — what the library's parts share with each other.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "glass.h"
#include "glass-params-table.h"
#include "adaptive/glass-adaptive.h"

G_BEGIN_DECLS

/* GLASS_DEBUG=flag,flag,... (comparisons and measurements; design.md §8.8) */
typedef enum {
  GLASS_DEBUG_HUD                 = 1 << 0,
  GLASS_DEBUG_NO_SUPERSAMPLE      = 1 << 1,
  GLASS_DEBUG_ESTIMATED_FOOTPRINT = 1 << 2,
  GLASS_DEBUG_NO_CACHE            = 1 << 3,
} GlassDebugFlags;

GlassDebugFlags glass_get_debug_flags (void);

/* ── Style ── */
#define GLASS_TYPE_STYLE_NODE (glass_style_node_get_type ())
G_DECLARE_FINAL_TYPE (GlassStyleNode, glass_style_node, GLASS, STYLE_NODE, GtkWidget)
/* A never-drawn child whose CSS colour the parent reads. */
GtkWidget  *glass_style_node_new      (const char *css_name);

void        glass_style_ensure        (GdkDisplay *display);
/* A CSS class giving glasspanel this border-radius (for the CSS fallback). */
const char *glass_style_radius_class  (GdkDisplay *display,
                                       double      radius);

/* ── Context ── */
/* The value of every key for a material: explicit, material, default. */
void     glass_context_resolve       (GlassContext *self,
                                      GlassMaterial material,
                                      double        out[GLASS_N_PARAMS]);
/* Bumped by every change that affects drawing. */
guint    glass_context_get_generation (GlassContext *self);
gboolean glass_context_get_high_contrast (GlassContext *self);
/* FULL or FALLBACK: the mode setting with GLASS_RENDERER applied. */
GlassRendererMode glass_context_get_wanted_renderer (GlassContext *self);

/* ── View <-> panel ── */
typedef enum {
  GLASS_PANEL_MODE_NONE,       /* not rooted */
  GLASS_PANEL_MODE_VIEW,       /* the view draws its glass */
  GLASS_PANEL_MODE_FALLBACK,   /* CSS draws it */
  GLASS_PANEL_MODE_NESTED,     /* inside another panel: no glass of its own */
} GlassPanelMode;

void     glass_view_register_panel   (GlassView  *self,
                                      GlassPanel *panel);
void     glass_view_unregister_panel (GlassView  *self,
                                      GlassPanel *panel);
gboolean glass_view_is_overlay_descendant (GlassView *self,
                                           GtkWidget *widget);
void     glass_view_set_edge         (GlassView       *self,
                                      GtkPositionType  position,
                                      GlassEdgeStyle   style,
                                      int              size);
/* Moves an overlay child by (dx, dy) without relayout (sliding sidebars). */
void     glass_view_set_overlay_offset (GlassView *self,
                                        GtkWidget *overlay,
                                        double     dx,
                                        double     dy);

void     glass_panel_set_mode        (GlassPanel     *self,
                                      GlassPanelMode  mode);
GlassPanelMode glass_panel_get_mode  (GlassPanel *self);
/* Called from the view's snapshot: stores the sample, applies it later. */
void     glass_panel_push_luma       (GlassPanel           *self,
                                      const GlassLumaStats *stats);
/* 0..1: the pressed highlight of GlassButton. */
double   glass_panel_get_highlight   (GlassPanel *self);
void     glass_panel_set_highlight   (GlassPanel *self,
                                      double      highlight);
/* The panel's effective tint (rgb, strength) and whether it adapts. */
void     glass_panel_get_tint_rgba   (GlassPanel    *self,
                                      const GdkRGBA *theme_bg,
                                      float          out[4]);
gboolean glass_panel_uses_adaptive   (GlassPanel *self);

G_END_DECLS
