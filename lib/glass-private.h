/* glass-private.h — what the library's parts share with each other.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "glass.h"
#include "glass-params-table.h"
#include "adaptive/glass-adaptive.h"
#include "render/glass-renderer.h"

G_BEGIN_DECLS

/* GLASS_DEBUG=flag,flag,... (comparisons and measurements; design.md §8.8) */
typedef enum {
  GLASS_DEBUG_HUD                 = 1 << 0,
  GLASS_DEBUG_NO_SUPERSAMPLE      = 1 << 1,
  GLASS_DEBUG_ESTIMATED_FOOTPRINT = 1 << 2,
  GLASS_DEBUG_NO_CACHE            = 1 << 3,
  GLASS_DEBUG_WINDOW_CAPTURE      = 1 << 4,   /* capture with the window's renderer */
  GLASS_DEBUG_GPU_TIME            = 1 << 5,   /* GPU time per pass, in the debug log */
} GlassDebugFlags;

GlassDebugFlags glass_get_debug_flags (void);

/* ── Style ── */
#define GLASS_TYPE_STYLE_NODE (glass_style_node_get_type ())
G_DECLARE_FINAL_TYPE (GlassStyleNode, glass_style_node, GLASS, STYLE_NODE, GtkWidget)
/* A never-drawn child whose CSS colour the parent reads. */
GtkWidget  *glass_style_node_new      (const char *css_name);

/* A row that draws each child inside a pill (glass-pill-box.c). */
#define GLASS_TYPE_PILL_BOX (glass_pill_box_get_type ())
G_DECLARE_FINAL_TYPE (GlassPillBox, glass_pill_box, GLASS, PILL_BOX, GtkWidget)
GtkWidget  *glass_pill_box_new             (void);
void        glass_pill_box_set_homogeneous (GlassPillBox *self,
                                            gboolean      homogeneous);
void        glass_pill_box_append          (GlassPillBox *self,
                                            GtkWidget    *child);
void        glass_pill_box_prepend         (GlassPillBox *self,
                                            GtkWidget    *child);
void        glass_pill_box_remove          (GlassPillBox *self,
                                            GtkWidget    *child);

/* GlassButtonGroup: whether any of its widgets is visible. */
gboolean    glass_button_group_has_visible_child (GlassButtonGroup *self);

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
/* Paints the backdrop colour only into what the glass captures, not on
 * screen: for small views inside other widgets (a switch's knob), whose
 * surroundings are drawn by the parent. */
void     glass_view_set_backdrop_capture_only (GlassView *self,
                                               gboolean   capture_only);
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
/* The panel's glass this frame, set by the view that drew it: a view inside
 * the panel puts it under its own backdrop (glass over glass across views). */
void     glass_panel_set_output      (GlassPanel             *self,
                                      GlassView              *view,
                                      const GlassLayerSource *output);
gboolean glass_panel_get_output      (GlassPanel       *self,
                                      GlassView       **view,
                                      GlassLayerSource *output);
/* The views inside the panel (a switch's): they draw again whenever the
 * panel's glass changes, since it is under their backdrop. */
void     glass_panel_add_nested_view    (GlassPanel *self,
                                         GlassView  *view);
void     glass_panel_remove_nested_view (GlassPanel *self,
                                         GlassView  *view);
/* The panel's own value for a key, used unless the app sets the key
 * explicitly (a switch's knob has no blur); NAN: the material's. */
void     glass_panel_set_param_default (GlassPanel  *self,
                                        GlassParamId param,
                                        double       value);
/* glass_context_resolve() for this panel: the explicit value, then the
 * panel's own, then the material's, then the default (design.md §11.2). */
void     glass_panel_resolve_params  (GlassPanel   *self,
                                      GlassContext *context,
                                      double        out[GLASS_N_PARAMS]);
/* The press "bulge" (GlassPanel:interactive): the glass and the child grow
 * by this factor around the centre. */
double   glass_panel_get_visual_scale (GlassPanel *self);
/* Drives the press by hand (a toggle group's plate is under its buttons). */
/* The corner radius the glass really has, px (a capsule's is half its
 * shorter side). */
double   glass_panel_effective_radius (GlassPanel            *self,
                                       const graphene_rect_t *bounds);
void     glass_panel_set_pressed     (GlassPanel *self,
                                      gboolean    pressed);
void     glass_panel_set_press_grow  (GlassPanel *self,
                                      double      grow_px,
                                      double      max_extra);
double   glass_panel_get_press       (GlassPanel *self);

/* 0..1: the pressed highlight of GlassButton. */
double   glass_panel_get_highlight   (GlassPanel *self);
void     glass_panel_set_highlight   (GlassPanel *self,
                                      double      highlight);
/* The panel's effective tint (rgb, strength) and whether it adapts. */
void     glass_panel_get_tint_rgba   (GlassPanel    *self,
                                      const GdkRGBA *theme_bg,
                                      float          out[4]);
gboolean glass_panel_uses_adaptive   (GlassPanel *self);

/* ── One pane of glass outside a view (popovers, dialogs; glass-standalone.c) ── */
typedef struct _GlassStandalone GlassStandalone;

GlassStandalone *glass_standalone_new      (void);
void             glass_standalone_free     (GlassStandalone *self);
/* Drops the GL objects and the renderer (on unrealize). */
void             glass_standalone_release  (GlassStandalone *self);
/* Glass of `rect` (widget coordinates) over `backdrop` (a node in the same
 * coordinates). FALSE if it could not be drawn: draw the plain look. */
gboolean         glass_standalone_draw     (GlassStandalone       *self,
                                            GtkWidget             *widget,
                                            GtkSnapshot           *snapshot,
                                            GskRenderNode         *backdrop,
                                            const graphene_rect_t *rect,
                                            double                 radius,
                                            GlassMaterial          material,
                                            const GdkRGBA         *theme_bg,
                                            gboolean               shadow);
GskRenderNode   *glass_standalone_backdrop (GtkWidget              *source,
                                            const graphene_point_t *offset);

G_END_DECLS
