/* glass-private.h — what the library's parts share with each other.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "glass.h"
#include "glass-jelly.h"
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

/* For a part that forwards GtkActionable to an inner widget: gives @widget
 * an action muxer of its own now. Without one, an action name set before
 * the part is in a window never reaches the window's actions (the inner
 * widget's muxer is left with no parent; docs/memo.md 地雷44). */
static inline void
glass_ensure_action_muxer (GtkWidget *widget)
{
  gtk_widget_insert_action_group (widget, "glass-private", NULL);
}

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

/* GlassToggleGroup, for GlassTabBar: a toggle showing @content (returns
 * its button). */
GtkWidget  *glass_toggle_group_append_item (GlassToggleGroup *self,
                                            const char       *name,
                                            GtkWidget        *content);

/* GlassButtonGroup: whether any of its widgets is visible. */
gboolean    glass_button_group_has_visible_child (GlassButtonGroup *self);

void        glass_style_ensure        (GdkDisplay *display);
/* A CSS class giving glasspanel this border-radius (for the CSS fallback). */
const char *glass_style_radius_class  (GdkDisplay *display,
                                       double      radius);
/* The same for a radius per corner (tl, tr, br, bl; negative: @radius's,
 * and a negative @radius: a capsule's). */
const char *glass_style_corner_radii_class (GdkDisplay   *display,
                                            double        radius,
                                            const double  radii[4]);

/* ── Context ── */
/* The index of @key, -1 if it is not a parameter. */
int      glass_param_find            (const char *key);
/* Checks a value for @key as set_param does (warns as @func, clamps, snaps
 * to the allowed values): its index with the value in @out, or -1. */
int      glass_param_check           (const char *func,
                                      const char *key,
                                      double      value,
                                      double     *out);
/* The value of every key for a material: explicit, material, default. */
void     glass_context_resolve       (GlassContext *self,
                                      GlassMaterial material,
                                      double        out[GLASS_N_PARAMS]);
/* A material's tint (rgb, @strength): the context's colour if set, else the
 * material's (@theme_bg, the theme's window or popover colour, for those
 * tinted from the theme). */
void     glass_context_resolve_tint  (GlassContext  *self,
                                      GlassMaterial  material,
                                      double         strength,
                                      const GdkRGBA *theme_bg,
                                      float          out[4]);
/* AdwStyleManager:dark: the materials' values for the dark appearance. */
gboolean glass_context_get_dark      (GlassContext *self);
/* A material's body and outline in the current appearance (spec/params.json
 * surfaces, outlines). @plain_body: the reference's body under its outline,
 * for a part's own glass that is a lens rather than a pane (knobs, plates). */
void     glass_context_resolve_surface (GlassContext     *self,
                                        GlassMaterial     material,
                                        gboolean          plain_body,
                                        GlassSurfaceSpec *out);
/* What a surface means for the capture's blur (with @params' blur-radius)
 * and for the glass pass. Either may be NULL. */
void     glass_surface_apply         (const GlassSurfaceSpec *surface,
                                      const double           params[GLASS_N_PARAMS],
                                      GlassBlurSpec         *blur,
                                      GlassSurfaceTerms     *terms);
/* GlassContext:reduce-transparency, applied to resolved @params and @tint:
 * no refraction, more blur, more tint. The same for every kind of glass. */
void     glass_reduce_transparency   (double        params[GLASS_N_PARAMS],
                                      float         tint[4]);
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
/* A style class a part relies on (.glass-button): unlike
 * gtk_widget_add_css_class(), it comes back when the app replaces the
 * classes (css-classes). The panel's own (shape, mode, colours) do too. */
void     glass_panel_add_own_class   (GlassPanel *self,
                                      const char *name);
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
/* glass_context_resolve() for this panel: the app's value for the panel
 * (glass_panel_set_param), then the context's, then the widget's own, then
 * the material's, then the default (design.md §11.2). */
void     glass_panel_resolve_params  (GlassPanel   *self,
                                      GlassContext *context,
                                      double        out[GLASS_N_PARAMS]);
/* The material's body and outline for this panel (glass_context_resolve_surface()). */
void     glass_panel_resolve_surface (GlassPanel       *self,
                                      GlassContext     *context,
                                      GlassSurfaceSpec *out);
/* A part's own glass that is a lens rather than a pane (a switch's knob, a
 * toggle group's plate): the reference's body, no frost, under the
 * material's outline. */
void     glass_panel_set_plain_body  (GlassPanel *self,
                                      gboolean    plain);
/* The press "bulge" (GlassPanel:interactive): the glass and the child grow
 * by this factor around the centre. */
double   glass_panel_get_visual_scale (GlassPanel *self);
/* How much of the child shows while the glass morphs or materializes
 * (design.md §6.8), 0..1. */
float    glass_panel_get_content_opacity (GlassPanel *self);
/* Drives the press by hand (a toggle group's plate is under its buttons). */
/* Whether any corner has a radius of its own (GlassPanel:top-left-radius...). */
gboolean glass_panel_has_corner_radii (GlassPanel *self);
/* Each corner's radius at @bounds: tl, tr, br, bl, at most half the
 * shorter side. */
void     glass_panel_resolve_corners (GlassPanel            *self,
                                      const graphene_rect_t *bounds,
                                      double                 out[4]);

/* Morphing (design.md §6.8). The view draws a morphing panel's glass
 * between where the morph started and @target (its place this frame):
 * TRUE with @rect and @corners if it is morphing. */
gboolean glass_panel_get_morph      (GlassPanel            *self,
                                     const graphene_rect_t *target,
                                     graphene_rect_t       *rect,
                                     double                 corners[4]);
/* A morph-id swap: the partner's glass, left behind as a drop that shrinks
 * away while ours travels (TRUE while there is one), to be fused with ours
 * over glass_panel_get_anchor_merge() px. */
gboolean glass_panel_get_morph_anchor (GlassPanel      *self,
                                       graphene_rect_t *rect,
                                       double          *radius);
double   glass_panel_get_anchor_merge (GlassPanel      *self);
/* A knob's glass hangs from its place on springs (the jelly, design.md
 * §6.9): it stretches as the knob travels and runs on a little when it
 * stops. The view passes the panel's place and draws the glass at what
 * comes back. */
void     glass_panel_set_follows     (GlassPanel      *self,
                                      gboolean         follows);
void     glass_panel_follow          (GlassPanel      *self,
                                      graphene_rect_t *rect);
/* Materializing (design.md §6.8): how much of the glass there is (0..1:
 * the lens, tint, frost, outline and shadow scale with it, and the glass
 * fades with it), and how much larger than the panel it is drawn. */
double   glass_panel_get_visibility (GlassPanel            *self,
                                     double                *grow);
/* A hidden panel that is still drawn, shrinking into its neighbour or
 * dissolving where it was: TRUE with its glass this frame, in @view's
 * coordinates, and glass_panel_get_ghost_visibility() of it. */
gboolean glass_panel_get_ghost      (GlassPanel            *self,
                                     GlassView             *view,
                                     graphene_rect_t       *rect,
                                     double                 corners[4]);
double   glass_panel_get_ghost_visibility (GlassPanel       *self);
/* Whether @self has been drawn since it was last mapped: a panel mapped
 * before that came on screen with the view, and does not materialize. */
gboolean glass_view_has_drawn_since_map (GlassView *self);
/* What the view drew for the panel this frame (morphs start from it). */
void     glass_panel_set_drawn      (GlassPanel            *self,
                                     const graphene_rect_t *rect,
                                     double                 radius);
/* The panels registered with @self, by index (NULL past the end). */
GlassPanel *glass_view_get_panel    (GlassView *self,
                                     guint      index);
/* The corner radius the glass really has, px (a capsule's is half its
 * shorter side; with radii per corner, the largest). */
double   glass_panel_effective_radius (GlassPanel            *self,
                                       const graphene_rect_t *bounds);
void     glass_panel_set_pressed     (GlassPanel *self,
                                      gboolean    pressed);
/* Like set_pressed, to @level (1: pressed): a dragged plate swells more. */
void     glass_panel_set_press_level (GlassPanel *self,
                                      double      level);
void     glass_panel_set_press_grow  (GlassPanel *self,
                                      double      grow_px,
                                      double      max_extra);
double   glass_panel_get_press       (GlassPanel *self);

/* 0..1: the pressed highlight of GlassButton. */
double   glass_panel_get_highlight   (GlassPanel *self);
void     glass_panel_set_highlight   (GlassPanel *self,
                                      double      highlight);
/* The panel's effective tint (rgb, strength; @params resolved for it, for
 * the strength) and whether it adapts. */
void     glass_panel_get_tint_rgba   (GlassPanel    *self,
                                      const double   params[GLASS_N_PARAMS],
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
                                            const double           radii[4],   /* tl, tr, br, bl */
                                            GlassMaterial          material,
                                            const GdkRGBA         *theme_bg,
                                            gboolean               shadow);
/* The same with a drop fused to it (@drop NULL: none), over @merge px: a
 * menu coming out of its button (design.md §6.8). */
gboolean         glass_standalone_draw_drop (GlassStandalone       *self,
                                             GtkWidget             *widget,
                                             GtkSnapshot           *snapshot,
                                             GskRenderNode         *backdrop,
                                             const graphene_rect_t *rect,
                                             const double           radii[4],
                                             const graphene_rect_t *drop,
                                             double                 drop_radius,
                                             double                 merge,
                                             GlassMaterial          material,
                                             const GdkRGBA         *theme_bg,
                                             gboolean               shadow);
GskRenderNode   *glass_standalone_backdrop (GtkWidget              *source,
                                            const graphene_point_t *offset);

/* ── Menus that come out of their button (design.md §6.8) ── */
/* Where the popover's glass comes out of and goes back into: a rectangle
 * in the root's coordinates (the button, a pill of @radius), or NULL. The
 * popover's surface must reach it (glass_popover_set_reach()). */
void             glass_popover_set_origin  (GlassPopover          *self,
                                            const graphene_rect_t *rect,
                                            double                 radius);
/* Room above the contents, px: the surface reaches back over the button. */
void             glass_popover_set_reach   (GlassPopover          *self,
                                            int                    reach);
/* Closes the popover, pouring its glass back into its origin first. */
void             glass_popover_dismiss     (GlassPopover          *self);

G_END_DECLS
