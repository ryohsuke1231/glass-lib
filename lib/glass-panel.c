/* glass-panel.c — a pane of glass with one child (design.md §6, §7.2, §12).
 *
 * The panel does not draw its glass: the GlassView above it does (see
 * glass-view.c). The panel finds that view when it is rooted and registers
 * with it; outside a view (or on the content side of one), with the CSS
 * fallback, or in high contrast, its CSS draws frosted glass instead.
 *
 * SPDX-License-Identifier: MIT
 */
#include "glass-private.h"

#include <adwaita.h>
#include <math.h>
#include <string.h>

/**
 * GlassPanel:
 *
 * A pane of glass with one child: icons, labels, flat buttons.
 *
 * Put panels among the overlay children of a [class@View]: the view draws
 * them as glass that refracts its content. Anywhere else a panel is drawn
 * as frosted glass with CSS.
 *
 * The colour of what is on the panel follows what is under it
 * ([property@Panel:adaptive]): the panel gets the style class `.glass-light`
 * (dark foreground) or `.glass-dark` (light foreground).
 *
 * ## CSS nodes
 *
 * `GlassPanel` has a single CSS node with name `glasspanel`. It gets the
 * style class `.glass-fallback` when CSS draws the glass, `.glass-nested`
 * inside another panel (glass is not drawn on glass), and `.glass-light` /
 * `.glass-dark` from the adaptive colours.
 *
 * ## Accessibility
 *
 * `GlassPanel` uses the %GTK_ACCESSIBLE_ROLE_GROUP role.
 */

typedef struct {
  GtkWidget          *child;
  GtkWidget          *accent_node;      /* never drawn: resolves the accent (PROMINENT) */
  GlassMaterial       material;
  double              corner_radius;
  double              corner_radii[4];  /* tl, tr, br, bl; -1 = corner_radius */
  GdkRGBA             tint;
  gboolean            tint_set;
  gboolean            has_shadow;
  GlassAdaptiveMode   adaptive;
  GlassAppearance     appearance;

  GlassView          *view;             /* registered with, not owned */
  GlassPanelMode      mode;
  gboolean            high_contrast;    /* has .glass-high-contrast */
  const char         *shape_class;      /* the CSS radius class in use */
  GPtrArray          *own_classes;      /* interned: a part's classes (.glass-button) */
  guint               changing_classes; /* > 0: our own changes, not the app's */

  GlassAdaptiveState  astate;
  GlassLumaStats      last_luma;
  gboolean            have_luma;
  GlassAppearance     pending;
  guint               apply_tick;
  guint               settle_source;

  double              highlight;

  gboolean            interactive;
  double              press;            /* 0..1 (more while a plate is dragged), sprung */
  double              press_grow_px;    /* how much wider the glass gets, pressed */
  double              press_max_extra;  /* ... at most this fraction */
  AdwAnimation       *press_anim;
  GtkEventController *press_controller;

  GlassView          *output_view;     /* not owned; valid while registered */
  GlassLayerSource    output;
  gboolean            has_output;
  GPtrArray          *nested_views;    /* GlassViews inside, not owned (they
                                        * remove themselves when unrooted) */

  /* Morphing (design.md §6.8). The glass as the view last drew it, in the
   * view's coordinates: where a morph to or from this panel starts. */
  char               *morph_id;
  gboolean            has_drawn;
  graphene_rect_t     drawn_rect;
  double              drawn_radius;
  gint64              hidden_at;        /* µs, the last unmap */
  gint64              taken_at;         /* µs, when a partner took our glass */
  /* Appearing: from morph_from to where the panel is, on the jelly's
   * springs (glass-jelly.c); morph_t is how far it has come. A morph-id
   * swap leaves the partner's glass behind as a drop that shrinks away
   * (anchor), joined to ours while they part. */
  GlassJelly          jelly;
  double              morph_t;          /* 0..1 */
  gboolean            morphing;
  graphene_rect_t     morph_from;
  double              morph_from_radius;
  GtkWidget          *morph_source;     /* weak: a shown source, read in the first frame */
  gboolean            has_anchor;
  guint               motion_tick;      /* redraws the view while the glass moves */
  /* A knob's glass hangs from its place on the jelly (design.md §6.9). */
  gboolean            follows;
  gboolean            follow_moving;
  /* Appearing without a partner: SwiftUI's .materialize (design.md §6.8). */
  AdwAnimation       *materialize_anim;
  double              materialize_t;    /* 0..1 */
  gboolean            materializing;
  /* Hidden, still drawn: shrinking into ghost_into on the jelly, or with no
   * one to go to, dissolving (ghost_into NULL). */
  AdwAnimation       *ghost_anim;
  double              ghost_t;
  gboolean            ghost;
  graphene_rect_t     ghost_from;
  double              ghost_from_radius;
  GtkWidget          *ghost_into;       /* weak */

  double              param_value[GLASS_N_PARAMS];     /* the app's (set_param); NAN: none */
  double              param_default[GLASS_N_PARAMS];   /* the widget's own; NAN: none */
  gboolean            plain_body;       /* a lens, not a pane: no frost (knobs, plates) */
} GlassPanelPrivate;

enum {
  PROP_0,
  PROP_CHILD,
  PROP_MATERIAL,
  PROP_CORNER_RADIUS,
  PROP_TOP_LEFT_RADIUS,
  PROP_TOP_RIGHT_RADIUS,
  PROP_BOTTOM_RIGHT_RADIUS,
  PROP_BOTTOM_LEFT_RADIUS,
  PROP_TINT,
  PROP_HAS_SHADOW,
  PROP_ADAPTIVE,
  PROP_APPEARANCE,
  PROP_INTERACTIVE,
  PROP_MORPH_ID,
  N_PROPS
};

static GParamSpec *props[N_PROPS];

static void glass_panel_buildable_init (GtkBuildableIface *iface);

G_DEFINE_TYPE_WITH_CODE (GlassPanel, glass_panel, GTK_TYPE_WIDGET,
                         G_ADD_PRIVATE (GlassPanel)
                         G_IMPLEMENT_INTERFACE (GTK_TYPE_BUILDABLE, glass_panel_buildable_init))

static GtkBuildableIface *parent_buildable_iface;

#define PRIV(self) ((GlassPanelPrivate *) glass_panel_get_instance_private (GLASS_PANEL (self)))

/* ── Modes and CSS classes ────────────────────────────────────────────────── */

static void
update_shape_class (GlassPanel *self)
{
  GlassPanelPrivate *priv = PRIV (self);
  const char *wanted = NULL;
  GtkWidget *widget = GTK_WIDGET (self);

  /* The CSS shape is the glass's shape in every mode: it is what the CSS
   * fallback draws, and what the panel clips its child to (overflow is
   * hidden). Capsules are the stylesheet's default. */
  if (glass_panel_has_corner_radii (self))
    wanted = glass_style_corner_radii_class (gtk_widget_get_display (widget), priv->corner_radius,
                                             priv->corner_radii);
  else if (priv->corner_radius >= 0.0)
    wanted = glass_style_radius_class (gtk_widget_get_display (widget), priv->corner_radius);

  if (wanted == priv->shape_class)
    return;
  priv->changing_classes++;
  if (priv->shape_class)
    gtk_widget_remove_css_class (widget, priv->shape_class);
  priv->shape_class = wanted;
  if (wanted)
    gtk_widget_add_css_class (widget, wanted);
  priv->changing_classes--;
}

/* One corner's radius: 0 top left, 1 top right, 2 bottom right, 3 bottom
 * left (the order of CSS border-radius). */
static void
set_corner (GlassPanel *self,
            int         corner,
            double      radius)
{
  GlassPanelPrivate *priv = PRIV (self);

  radius = radius < 0.0 ? -1.0 : radius;
  if (priv->corner_radii[corner] == radius)
    return;
  priv->corner_radii[corner] = radius;
  update_shape_class (self);
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_TOP_LEFT_RADIUS + corner]);
  gtk_widget_queue_draw (GTK_WIDGET (self));
}

gboolean
glass_panel_has_corner_radii (GlassPanel *self)
{
  GlassPanelPrivate *priv = PRIV (self);

  for (int i = 0; i < 4; i++)
    if (priv->corner_radii[i] >= 0.0)
      return TRUE;
  return FALSE;
}

static void set_appearance (GlassPanel *self, GlassAppearance appearance);
static void stop_ghost (GlassPanel *self);
static void stop_motion (GlassPanel *self);

/* Whether a view measures what is under the panel: its glass, or its CSS
 * fallback (the view samples that itself); not in high contrast (opaque). */
static gboolean
is_measured (GlassPanel *self)
{
  GlassPanelPrivate *priv = PRIV (self);

  if (priv->view == NULL || glass_context_get_high_contrast (glass_context_get_default ()))
    return FALSE;
  return priv->mode == GLASS_PANEL_MODE_VIEW || priv->mode == GLASS_PANEL_MODE_FALLBACK;
}

void
glass_panel_set_mode (GlassPanel     *self,
                      GlassPanelMode  mode)
{
  GlassPanelPrivate *priv = PRIV (self);
  GtkWidget *widget = GTK_WIDGET (self);
  gboolean hc = glass_context_get_high_contrast (glass_context_get_default ());

  if (mode == GLASS_PANEL_MODE_NONE)
    priv->view = NULL;
  if (mode != GLASS_PANEL_MODE_VIEW)
    priv->has_output = FALSE;

  priv->changing_classes++;
  gtk_widget_remove_css_class (widget, "glass-fallback");
  gtk_widget_remove_css_class (widget, "glass-nested");
  gtk_widget_remove_css_class (widget, "glass-high-contrast");

  /* Outside a view there is nothing to register with: CSS draws it. */
  if (mode == GLASS_PANEL_MODE_NONE && gtk_widget_get_root (widget))
    mode = GLASS_PANEL_MODE_FALLBACK;

  if (mode == GLASS_PANEL_MODE_FALLBACK)
    gtk_widget_add_css_class (widget, "glass-fallback");
  else if (mode == GLASS_PANEL_MODE_NESTED)
    gtk_widget_add_css_class (widget, "glass-nested");
  priv->high_contrast = hc && mode != GLASS_PANEL_MODE_NESTED;
  if (priv->high_contrast)
    gtk_widget_add_css_class (widget, "glass-high-contrast");

  priv->mode = mode;
  update_shape_class (self);
  priv->changing_classes--;

  /* Only the panels of a view are measured (with the CSS fallback too, by
   * the view's own small samples: design.md §12.1); the others keep the
   * theme's colours. */
  if (!is_measured (self))
    set_appearance (self, GLASS_APPEARANCE_UNKNOWN);
}

GlassPanelMode
glass_panel_get_mode (GlassPanel *self)
{
  return PRIV (self)->mode;
}

/* An app that sets css-classes (GJS and Python constructors do, with
 * `css_classes: [...]`) replaces every class, ours too: the shape class
 * would go, and the child would be clipped to a capsule. Put ours back. */
static void
restore_classes (GlassPanel *self)
{
  GlassPanelPrivate *priv = PRIV (self);
  GtkWidget *widget = GTK_WIDGET (self);
  const char *wanted[8];
  guint n = 0;

  if (priv->changing_classes > 0)
    return;
  priv->changing_classes++;

  if (priv->shape_class)
    wanted[n++] = priv->shape_class;
  if (priv->mode == GLASS_PANEL_MODE_FALLBACK)
    wanted[n++] = "glass-fallback";
  else if (priv->mode == GLASS_PANEL_MODE_NESTED)
    wanted[n++] = "glass-nested";
  if (priv->high_contrast)
    wanted[n++] = "glass-high-contrast";
  if (priv->appearance == GLASS_APPEARANCE_LIGHT)
    wanted[n++] = "glass-light";
  else if (priv->appearance == GLASS_APPEARANCE_DARK)
    wanted[n++] = "glass-dark";
  if (glass_material_specs[priv->material].tint_from_accent)
    wanted[n++] = "glass-prominent";

  for (guint i = 0; i < n; i++)
    if (!gtk_widget_has_css_class (widget, wanted[i]))
      gtk_widget_add_css_class (widget, wanted[i]);
  for (guint i = 0; priv->own_classes && i < priv->own_classes->len; i++)
    if (!gtk_widget_has_css_class (widget, g_ptr_array_index (priv->own_classes, i)))
      gtk_widget_add_css_class (widget, g_ptr_array_index (priv->own_classes, i));

  priv->changing_classes--;
}

static void
css_classes_changed (GlassPanel *self)
{
  restore_classes (self);
}

void
glass_panel_add_own_class (GlassPanel *self,
                           const char *name)
{
  GlassPanelPrivate *priv = PRIV (self);
  const char *interned = g_intern_string (name);

  if (priv->own_classes == NULL)
    priv->own_classes = g_ptr_array_new ();
  if (!g_ptr_array_find (priv->own_classes, interned, NULL))
    g_ptr_array_add (priv->own_classes, (gpointer) interned);
  gtk_widget_add_css_class (GTK_WIDGET (self), interned);
}

static void
find_view (GlassPanel *self)
{
  GlassPanelPrivate *priv = PRIV (self);
  GtkWidget *widget = GTK_WIDGET (self);
  GtkWidget *parent = gtk_widget_get_parent (widget);
  GtkWidget *view = parent ? gtk_widget_get_ancestor (parent, GLASS_TYPE_VIEW) : NULL;

  /* A panel inside another panel is glass over glass: the view draws it in
   * a later layer (design.md §6.7). */
  if (view && glass_view_is_overlay_descendant (GLASS_VIEW (view), widget))
    {
      priv->view = GLASS_VIEW (view);
      glass_view_register_panel (priv->view, self);   /* sets the mode */
      return;
    }

  /* No view, or on its content side: the content layer is not where glass
   * goes (design.md §7.2), and nothing would draw it there. */
  glass_panel_set_mode (self, GLASS_PANEL_MODE_FALLBACK);
}

static void
glass_panel_root (GtkWidget *widget)
{
  GlassPanel *self = GLASS_PANEL (widget);

  GTK_WIDGET_CLASS (glass_panel_parent_class)->root (widget);
  glass_style_ensure (gtk_widget_get_display (widget));
  find_view (self);
}

static void
glass_panel_unroot (GtkWidget *widget)
{
  GlassPanel *self = GLASS_PANEL (widget);
  GlassPanelPrivate *priv = PRIV (self);

  /* A morph's glass is in the view's coordinates: none of it carries over. */
  stop_ghost (self);
  stop_motion (self);
  g_clear_weak_pointer (&priv->morph_source);
  priv->morphing = FALSE;
  priv->has_anchor = FALSE;
  priv->follow_moving = FALSE;
  glass_jelly_stop (&priv->jelly);
  if (priv->materializing)
    {
      priv->materializing = FALSE;
      adw_animation_skip (priv->materialize_anim);
    }
  priv->has_drawn = FALSE;

  if (priv->view)
    glass_view_unregister_panel (priv->view, self);
  priv->view = NULL;
  priv->mode = GLASS_PANEL_MODE_NONE;
  priv->has_output = FALSE;
  g_clear_handle_id (&priv->settle_source, g_source_remove);
  if (priv->apply_tick)
    {
      gtk_widget_remove_tick_callback (widget, priv->apply_tick);
      priv->apply_tick = 0;
    }

  GTK_WIDGET_CLASS (glass_panel_parent_class)->unroot (widget);
}

/* ── Adaptive colours (design.md §12) ─────────────────────────────────────── */

static void
set_appearance (GlassPanel      *self,
                GlassAppearance  appearance)
{
  GlassPanelPrivate *priv = PRIV (self);
  GtkWidget *widget = GTK_WIDGET (self);

  if (priv->appearance == appearance)
    return;

  priv->changing_classes++;
  gtk_widget_remove_css_class (widget, "glass-light");
  gtk_widget_remove_css_class (widget, "glass-dark");
  if (appearance == GLASS_APPEARANCE_LIGHT)
    gtk_widget_add_css_class (widget, "glass-light");
  else if (appearance == GLASS_APPEARANCE_DARK)
    gtk_widget_add_css_class (widget, "glass-dark");
  priv->changing_classes--;

  priv->appearance = appearance;
  if (appearance == GLASS_APPEARANCE_UNKNOWN)
    glass_adaptive_state_init (&priv->astate);
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_APPEARANCE]);
}

static int
preference (GlassPanel *self)
{
  switch (PRIV (self)->adaptive)
    {
    case GLASS_ADAPTIVE_MODE_PREFER_LIGHT:
      return GLASS_ADAPTIVE_PREF_LIGHT;
    case GLASS_ADAPTIVE_MODE_PREFER_DARK:
      return GLASS_ADAPTIVE_PREF_DARK;
    default:
      return GLASS_ADAPTIVE_PREF_NONE;
    }
}

static GlassAppearance
decide (GlassPanel *self, gint64 now)
{
  GlassPanelPrivate *priv = PRIV (self);
  int result = glass_adaptive_update (&priv->astate, &priv->last_luma, now, preference (self));

  return result == GLASS_ADAPTIVE_LIGHT ? GLASS_APPEARANCE_LIGHT
         : result == GLASS_ADAPTIVE_DARK ? GLASS_APPEARANCE_DARK
         : GLASS_APPEARANCE_UNKNOWN;
}

static gboolean settle (gpointer data);

static void
arm_settle (GlassPanel *self)
{
  GlassPanelPrivate *priv = PRIV (self);

  /* The smoothing lags the last sample; when nothing redraws it would stay
   * behind a backdrop that just stopped moving. Feed that sample again. */
  g_clear_handle_id (&priv->settle_source, g_source_remove);
  if (glass_adaptive_is_settling (&priv->astate, &priv->last_luma) ||
      priv->astate.last_flip_us == priv->astate.last_sample_us)
    priv->settle_source = g_timeout_add (100, settle, self);
}

static gboolean
settle (gpointer data)
{
  GlassPanel *self = data;
  GlassPanelPrivate *priv = PRIV (self);

  priv->settle_source = 0;
  if (!priv->have_luma || !glass_panel_uses_adaptive (self) || !is_measured (self))
    return G_SOURCE_REMOVE;

  set_appearance (self, decide (self, g_get_monotonic_time ()));
  arm_settle (self);
  return G_SOURCE_REMOVE;
}

static gboolean
apply_pending (GtkWidget     *widget,
               GdkFrameClock *clock,
               gpointer       data)
{
  GlassPanel *self = GLASS_PANEL (widget);
  GlassPanelPrivate *priv = PRIV (self);

  priv->apply_tick = 0;
  if (glass_panel_uses_adaptive (self) && is_measured (self))
    set_appearance (self, priv->pending);
  arm_settle (self);
  return G_SOURCE_REMOVE;
}

void
glass_panel_push_luma (GlassPanel           *self,
                       const GlassLumaStats *stats)
{
  GlassPanelPrivate *priv = PRIV (self);

  if (!glass_panel_uses_adaptive (self))
    return;

  priv->last_luma = *stats;
  priv->have_luma = TRUE;
  priv->pending = decide (self, g_get_monotonic_time ());

  /* In the snapshot: never touch the style here (design.md §5.3-5). The
   * class changes before the next frame; the colour transition hides the
   * frame of delay, and the glass itself is not delayed. */
  if (priv->apply_tick == 0)
    priv->apply_tick = gtk_widget_add_tick_callback (GTK_WIDGET (self), apply_pending, NULL, NULL);
}

gboolean
glass_panel_uses_adaptive (GlassPanel *self)
{
  GlassPanelPrivate *priv = PRIV (self);

  return priv->adaptive != GLASS_ADAPTIVE_MODE_OFF &&
         glass_material_specs[priv->material].adaptive;
}

/* ── Glass parameters for the view ────────────────────────────────────────── */

void
glass_panel_set_output (GlassPanel             *self,
                        GlassView              *view,
                        const GlassLayerSource *output)
{
  GlassPanelPrivate *priv = PRIV (self);
  gboolean changed = (output != NULL) != priv->has_output || view != priv->output_view ||
                     (output && (output->id != priv->output.id || output->tex != priv->output.tex ||
                                 !graphene_rect_equal (&output->rect, &priv->output.rect)));

  priv->output_view = view;
  priv->has_output = output != NULL;
  if (output)
    priv->output = *output;

  /* The views inside draw again with the new glass under them. GTK caches
   * their nodes, so they would otherwise keep the old glass until something
   * in them changes. From the view's snapshot (the usual caller) this is
   * the same frame: GTK clears draw_needed only after a widget's snapshot,
   * so the queue stops at the drawing view, and the overlays it draws next
   * are drawn anew, with no extra frame (docs/memo.md 地雷24). */
  if (changed && priv->nested_views)
    for (guint i = 0; i < priv->nested_views->len; i++)
      gtk_widget_queue_draw (g_ptr_array_index (priv->nested_views, i));
}

void
glass_panel_add_nested_view (GlassPanel *self,
                             GlassView  *view)
{
  GlassPanelPrivate *priv = PRIV (self);

  if (priv->nested_views == NULL)
    priv->nested_views = g_ptr_array_new ();
  if (!g_ptr_array_find (priv->nested_views, view, NULL))
    g_ptr_array_add (priv->nested_views, view);
}

void
glass_panel_remove_nested_view (GlassPanel *self,
                                GlassView  *view)
{
  GlassPanelPrivate *priv = PRIV (self);

  if (priv->nested_views)
    g_ptr_array_remove (priv->nested_views, view);
}

void
glass_panel_set_param_default (GlassPanel  *self,
                               GlassParamId param,
                               double       value)
{
  GlassPanelPrivate *priv = PRIV (self);

  g_return_if_fail (param < GLASS_N_PARAMS);

  priv->param_default[param] = value;
  gtk_widget_queue_draw (GTK_WIDGET (self));
}

void
glass_panel_resolve_params (GlassPanel   *self,
                            GlassContext *context,
                            double        out[GLASS_N_PARAMS])
{
  GlassPanelPrivate *priv = PRIV (self);

  glass_context_resolve (context, priv->material, out);
  for (int i = 0; i < GLASS_N_PARAMS; i++)
    {
      if (!isnan (priv->param_value[i]))
        out[i] = priv->param_value[i];
      else if (!isnan (priv->param_default[i]) && !glass_context_is_param_set (context, glass_param_specs[i].key))
        out[i] = priv->param_default[i];
    }
}

void
glass_panel_set_plain_body (GlassPanel *self,
                            gboolean    plain)
{
  PRIV (self)->plain_body = plain;
  gtk_widget_queue_draw (GTK_WIDGET (self));
}

void
glass_panel_resolve_surface (GlassPanel       *self,
                             GlassContext     *context,
                             GlassSurfaceSpec *out)
{
  GlassPanelPrivate *priv = PRIV (self);

  glass_context_resolve_surface (context, priv->material, priv->plain_body, out);
}

gboolean
glass_panel_get_output (GlassPanel       *self,
                        GlassView       **view,
                        GlassLayerSource *output)
{
  GlassPanelPrivate *priv = PRIV (self);

  if (!priv->has_output || priv->mode != GLASS_PANEL_MODE_VIEW)
    return FALSE;
  *view = priv->output_view;
  *output = priv->output;
  return TRUE;
}

/* ── Press (GlassPanel:interactive) ─────────────────────────────────────────
 * Pressing anywhere in an interactive panel makes its glass (and what is on
 * it) grow a little, lighter, and spring back on release, like Apple's
 * interactive glass (design.md §6.7). */

double
glass_panel_effective_radius (GlassPanel            *self,
                              const graphene_rect_t *bounds)
{
  GlassPanelPrivate *priv = PRIV (self);
  double half = MIN (bounds->size.width, bounds->size.height) / 2.0;
  double r = priv->corner_radius < 0.0 ? half : MIN (priv->corner_radius, half);
  double largest = 0.0;

  if (!glass_panel_has_corner_radii (self))
    return r;
  /* Per corner: the largest (fused shapes have one radius each). */
  for (int i = 0; i < 4; i++)
    largest = MAX (largest, priv->corner_radii[i] >= 0.0 ? MIN (priv->corner_radii[i], half) : r);
  return largest;
}

double
glass_panel_get_visual_scale (GlassPanel *self)
{
  GlassPanelPrivate *priv = PRIV (self);
  int w = gtk_widget_get_width (GTK_WIDGET (self));
  int h = gtk_widget_get_height (GTK_WIDGET (self));

  if (priv->press <= 0.0 || w <= 0 || h <= 0)
    return 1.0;
  /* A fixed growth of the longer side: small buttons pop, long bars barely
   * swell. */
  return 1.0 + priv->press * MIN (priv->press_max_extra, priv->press_grow_px / MAX (w, h));
}

static void
press_value (double value, gpointer data)
{
  GlassPanel *self = data;

  PRIV (self)->press = MAX (value, 0.0);
  gtk_widget_queue_draw (GTK_WIDGET (self));
}

void
glass_panel_set_pressed (GlassPanel *self,
                         gboolean    pressed)
{
  glass_panel_set_press_level (self, pressed ? 1.0 : 0.0);
}

void
glass_panel_set_press_level (GlassPanel *self,
                             double      level)
{
  GlassPanelPrivate *priv = PRIV (self);
  AdwSpringParams *spring;

  if (priv->press_anim == NULL)
    {
      AdwAnimationTarget *target = adw_callback_animation_target_new (press_value, self, NULL);

      priv->press_anim = adw_spring_animation_new (GTK_WIDGET (self), 0.0, 1.0,
                                                   adw_spring_params_new (1.0, 1.0, 600.0), target);
    }

  /* In fast and firm; out with a small overshoot. */
  spring = level > 0.0 ? adw_spring_params_new (0.9, 1.0, 900.0) : adw_spring_params_new (0.45, 1.0, 420.0);
  adw_spring_animation_set_spring_params (ADW_SPRING_ANIMATION (priv->press_anim), spring);
  adw_spring_params_unref (spring);
  adw_spring_animation_set_value_from (ADW_SPRING_ANIMATION (priv->press_anim), priv->press);
  adw_spring_animation_set_value_to (ADW_SPRING_ANIMATION (priv->press_anim), level);
  adw_spring_animation_set_initial_velocity (ADW_SPRING_ANIMATION (priv->press_anim), 0.0);
  adw_animation_play (priv->press_anim);
}

void
glass_panel_set_press_grow (GlassPanel *self,
                            double      grow_px,
                            double      max_extra)
{
  PRIV (self)->press_grow_px = grow_px;
  PRIV (self)->press_max_extra = max_extra;
}

double
glass_panel_get_press (GlassPanel *self)
{
  return PRIV (self)->press;
}

/* Watches presses in the capture phase without taking them: the buttons on
 * the panel still get every event. */
static gboolean
press_event (GtkEventControllerLegacy *controller,
             GdkEvent                 *event,
             GlassPanel               *self)
{
  switch ((int) gdk_event_get_event_type (event))
    {
    case GDK_BUTTON_PRESS:
    case GDK_TOUCH_BEGIN:
      glass_panel_set_pressed (self, TRUE);
      break;
    case GDK_BUTTON_RELEASE:
    case GDK_TOUCH_END:
    case GDK_TOUCH_CANCEL:
    case GDK_GRAB_BROKEN:
      glass_panel_set_pressed (self, FALSE);
      break;
    default:
      break;
    }
  return GDK_EVENT_PROPAGATE;
}

/* ── Morphing (GlassPanel:morph-id, design.md §6.8) ─────────────────────────
 * A panel that appears morphs from a partner's glass: a hidden panel with
 * the same morph-id (its glass becomes ours, and stays behind as a drop that
 * shrinks away), or else, in a GlassGroup, the neighbouring panel (a drop
 * splitting off). With neither it materializes. A panel that hides leaves a
 * ghost: its glass, without the content, shrinking into its neighbour in a
 * group, or else dissolving where it was. The view reads all of it while it
 * draws (glass_panel_get_morph(), glass_panel_get_ghost() ...), and that is
 * where the jelly (glass-jelly.c) is stepped, with the frame's time and the
 * frame's place for the panel (§5.3-2); the ticks only ask for frames. */

/* Showing and hiding in one go (hide a button, show a field) happens in one
 * main loop iteration; this much later it is no longer a swap. */
#define MORPH_WINDOW_US (250 * 1000)
/* A ghost with no one to go to dissolves over this (SwiftUI's
 * dematerialize, measured by liquid_glass_widgets at 120 fps: 350 ms, the
 * glass swelling to 1.15). Appearing without a partner takes 250 ms, from
 * 1.15 down to its size. */
#define DISSOLVE_MS 350
#define MATERIALIZE_MS 250
#define MATERIALIZE_GROW 0.15
/* A morph-id swap: the partner's glass stays behind as a drop that is gone
 * by this much of the way (liquid_glass_widgets' morph engine: 40%), fused
 * to ours over this width (logical px) while they part. */
#define ANCHOR_SPAN 0.4
#define ANCHOR_MERGE 24.0

void
glass_panel_resolve_corners (GlassPanel            *self,
                             const graphene_rect_t *bounds,
                             double                 out[4])
{
  GlassPanelPrivate *priv = PRIV (self);
  double half = MIN (bounds->size.width, bounds->size.height) / 2.0;
  double r = priv->corner_radius < 0.0 ? half : MIN (priv->corner_radius, half);

  for (int i = 0; i < 4; i++)
    out[i] = priv->corner_radii[i] >= 0.0 ? MIN (priv->corner_radii[i], half) : r;
}

static GtkWidget *
fuse_group (GlassPanel *self)
{
  GlassPanelPrivate *priv = PRIV (self);
  GtkWidget *group = gtk_widget_get_ancestor (GTK_WIDGET (self), GLASS_TYPE_GROUP);

  return group && priv->view && gtk_widget_is_ancestor (group, GTK_WIDGET (priv->view)) ? group : NULL;
}

/* The group's panels in order (not those on them: another layer). */
static void
collect_panels (GtkWidget *widget,
                GPtrArray *out)
{
  for (GtkWidget *child = gtk_widget_get_first_child (widget); child; child = gtk_widget_get_next_sibling (child))
    {
      if (GLASS_IS_PANEL (child))
        g_ptr_array_add (out, child);
      else
        collect_panels (child, out);
    }
}

/* The nearest shown panel before @self in its group, else after it; not one
 * that is appearing itself (shown in the same go: they all come out of the
 * glass that was there). */
static GlassPanel *
group_neighbour (GlassPanel *self,
                 GtkWidget  *group)
{
  g_autoptr (GPtrArray) panels = g_ptr_array_new ();
  guint index;

  collect_panels (group, panels);
  if (!g_ptr_array_find (panels, self, &index))
    return NULL;
  for (guint k = 1; k < panels->len; k++)
    {
      for (int side = -1; side <= 1; side += 2)
        {
          gint64 j = (gint64) index + side * (gint64) k;
          GlassPanel *other;

          if (j < 0 || j >= panels->len)
            continue;
          other = g_ptr_array_index (panels, j);
          if (gtk_widget_get_mapped (GTK_WIDGET (other)) && PRIV (other)->has_drawn &&
              !PRIV (other)->ghost && !PRIV (other)->morphing)
            return other;
        }
    }
  return NULL;
}

/* A panel of the same view with our morph-id whose glass we can take: one
 * that has just hidden (or is fading as a ghost), else one still shown. */
static GlassPanel *
morph_partner (GlassPanel *self,
               gint64      now)
{
  GlassPanelPrivate *priv = PRIV (self);
  GlassPanel *shown = NULL;

  if (priv->morph_id == NULL || priv->view == NULL)
    return NULL;
  for (guint i = 0; ; i++)
    {
      GlassPanel *other = glass_view_get_panel (priv->view, i);
      GlassPanelPrivate *op;

      if (other == NULL)
        break;
      op = PRIV (other);
      if (other == self || !op->has_drawn || g_strcmp0 (op->morph_id, priv->morph_id) != 0)
        continue;
      if (!gtk_widget_get_mapped (GTK_WIDGET (other)))
        {
          if (op->ghost || now - op->hidden_at < MORPH_WINDOW_US)
            return other;
        }
      else if (shown == NULL)
        shown = other;
    }
  return shown;
}

/* The jelly is stepped where the view reads it, in its snapshot, with that
 * frame's time and that frame's place for the panel (§5.3-2); this only
 * asks the view for the frames, while the glass moves. On the view: a
 * ghost is not mapped, and libadwaita and GTK skip unmapped widgets. */
static gboolean
motion_tick (GtkWidget     *widget,
             GdkFrameClock *clock,
             gpointer       data)
{
  GlassPanel *self = data;
  GlassPanelPrivate *priv = PRIV (self);

  if (priv->view == NULL ||
      (!priv->morphing && !(priv->ghost && priv->ghost_into) && !priv->follow_moving))
    {
      /* The content at full strength, now that the glass is in place. */
      gtk_widget_queue_draw (GTK_WIDGET (self));
      priv->motion_tick = 0;
      return G_SOURCE_REMOVE;
    }
  gtk_widget_queue_draw (GTK_WIDGET (priv->view));
  if (priv->morphing)
    gtk_widget_queue_draw (GTK_WIDGET (self));   /* the content fades in */
  return G_SOURCE_CONTINUE;
}

static void
start_motion (GlassPanel *self)
{
  GlassPanelPrivate *priv = PRIV (self);

  if (priv->motion_tick == 0 && priv->view)
    priv->motion_tick = gtk_widget_add_tick_callback (GTK_WIDGET (priv->view), motion_tick,
                                                      g_object_ref (self), g_object_unref);
}

static void
stop_motion (GlassPanel *self)
{
  GlassPanelPrivate *priv = PRIV (self);

  if (priv->motion_tick && priv->view)
    gtk_widget_remove_tick_callback (GTK_WIDGET (priv->view), priv->motion_tick);
  priv->motion_tick = 0;
}

static gint64
frame_time (GlassPanel *self)
{
  GlassPanelPrivate *priv = PRIV (self);
  GdkFrameClock *clock = priv->view ? gtk_widget_get_frame_clock (GTK_WIDGET (priv->view)) : NULL;

  return clock ? gdk_frame_clock_get_frame_time (clock) : g_get_monotonic_time ();
}

static void
materialize_value (double value, gpointer data)
{
  GlassPanel *self = data;
  GlassPanelPrivate *priv = PRIV (self);

  priv->materialize_t = value;
  gtk_widget_queue_draw (GTK_WIDGET (self));
  if (priv->view)
    gtk_widget_queue_draw (GTK_WIDGET (priv->view));
}

static void
materialize_done (AdwAnimation *animation, GlassPanel *self)
{
  PRIV (self)->materializing = FALSE;
  gtk_widget_queue_draw (GTK_WIDGET (self));
}

static void
stop_ghost (GlassPanel *self)
{
  GlassPanelPrivate *priv = PRIV (self);

  priv->ghost = FALSE;
  if (priv->ghost_into && !priv->morphing)
    glass_jelly_stop (&priv->jelly);
  g_clear_weak_pointer (&priv->ghost_into);
  if (priv->ghost_anim)
    {
      AdwAnimation *anim = g_steal_pointer (&priv->ghost_anim);

      adw_animation_skip (anim);
      g_object_unref (anim);
    }
  if (priv->view)
    gtk_widget_queue_draw (GTK_WIDGET (priv->view));
}

static void
ghost_value (double value, gpointer data)
{
  GlassPanel *self = data;
  GlassPanelPrivate *priv = PRIV (self);

  priv->ghost_t = value;
  if (priv->view)
    gtk_widget_queue_draw (GTK_WIDGET (priv->view));
}

static void
ghost_done (AdwAnimation *animation, GlassPanel *self)
{
  GlassPanelPrivate *priv = PRIV (self);

  if (priv->ghost_anim != animation)
    return;
  priv->ghost = FALSE;
  g_clear_weak_pointer (&priv->ghost_into);
  if (priv->view)
    gtk_widget_queue_draw (GTK_WIDGET (priv->view));
  /* The animation holds a reference to us: let it go. */
  g_clear_object (&priv->ghost_anim);
}

/* Appearing with no glass to come from: the glass fades up as it settles
 * inward from slightly oversized, and the content sharpens last (SwiftUI's
 * .materialize; design.md §6.8). With less motion, a plain fade. */
static void
begin_materialize (GlassPanel *self)
{
  GlassPanelPrivate *priv = PRIV (self);

  if (priv->materialize_anim == NULL)
    {
      AdwAnimationTarget *target = adw_callback_animation_target_new (materialize_value, self, NULL);

      priv->materialize_anim = adw_timed_animation_new (GTK_WIDGET (self), 0.0, 1.0, MATERIALIZE_MS, target);
      adw_timed_animation_set_easing (ADW_TIMED_ANIMATION (priv->materialize_anim), ADW_LINEAR);
      g_signal_connect (priv->materialize_anim, "done", G_CALLBACK (materialize_done), self);
    }
  priv->materializing = TRUE;
  priv->materialize_t = 0.0;
  adw_animation_reset (priv->materialize_anim);
  adw_animation_play (priv->materialize_anim);
}

static void
begin_appear (GlassPanel *self)
{
  GlassPanelPrivate *priv = PRIV (self);
  gint64 now = g_get_monotonic_time ();
  GlassPanel *source = NULL;
  GtkWidget *group = NULL;
  gboolean reduced;

  if (priv->view == NULL || priv->mode != GLASS_PANEL_MODE_VIEW)
    return;
  if (!glass_animations_enabled (GTK_WIDGET (self)))
    {
      if (priv->ghost)
        stop_ghost (self);
      return;
    }
  reduced = glass_motion_reduced (GTK_WIDGET (self));

  if (priv->ghost)
    {
      /* Shown again while fading: grow back out of where the ghost is. */
      priv->morph_from = priv->drawn_rect;
      priv->morph_from_radius = priv->drawn_radius;
      stop_ghost (self);
      source = self;
    }
  else
    {
      source = morph_partner (self, now);
      if (source)
        {
          PRIV (source)->taken_at = now;
          if (PRIV (source)->ghost)
            stop_ghost (source);
        }
      else if ((group = fuse_group (self)))
        source = group_neighbour (self, group);
      if (source == NULL)
        {
          /* Nothing to come out of. Not when the view itself just came on
           * screen (a page shown): the panel is part of what appeared. */
          if (glass_view_has_drawn_since_map (priv->view))
            begin_materialize (self);
          return;
        }
      /* Less motion: no travel; the partner's glass goes, ours fades in. */
      if (reduced)
        {
          begin_materialize (self);
          return;
        }
      priv->morph_from = PRIV (source)->drawn_rect;
      priv->morph_from_radius = PRIV (source)->drawn_radius;
      /* A source still shown may move in this very frame (a centred row
       * grows): its place is read in the first snapshot (§5.3-2). */
      if (gtk_widget_get_mapped (GTK_WIDGET (source)))
        g_set_weak_pointer (&priv->morph_source, GTK_WIDGET (source));
    }
  if (reduced)
    return;

  /* A morph-id swap outside a group: the partner's glass stays behind as a
   * drop, joined to ours until it is gone (in a group the neighbour is
   * there already, joined by the group). */
  priv->has_anchor = source != self && group == NULL && priv->morph_source == NULL;
  glass_jelly_start (&priv->jelly, &glass_jelly_travel, &priv->morph_from, TRUE, TRUE);
  priv->morphing = TRUE;
  priv->morph_t = 0.0;
  start_motion (self);
}

static void
begin_vanish (GlassPanel *self)
{
  GlassPanelPrivate *priv = PRIV (self);
  gint64 now = g_get_monotonic_time ();
  AdwAnimationTarget *target;
  GtkWidget *group;
  GlassPanel *into;

  priv->hidden_at = now;
  g_clear_weak_pointer (&priv->morph_source);
  priv->morphing = FALSE;
  priv->has_anchor = FALSE;
  glass_jelly_stop (&priv->jelly);
  if (priv->materializing)
    {
      priv->materializing = FALSE;
      adw_animation_skip (priv->materialize_anim);
    }
  if (!priv->has_drawn || priv->view == NULL || priv->mode != GLASS_PANEL_MODE_VIEW ||
      now - priv->taken_at < MORPH_WINDOW_US ||
      !gtk_widget_get_mapped (GTK_WIDGET (priv->view)) ||
      !glass_animations_enabled (GTK_WIDGET (priv->view)))
    return;

  stop_ghost (self);
  priv->ghost = TRUE;
  priv->ghost_t = 0.0;
  priv->ghost_from = priv->drawn_rect;
  priv->ghost_from_radius = priv->drawn_radius;

  /* In a group, into the neighbour, on the jelly: a drop drawn back into
   * the glass it came out of. */
  into = (group = fuse_group (self)) ? group_neighbour (self, group) : NULL;
  if (into && !glass_motion_reduced (GTK_WIDGET (priv->view)))
    {
      g_set_weak_pointer (&priv->ghost_into, GTK_WIDGET (into));
      glass_jelly_start (&priv->jelly, &glass_jelly_travel, &priv->ghost_from, TRUE, TRUE);
      start_motion (self);
      return;
    }

  /* No one to go to: it dissolves where it is (SwiftUI's dematerialize).
   * On the view: libadwaita skips the animations of unmapped widgets. */
  target = adw_callback_animation_target_new (ghost_value, g_object_ref (self), g_object_unref);
  priv->ghost_anim = adw_timed_animation_new (GTK_WIDGET (priv->view), 0.0, 1.0, DISSOLVE_MS, target);
  adw_timed_animation_set_easing (ADW_TIMED_ANIMATION (priv->ghost_anim), ADW_LINEAR);
  g_signal_connect (priv->ghost_anim, "done", G_CALLBACK (ghost_done), self);
  adw_animation_play (priv->ghost_anim);
}

static void
lerp_corners (double from, const double to[4], double t, double out[4])
{
  for (int i = 0; i < 4; i++)
    out[i] = MAX (from + (to[i] - from) * t, 0.0);
}

gboolean
glass_panel_get_morph (GlassPanel            *self,
                       const graphene_rect_t *target,
                       graphene_rect_t       *rect,
                       double                 corners[4])
{
  GlassPanelPrivate *priv = PRIV (self);
  double to[4];

  if (!priv->morphing)
    return FALSE;
  if (priv->morph_source)
    {
      graphene_rect_t from;

      if (priv->view && gtk_widget_get_mapped (priv->morph_source) &&
          gtk_widget_compute_bounds (priv->morph_source, GTK_WIDGET (priv->view), &from))
        {
          priv->morph_from = from;
          priv->morph_from_radius = glass_panel_effective_radius (GLASS_PANEL (priv->morph_source), &from);
          glass_jelly_start (&priv->jelly, &glass_jelly_travel, &from, TRUE, TRUE);
        }
      g_clear_weak_pointer (&priv->morph_source);
    }

  /* On the springs towards where the panel is this frame. */
  glass_jelly_set_mark (&priv->jelly, target);
  if (!glass_jelly_step (&priv->jelly, frame_time (self)))
    {
      priv->morphing = FALSE;
      priv->has_anchor = FALSE;
      glass_jelly_stop (&priv->jelly);
      return FALSE;
    }
  glass_jelly_get (&priv->jelly, rect);
  priv->morph_t = MAX (priv->morph_t, glass_jelly_progress (&priv->jelly, &priv->morph_from));
  glass_panel_resolve_corners (self, target, to);
  lerp_corners (priv->morph_from_radius, to, priv->morph_t, corners);
  /* No corner rounder than the glass is wide while it stretches. */
  for (int i = 0; i < 4; i++)
    corners[i] = MIN (corners[i], MIN (rect->size.width, rect->size.height) / 2.0);
  return TRUE;
}

gboolean
glass_panel_get_morph_anchor (GlassPanel      *self,
                              graphene_rect_t *rect,
                              double          *radius)
{
  GlassPanelPrivate *priv = PRIV (self);
  double left;
  float cx, cy;

  if (!priv->morphing || !priv->has_anchor)
    return FALSE;
  /* Gone by ANCHOR_SPAN of the way, shrinking about its centre. */
  left = 1.0 - priv->morph_t / ANCHOR_SPAN;
  if (left <= 0.02)
    {
      priv->has_anchor = FALSE;
      return FALSE;
    }
  cx = priv->morph_from.origin.x + priv->morph_from.size.width / 2.0f;
  cy = priv->morph_from.origin.y + priv->morph_from.size.height / 2.0f;
  *rect = GRAPHENE_RECT_INIT (cx - priv->morph_from.size.width * (float) left / 2.0f,
                              cy - priv->morph_from.size.height * (float) left / 2.0f,
                              MAX (priv->morph_from.size.width * (float) left, 1.0f),
                              MAX (priv->morph_from.size.height * (float) left, 1.0f));
  *radius = MIN (priv->morph_from_radius * left, MIN (rect->size.width, rect->size.height) / 2.0);
  return TRUE;
}

double
glass_panel_get_anchor_merge (GlassPanel *self)
{
  return ANCHOR_MERGE;
}

void
glass_panel_set_follows (GlassPanel *self,
                         gboolean    follows)
{
  PRIV (self)->follows = follows;
}

void
glass_panel_follow (GlassPanel      *self,
                    graphene_rect_t *rect)
{
  GlassPanelPrivate *priv = PRIV (self);

  if (!priv->follows || priv->morphing)
    return;
  if (glass_motion_reduced (GTK_WIDGET (self)))
    {
      glass_jelly_stop (&priv->jelly);
      priv->follow_moving = FALSE;
      return;
    }
  if (!priv->jelly.active)
    {
      /* Along the knob's travel only; its height does not stretch. */
      glass_jelly_start (&priv->jelly, &glass_jelly_plate, rect, TRUE, FALSE);
      return;
    }
  glass_jelly_set_mark (&priv->jelly, rect);
  priv->follow_moving = glass_jelly_step (&priv->jelly, frame_time (self));
  glass_jelly_get (&priv->jelly, rect);
  if (priv->follow_moving)
    start_motion (self);
}

double
glass_panel_get_visibility (GlassPanel *self,
                            double     *grow)
{
  GlassPanelPrivate *priv = PRIV (self);
  double t;

  *grow = 1.0;
  if (!priv->materializing)
    return 1.0;
  /* The glass resolves at a steady rate, just short of the end, so it is
   * there before its content (liquid_glass_widgets' capture: 0 - 0.92). */
  t = CLAMP (priv->materialize_t / 0.92, 0.0, 1.0);
  if (!glass_motion_reduced (GTK_WIDGET (self)))
    *grow = 1.0 + MATERIALIZE_GROW * (1.0 - t);
  return t;
}

gboolean
glass_panel_get_ghost (GlassPanel      *self,
                       GlassView       *view,
                       graphene_rect_t *rect,
                       double           corners[4])
{
  GlassPanelPrivate *priv = PRIV (self);
  graphene_rect_t into;
  double to[4];

  if (!priv->ghost)
    return FALSE;

  /* Dissolving where it was, swelling a little as it goes. */
  if (priv->ghost_anim && priv->ghost_into == NULL)
    {
      double grow = glass_motion_reduced (GTK_WIDGET (view)) ? 1.0 : 1.0 + MATERIALIZE_GROW * priv->ghost_t;
      double r = priv->ghost_from_radius * grow;

      *rect = priv->ghost_from;
      graphene_rect_inset (rect, -rect->size.width * (float) (grow - 1.0) / 2.0f,
                           -rect->size.height * (float) (grow - 1.0) / 2.0f);
      for (int i = 0; i < 4; i++)
        corners[i] = MIN (r, MIN (rect->size.width, rect->size.height) / 2.0);
      return TRUE;
    }

  if (priv->ghost_into == NULL || !gtk_widget_get_mapped (priv->ghost_into) ||
      !gtk_widget_compute_bounds (priv->ghost_into, GTK_WIDGET (view), &into))
    {
      stop_ghost (self);
      return FALSE;
    }
  /* Into the neighbour on the springs; gone once it has arrived (the two
   * are one shape then). */
  glass_jelly_set_mark (&priv->jelly, &into);
  if (!glass_jelly_step (&priv->jelly, frame_time (self)))
    {
      stop_ghost (self);
      return FALSE;
    }
  glass_jelly_get (&priv->jelly, rect);
  priv->ghost_t = MAX (priv->ghost_t, glass_jelly_progress (&priv->jelly, &priv->ghost_from));
  glass_panel_resolve_corners (GLASS_PANEL (priv->ghost_into), &into, to);
  lerp_corners (priv->ghost_from_radius, to, priv->ghost_t, corners);
  for (int i = 0; i < 4; i++)
    corners[i] = MIN (corners[i], MIN (rect->size.width, rect->size.height) / 2.0);
  return TRUE;
}

double
glass_panel_get_ghost_visibility (GlassPanel *self)
{
  GlassPanelPrivate *priv = PRIV (self);

  if (priv->ghost && priv->ghost_anim && priv->ghost_into == NULL)
    return CLAMP (1.0 - priv->ghost_t, 0.0, 1.0);
  return 1.0;
}

void
glass_panel_set_drawn (GlassPanel            *self,
                       const graphene_rect_t *rect,
                       double                 radius)
{
  GlassPanelPrivate *priv = PRIV (self);

  priv->has_drawn = TRUE;
  priv->drawn_rect = *rect;
  priv->drawn_radius = radius;
}

/* The content fades in while the glass morphs into place; materializing,
 * it starts once the glass has begun to form and sharpens up to the end
 * (liquid_glass_widgets' capture: 0.35 - 1). */
static float
content_opacity (GlassPanel *self)
{
  GlassPanelPrivate *priv = PRIV (self);

  if (priv->materializing)
    return glass_motion_reduced (GTK_WIDGET (self)) ? (float) priv->materialize_t
                                                     : (float) CLAMP ((priv->materialize_t - 0.35) / 0.65, 0.0, 1.0);
  if (!priv->morphing)
    return 1.0f;
  return (float) CLAMP ((priv->morph_t - 0.25) / 0.55, 0.0, 1.0);
}

/* ... and out of focus until then (a Gaussian of up to 8 px). */
static float
content_blur (GlassPanel *self)
{
  GlassPanelPrivate *priv = PRIV (self);

  if (!priv->materializing || glass_motion_reduced (GTK_WIDGET (self)))
    return 0.0f;
  return 8.0f * (1.0f - content_opacity (self));
}

float
glass_panel_get_content_opacity (GlassPanel *self)
{
  return content_opacity (self);
}

static void
glass_panel_map (GtkWidget *widget)
{
  GTK_WIDGET_CLASS (glass_panel_parent_class)->map (widget);
  begin_appear (GLASS_PANEL (widget));
}

static void
glass_panel_unmap (GtkWidget *widget)
{
  begin_vanish (GLASS_PANEL (widget));
  GTK_WIDGET_CLASS (glass_panel_parent_class)->unmap (widget);
}

static void
glass_panel_snapshot (GtkWidget   *widget,
                      GtkSnapshot *snapshot)
{
  double vs = glass_panel_get_visual_scale (GLASS_PANEL (widget));
  float opacity = content_opacity (GLASS_PANEL (widget));
  float blur = content_blur (GLASS_PANEL (widget));
  float cx = gtk_widget_get_width (widget) / 2.0f;
  float cy = gtk_widget_get_height (widget) / 2.0f;

  if (opacity < 1.0f)
    gtk_snapshot_push_opacity (snapshot, opacity);
  if (blur > 0.05f)
    gtk_snapshot_push_blur (snapshot, blur);
  if (vs != 1.0)
    {
      gtk_snapshot_save (snapshot);
      gtk_snapshot_translate (snapshot, &GRAPHENE_POINT_INIT (cx, cy));
      gtk_snapshot_scale (snapshot, (float) vs, (float) vs);
      gtk_snapshot_translate (snapshot, &GRAPHENE_POINT_INIT (-cx, -cy));
    }
  for (GtkWidget *child = gtk_widget_get_first_child (widget); child; child = gtk_widget_get_next_sibling (child))
    gtk_widget_snapshot_child (widget, child, snapshot);
  if (vs != 1.0)
    gtk_snapshot_restore (snapshot);
  if (blur > 0.05f)
    gtk_snapshot_pop (snapshot);
  if (opacity < 1.0f)
    gtk_snapshot_pop (snapshot);
}

/**
 * glass_panel_get_interactive:
 * @self: a panel
 *
 * Gets whether the glass reacts to presses.
 *
 * Returns: whether the glass reacts to presses
 */
gboolean
glass_panel_get_interactive (GlassPanel *self)
{
  g_return_val_if_fail (GLASS_IS_PANEL (self), FALSE);

  return PRIV (self)->interactive;
}

/**
 * glass_panel_set_interactive:
 * @self: a panel
 * @interactive: whether the glass reacts to presses
 *
 * Makes the glass swell and light up while it is pressed. The press is only
 * watched: the widgets on the panel still get it.
 */
void
glass_panel_set_interactive (GlassPanel *self,
                             gboolean    interactive)
{
  GlassPanelPrivate *priv;

  g_return_if_fail (GLASS_IS_PANEL (self));

  priv = PRIV (self);
  interactive = !!interactive;
  if (priv->interactive == interactive)
    return;
  priv->interactive = interactive;

  if (interactive)
    {
      priv->press_controller = gtk_event_controller_legacy_new ();
      gtk_event_controller_set_propagation_phase (priv->press_controller, GTK_PHASE_CAPTURE);
      g_signal_connect (priv->press_controller, "event", G_CALLBACK (press_event), self);
      gtk_widget_add_controller (GTK_WIDGET (self), priv->press_controller);
    }
  else
    {
      gtk_widget_remove_controller (GTK_WIDGET (self), priv->press_controller);
      priv->press_controller = NULL;
      glass_panel_set_pressed (self, FALSE);
    }
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_INTERACTIVE]);
}

/**
 * glass_panel_get_morph_id:
 * @self: a panel
 *
 * Gets the morph ID.
 *
 * Returns: (nullable): the morph ID
 */
const char *
glass_panel_get_morph_id (GlassPanel *self)
{
  g_return_val_if_fail (GLASS_IS_PANEL (self), NULL);

  return PRIV (self)->morph_id;
}

/**
 * glass_panel_set_morph_id:
 * @self: a panel
 * @morph_id: (nullable): the ID it shares with the panels it morphs into
 *   and out of
 *
 * Sets [property@Panel:morph-id].
 */
void
glass_panel_set_morph_id (GlassPanel *self,
                          const char *morph_id)
{
  g_return_if_fail (GLASS_IS_PANEL (self));

  if (g_set_str (&PRIV (self)->morph_id, morph_id))
    g_object_notify_by_pspec (G_OBJECT (self), props[PROP_MORPH_ID]);
}

double
glass_panel_get_highlight (GlassPanel *self)
{
  return PRIV (self)->highlight;
}

void
glass_panel_set_highlight (GlassPanel *self,
                           double      highlight)
{
  GlassPanelPrivate *priv = PRIV (self);

  highlight = CLAMP (highlight, 0.0, 1.0);
  if (priv->highlight == highlight)
    return;
  priv->highlight = highlight;
  gtk_widget_queue_draw (GTK_WIDGET (self));
}

/* PROMINENT: the theme's accent, which the context's tint colour does not
 * replace (it marks the button that confirms, not the look of the app). */
static gboolean
accent_tint (GlassPanel *self,
             double      strength,
             float       out[4])
{
  GlassPanelPrivate *priv = PRIV (self);
  GdkRGBA accent;

  if (!glass_material_specs[priv->material].tint_from_accent || priv->accent_node == NULL)
    return FALSE;
  gtk_widget_get_color (priv->accent_node, &accent);
  out[0] = accent.red;
  out[1] = accent.green;
  out[2] = accent.blue;
  out[3] = (float) strength;
  return TRUE;
}

void
glass_panel_get_tint_rgba (GlassPanel    *self,
                           const double   params[GLASS_N_PARAMS],
                           const GdkRGBA *theme_bg,
                           float          out[4])
{
  GlassPanelPrivate *priv = PRIV (self);

  /* The panel's own tint, else the context's colour or the material's,
   * with the tint-strength in effect (design.md §11.2). */
  if (priv->tint_set)
    {
      out[0] = priv->tint.red;
      out[1] = priv->tint.green;
      out[2] = priv->tint.blue;
      out[3] = priv->tint.alpha;
    }
  else if (!accent_tint (self, params[GLASS_PARAM_ID_TINT_STRENGTH], out))
    glass_context_resolve_tint (glass_context_get_default (), priv->material,
                                params[GLASS_PARAM_ID_TINT_STRENGTH], theme_bg, out);

  /* Pressed: a little more of a lighter tint. */
  if (MAX (priv->highlight, priv->press) > 0.0)
    {
      double h = MIN (1.0, MAX (priv->highlight, priv->press));

      for (int i = 0; i < 3; i++)
        out[i] = out[i] + (1.0f - out[i]) * (float) (0.5 * h);
      out[3] = MIN (1.0f, out[3] + (float) (0.14 * h));
    }
}

/* ── Layout ───────────────────────────────────────────────────────────────── */

static void
glass_panel_measure (GtkWidget      *widget,
                     GtkOrientation  orientation,
                     int             for_size,
                     int            *minimum,
                     int            *natural,
                     int            *minimum_baseline,
                     int            *natural_baseline)
{
  GtkWidget *child = PRIV (widget)->child;

  if (child && gtk_widget_should_layout (child))
    gtk_widget_measure (child, orientation, for_size, minimum, natural,
                        minimum_baseline, natural_baseline);
  else
    *minimum = *natural = 0;
}

static void
glass_panel_size_allocate (GtkWidget *widget,
                           int        width,
                           int        height,
                           int        baseline)
{
  GtkWidget *child = PRIV (widget)->child;

  if (child && gtk_widget_should_layout (child))
    gtk_widget_allocate (child, width, height, baseline, NULL);
  if (PRIV (widget)->accent_node)
    gtk_widget_allocate (PRIV (widget)->accent_node, 0, 0, -1, NULL);
}

static GtkSizeRequestMode
glass_panel_get_request_mode (GtkWidget *widget)
{
  GtkWidget *child = PRIV (widget)->child;

  return child ? gtk_widget_get_request_mode (child) : GTK_SIZE_REQUEST_CONSTANT_SIZE;
}

static void
glass_panel_compute_expand (GtkWidget *widget,
                            gboolean  *hexpand,
                            gboolean  *vexpand)
{
  GtkWidget *child = PRIV (widget)->child;

  *hexpand = child && gtk_widget_compute_expand (child, GTK_ORIENTATION_HORIZONTAL);
  *vexpand = child && gtk_widget_compute_expand (child, GTK_ORIENTATION_VERTICAL);
}

/* ── GObject ──────────────────────────────────────────────────────────────── */

static void
glass_panel_dispose (GObject *object)
{
  GlassPanelPrivate *priv = PRIV (object);

  g_clear_handle_id (&priv->settle_source, g_source_remove);
  g_clear_object (&priv->press_anim);
  g_clear_object (&priv->materialize_anim);
  stop_motion (GLASS_PANEL (object));
  g_clear_weak_pointer (&priv->morph_source);
  g_clear_weak_pointer (&priv->ghost_into);
  g_clear_pointer (&priv->child, gtk_widget_unparent);
  g_clear_pointer (&priv->accent_node, gtk_widget_unparent);
  g_clear_pointer (&priv->nested_views, g_ptr_array_unref);
  g_clear_pointer (&priv->own_classes, g_ptr_array_unref);

  G_OBJECT_CLASS (glass_panel_parent_class)->dispose (object);
}

static void
glass_panel_finalize (GObject *object)
{
  g_free (PRIV (object)->morph_id);

  G_OBJECT_CLASS (glass_panel_parent_class)->finalize (object);
}

static void
glass_panel_get_property (GObject    *object,
                          guint       prop_id,
                          GValue     *value,
                          GParamSpec *pspec)
{
  GlassPanel *self = GLASS_PANEL (object);
  GlassPanelPrivate *priv = PRIV (self);

  switch (prop_id)
    {
    case PROP_CHILD:
      g_value_set_object (value, priv->child);
      break;
    case PROP_MATERIAL:
      g_value_set_enum (value, priv->material);
      break;
    case PROP_CORNER_RADIUS:
      g_value_set_double (value, priv->corner_radius);
      break;
    case PROP_TOP_LEFT_RADIUS:
    case PROP_TOP_RIGHT_RADIUS:
    case PROP_BOTTOM_RIGHT_RADIUS:
    case PROP_BOTTOM_LEFT_RADIUS:
      g_value_set_double (value, priv->corner_radii[prop_id - PROP_TOP_LEFT_RADIUS]);
      break;
    case PROP_TINT:
      g_value_set_boxed (value, priv->tint_set ? &priv->tint : NULL);
      break;
    case PROP_HAS_SHADOW:
      g_value_set_boolean (value, priv->has_shadow);
      break;
    case PROP_ADAPTIVE:
      g_value_set_enum (value, priv->adaptive);
      break;
    case PROP_APPEARANCE:
      g_value_set_enum (value, priv->appearance);
      break;
    case PROP_INTERACTIVE:
      g_value_set_boolean (value, priv->interactive);
      break;
    case PROP_MORPH_ID:
      g_value_set_string (value, priv->morph_id);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
glass_panel_set_property (GObject      *object,
                          guint         prop_id,
                          const GValue *value,
                          GParamSpec   *pspec)
{
  GlassPanel *self = GLASS_PANEL (object);

  switch (prop_id)
    {
    case PROP_CHILD:
      glass_panel_set_child (self, g_value_get_object (value));
      break;
    case PROP_MATERIAL:
      glass_panel_set_material (self, g_value_get_enum (value));
      break;
    case PROP_CORNER_RADIUS:
      glass_panel_set_corner_radius (self, g_value_get_double (value));
      break;
    case PROP_TOP_LEFT_RADIUS:
    case PROP_TOP_RIGHT_RADIUS:
    case PROP_BOTTOM_RIGHT_RADIUS:
    case PROP_BOTTOM_LEFT_RADIUS:
      set_corner (self, prop_id - PROP_TOP_LEFT_RADIUS, g_value_get_double (value));
      break;
    case PROP_TINT:
      glass_panel_set_tint (self, g_value_get_boxed (value));
      break;
    case PROP_HAS_SHADOW:
      glass_panel_set_has_shadow (self, g_value_get_boolean (value));
      break;
    case PROP_ADAPTIVE:
      glass_panel_set_adaptive (self, g_value_get_enum (value));
      break;
    case PROP_INTERACTIVE:
      glass_panel_set_interactive (self, g_value_get_boolean (value));
      break;
    case PROP_MORPH_ID:
      glass_panel_set_morph_id (self, g_value_get_string (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
glass_panel_class_init (GlassPanelClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = glass_panel_dispose;
  object_class->finalize = glass_panel_finalize;
  object_class->get_property = glass_panel_get_property;
  object_class->set_property = glass_panel_set_property;

  widget_class->measure = glass_panel_measure;
  widget_class->size_allocate = glass_panel_size_allocate;
  widget_class->get_request_mode = glass_panel_get_request_mode;
  widget_class->compute_expand = glass_panel_compute_expand;
  widget_class->snapshot = glass_panel_snapshot;
  widget_class->root = glass_panel_root;
  widget_class->unroot = glass_panel_unroot;
  widget_class->map = glass_panel_map;
  widget_class->unmap = glass_panel_unmap;

  /**
   * GlassPanel:child:
   *
   * What is on the glass.
   */
  props[PROP_CHILD] =
    g_param_spec_object ("child", NULL, NULL, GTK_TYPE_WIDGET,
                         G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

  /**
   * GlassPanel:material:
   *
   * The kind of glass.
   */
  props[PROP_MATERIAL] =
    g_param_spec_enum ("material", NULL, NULL, GLASS_TYPE_MATERIAL, GLASS_MATERIAL_REGULAR,
                       G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

  /**
   * GlassPanel:corner-radius:
   *
   * The corner radius in px; a negative value (the default) makes a
   * capsule: half the shorter side.
   */
  props[PROP_CORNER_RADIUS] =
    g_param_spec_double ("corner-radius", NULL, NULL, -1.0, G_MAXDOUBLE, -1.0,
                         G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

  /**
   * GlassPanel:top-left-radius:
   *
   * The radius of the top left corner in px; a negative value (the
   * default) is [property@Panel:corner-radius]'s.
   */
  props[PROP_TOP_LEFT_RADIUS] =
    g_param_spec_double ("top-left-radius", NULL, NULL, -1.0, G_MAXDOUBLE, -1.0,
                         G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

  /**
   * GlassPanel:top-right-radius:
   *
   * The radius of the top right corner in px; a negative value (the
   * default) is [property@Panel:corner-radius]'s.
   */
  props[PROP_TOP_RIGHT_RADIUS] =
    g_param_spec_double ("top-right-radius", NULL, NULL, -1.0, G_MAXDOUBLE, -1.0,
                         G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

  /**
   * GlassPanel:bottom-right-radius:
   *
   * The radius of the bottom right corner in px; a negative value (the
   * default) is [property@Panel:corner-radius]'s.
   */
  props[PROP_BOTTOM_RIGHT_RADIUS] =
    g_param_spec_double ("bottom-right-radius", NULL, NULL, -1.0, G_MAXDOUBLE, -1.0,
                         G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

  /**
   * GlassPanel:bottom-left-radius:
   *
   * The radius of the bottom left corner in px; a negative value (the
   * default) is [property@Panel:corner-radius]'s.
   */
  props[PROP_BOTTOM_LEFT_RADIUS] =
    g_param_spec_double ("bottom-left-radius", NULL, NULL, -1.0, G_MAXDOUBLE, -1.0,
                         G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

  /**
   * GlassPanel:tint:
   *
   * The colour mixed into the glass; its alpha is how much. %NULL (the
   * default) is the material's.
   */
  props[PROP_TINT] =
    g_param_spec_boxed ("tint", NULL, NULL, GDK_TYPE_RGBA,
                        G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

  /**
   * GlassPanel:has-shadow:
   *
   * Whether the glass casts a shadow.
   */
  props[PROP_HAS_SHADOW] =
    g_param_spec_boolean ("has-shadow", NULL, NULL, TRUE,
                          G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

  /**
   * GlassPanel:adaptive:
   *
   * How the colour of what is on the glass follows what is under it.
   * The %GLASS_MATERIAL_THICK and %GLASS_MATERIAL_MENU materials never
   * adapt: they keep the theme's colours; %GLASS_MATERIAL_PROMINENT keeps
   * the accent's foreground colour.
   */
  props[PROP_ADAPTIVE] =
    g_param_spec_enum ("adaptive", NULL, NULL, GLASS_TYPE_ADAPTIVE_MODE, GLASS_ADAPTIVE_MODE_AUTO,
                       G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

  /**
   * GlassPanel:appearance:
   *
   * How light the glass looks, from what is under it.
   */
  props[PROP_APPEARANCE] =
    g_param_spec_enum ("appearance", NULL, NULL, GLASS_TYPE_APPEARANCE, GLASS_APPEARANCE_UNKNOWN,
                       G_PARAM_READABLE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

  /**
   * GlassPanel:interactive:
   *
   * Whether the glass swells and lights up while it is pressed.
   */
  props[PROP_INTERACTIVE] =
    g_param_spec_boolean ("interactive", NULL, NULL, FALSE,
                          G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

  /**
   * GlassPanel:morph-id:
   *
   * Panels of a view with the same morph ID are one piece of glass that
   * changes shape: when one is shown just as another is hidden (a search
   * button that becomes a search field), its glass morphs from the other's
   * into its own, and its content fades in.
   *
   * In a [class@Group], panels morph without an ID too: one that is shown
   * comes out of its neighbour like a drop, and one that is hidden is
   * drawn into its neighbour.
   */
  props[PROP_MORPH_ID] =
    g_param_spec_string ("morph-id", NULL, NULL, NULL,
                         G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, N_PROPS, props);

  gtk_widget_class_set_css_name (widget_class, "glasspanel");
  gtk_widget_class_set_accessible_role (widget_class, GTK_ACCESSIBLE_ROLE_GROUP);
}

static void
glass_panel_init (GlassPanel *self)
{
  GlassPanelPrivate *priv = PRIV (self);

  priv->material = GLASS_MATERIAL_REGULAR;
  priv->corner_radius = -1.0;
  for (int i = 0; i < 4; i++)
    priv->corner_radii[i] = -1.0;
  priv->has_shadow = TRUE;
  priv->adaptive = GLASS_ADAPTIVE_MODE_AUTO;
  priv->appearance = GLASS_APPEARANCE_UNKNOWN;
  priv->press_grow_px = 6.0;
  priv->press_max_extra = 0.16;
  for (int i = 0; i < GLASS_N_PARAMS; i++)
    priv->param_value[i] = priv->param_default[i] = NAN;
  glass_adaptive_state_init (&priv->astate);

  gtk_widget_set_overflow (GTK_WIDGET (self), GTK_OVERFLOW_HIDDEN);
  g_signal_connect (self, "notify::css-classes", G_CALLBACK (css_classes_changed), NULL);
}

static void
glass_panel_buildable_add_child (GtkBuildable *buildable,
                                 GtkBuilder   *builder,
                                 GObject      *child,
                                 const char   *type)
{
  if (GTK_IS_WIDGET (child) && type == NULL)
    glass_panel_set_child (GLASS_PANEL (buildable), GTK_WIDGET (child));
  else
    parent_buildable_iface->add_child (buildable, builder, child, type);
}

static void
glass_panel_buildable_init (GtkBuildableIface *iface)
{
  parent_buildable_iface = g_type_interface_peek_parent (iface);
  iface->add_child = glass_panel_buildable_add_child;
}

/* ── Public API ───────────────────────────────────────────────────────────── */

/**
 * glass_panel_new:
 *
 * Creates a new panel.
 *
 * Returns: a new panel
 */
GtkWidget *
glass_panel_new (void)
{
  return g_object_new (GLASS_TYPE_PANEL, NULL);
}

/**
 * glass_panel_get_child:
 * @self: a panel
 *
 * Gets the child.
 *
 * Returns: (transfer none) (nullable): the child
 */
GtkWidget *
glass_panel_get_child (GlassPanel *self)
{
  g_return_val_if_fail (GLASS_IS_PANEL (self), NULL);

  return PRIV (self)->child;
}

/**
 * glass_panel_set_child:
 * @self: a panel
 * @child: (nullable): what is on the glass
 *
 * Sets the child.
 */
void
glass_panel_set_child (GlassPanel *self,
                       GtkWidget  *child)
{
  GlassPanelPrivate *priv;

  g_return_if_fail (GLASS_IS_PANEL (self));
  g_return_if_fail (child == NULL || GTK_IS_WIDGET (child));

  priv = PRIV (self);
  if (priv->child == child)
    return;

  g_clear_pointer (&priv->child, gtk_widget_unparent);
  if (child)
    {
      priv->child = child;
      gtk_widget_set_parent (child, GTK_WIDGET (self));
    }
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_CHILD]);
}

/**
 * glass_panel_get_material:
 * @self: a panel
 *
 * Gets the material.
 *
 * Returns: the material
 */
GlassMaterial
glass_panel_get_material (GlassPanel *self)
{
  g_return_val_if_fail (GLASS_IS_PANEL (self), GLASS_MATERIAL_REGULAR);

  return PRIV (self)->material;
}

/**
 * glass_panel_set_material:
 * @self: a panel
 * @material: the material
 *
 * Sets the kind of glass.
 */
void
glass_panel_set_material (GlassPanel    *self,
                          GlassMaterial  material)
{
  GlassPanelPrivate *priv;

  g_return_if_fail (GLASS_IS_PANEL (self));
  g_return_if_fail ((int) material >= 0 && (int) material < GLASS_N_MATERIALS);

  priv = PRIV (self);
  if (priv->material == material)
    return;
  priv->material = material;
  if (!glass_panel_uses_adaptive (self))
    set_appearance (self, GLASS_APPEARANCE_UNKNOWN);

  /* PROMINENT: the accent (a never-drawn node, glass.css) and the accent's
   * foreground (.glass-prominent). */
  if (glass_material_specs[material].tint_from_accent && priv->accent_node == NULL)
    {
      priv->accent_node = glass_style_node_new ("accent");
      gtk_widget_set_parent (priv->accent_node, GTK_WIDGET (self));
    }
  priv->changing_classes++;
  gtk_widget_remove_css_class (GTK_WIDGET (self), "glass-prominent");
  priv->changing_classes--;
  restore_classes (self);

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_MATERIAL]);
  gtk_widget_queue_draw (GTK_WIDGET (self));
}

/**
 * glass_panel_get_corner_radius:
 * @self: a panel
 *
 * Gets the corner radius; negative for a capsule.
 *
 * Returns: the corner radius; negative for a capsule
 */
double
glass_panel_get_corner_radius (GlassPanel *self)
{
  g_return_val_if_fail (GLASS_IS_PANEL (self), -1.0);

  return PRIV (self)->corner_radius;
}

/**
 * glass_panel_set_corner_radius:
 * @self: a panel
 * @radius: the radius in px, or a negative value for a capsule
 *
 * Sets the corner radius.
 */
void
glass_panel_set_corner_radius (GlassPanel *self,
                               double      radius)
{
  GlassPanelPrivate *priv;

  g_return_if_fail (GLASS_IS_PANEL (self));

  priv = PRIV (self);
  radius = radius < 0.0 ? -1.0 : radius;
  if (priv->corner_radius == radius)
    return;
  priv->corner_radius = radius;
  update_shape_class (self);
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_CORNER_RADIUS]);
  gtk_widget_queue_draw (GTK_WIDGET (self));
}

/**
 * glass_panel_get_corner_radii:
 * @self: a panel
 * @top_left: (out) (optional): the top left corner's radius
 * @top_right: (out) (optional): the top right corner's radius
 * @bottom_right: (out) (optional): the bottom right corner's radius
 * @bottom_left: (out) (optional): the bottom left corner's radius
 *
 * Gets the radius of each corner, as set: negative for a corner that has
 * [property@Panel:corner-radius]'s.
 */
void
glass_panel_get_corner_radii (GlassPanel *self,
                              double     *top_left,
                              double     *top_right,
                              double     *bottom_right,
                              double     *bottom_left)
{
  GlassPanelPrivate *priv;

  g_return_if_fail (GLASS_IS_PANEL (self));

  priv = PRIV (self);
  if (top_left)
    *top_left = priv->corner_radii[0];
  if (top_right)
    *top_right = priv->corner_radii[1];
  if (bottom_right)
    *bottom_right = priv->corner_radii[2];
  if (bottom_left)
    *bottom_left = priv->corner_radii[3];
}

/**
 * glass_panel_set_corner_radii:
 * @self: a panel
 * @top_left: the top left corner's radius in px, or a negative value
 * @top_right: the top right corner's radius in px, or a negative value
 * @bottom_right: the bottom right corner's radius in px, or a negative value
 * @bottom_left: the bottom left corner's radius in px, or a negative value
 *
 * Sets the radius of each corner. A negative value leaves that corner to
 * [property@Panel:corner-radius]. Each is at most half the shorter side.
 */
void
glass_panel_set_corner_radii (GlassPanel *self,
                              double      top_left,
                              double      top_right,
                              double      bottom_right,
                              double      bottom_left)
{
  g_return_if_fail (GLASS_IS_PANEL (self));

  g_object_freeze_notify (G_OBJECT (self));
  set_corner (self, 0, top_left);
  set_corner (self, 1, top_right);
  set_corner (self, 2, bottom_right);
  set_corner (self, 3, bottom_left);
  g_object_thaw_notify (G_OBJECT (self));
}

/**
 * glass_panel_get_tint:
 * @self: a panel
 * @tint: (out): the tint in use, its alpha being how much
 *
 * Gets the tint: the one set on the panel, or else the context's
 * [property@Context:tint-color] (or the material's colour) with the
 * `tint-strength` in effect. The materials tinted with the theme's colour
 * give white here, as the colour is only known when drawing.
 *
 * Returns: %TRUE if the tint was set on the panel
 */
gboolean
glass_panel_get_tint (GlassPanel *self,
                      GdkRGBA    *tint)
{
  GlassPanelPrivate *priv;
  double params[GLASS_N_PARAMS];
  float rgba[4];

  g_return_val_if_fail (GLASS_IS_PANEL (self), FALSE);

  priv = PRIV (self);
  if (priv->tint_set)
    *tint = priv->tint;
  else
    {
      glass_panel_resolve_params (self, glass_context_get_default (), params);
      if (!accent_tint (self, params[GLASS_PARAM_ID_TINT_STRENGTH], rgba))
        glass_context_resolve_tint (glass_context_get_default (), priv->material,
                                    params[GLASS_PARAM_ID_TINT_STRENGTH], NULL, rgba);
      *tint = (GdkRGBA) { rgba[0], rgba[1], rgba[2], rgba[3] };
    }
  return priv->tint_set;
}

/**
 * glass_panel_set_tint:
 * @self: a panel
 * @tint: (nullable): the tint, its alpha being how much; %NULL for the
 *   context's or the material's
 *
 * Sets the colour mixed into the glass, and how much of it. It takes
 * precedence over [property@Context:tint-color] and the `tint-strength`
 * parameter.
 */
void
glass_panel_set_tint (GlassPanel    *self,
                      const GdkRGBA *tint)
{
  GlassPanelPrivate *priv;

  g_return_if_fail (GLASS_IS_PANEL (self));

  priv = PRIV (self);
  if (tint == NULL && !priv->tint_set)
    return;
  if (tint && priv->tint_set && gdk_rgba_equal (tint, &priv->tint))
    return;
  priv->tint_set = tint != NULL;
  if (tint)
    priv->tint = *tint;
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_TINT]);
  gtk_widget_queue_draw (GTK_WIDGET (self));
}

/**
 * glass_panel_set_param:
 * @self: a panel
 * @key: a parameter, one of [method@Context.list_params]; the
 *   `GLASS_PARAM_…` constants name them (%GLASS_PARAM_BLUR_RADIUS …)
 * @value: its value
 *
 * Sets a parameter for this panel's glass only: it takes precedence over
 * the value set on the [class@Context] and over the material's. Values
 * outside the parameter's range are clamped, with a warning.
 *
 * Use it for glass that should look different from the rest (a large card
 * with a stronger lens, a panel without blur); for all glass at once, use
 * [method@Context.set_param].
 *
 * Returns: %FALSE if @key is not a parameter
 */
gboolean
glass_panel_set_param (GlassPanel *self,
                       const char *key,
                       double      value)
{
  GlassPanelPrivate *priv;
  int i;

  g_return_val_if_fail (GLASS_IS_PANEL (self), FALSE);

  i = glass_param_check ("glass_panel_set_param", key, value, &value);
  if (i < 0)
    return FALSE;
  priv = PRIV (self);
  if (priv->param_value[i] == value)
    return TRUE;
  priv->param_value[i] = value;
  gtk_widget_queue_draw (GTK_WIDGET (self));
  return TRUE;
}

/**
 * glass_panel_set_lens:
 * @self: a panel
 * @lens: the lens
 *
 * Gives this panel one of the measured lenses: sets its own values of
 * `max-z`, `profile-shape-n` and `displacement-scale` together, over the
 * context's and the material's. [method@Panel.reset_param] on those three
 * keys goes back to them.
 *
 * Since: 0.9
 */
void
glass_panel_set_lens (GlassPanel *self,
                      GlassLens   lens)
{
  GlassPanelPrivate *priv;

  g_return_if_fail (GLASS_IS_PANEL (self));
  g_return_if_fail ((int) lens >= 0 && (int) lens < GLASS_N_LENSES);

  priv = PRIV (self);
  priv->param_value[GLASS_PARAM_ID_MAX_Z] = glass_lens_specs[lens].max_z;
  priv->param_value[GLASS_PARAM_ID_PROFILE_SHAPE_N] = glass_lens_specs[lens].profile_shape_n;
  priv->param_value[GLASS_PARAM_ID_DISPLACEMENT_SCALE] = glass_lens_specs[lens].displacement_scale;
  gtk_widget_queue_draw (GTK_WIDGET (self));
}

/**
 * glass_panel_get_param:
 * @self: a panel
 * @key: a parameter
 *
 * Gets the value set with [method@Panel.set_param]; NaN if none is set or
 * @key is not a parameter.
 *
 * Returns: the value set with [method@Panel.set_param]; NaN if none is set
 *   or @key is not a parameter
 */
double
glass_panel_get_param (GlassPanel *self,
                       const char *key)
{
  int i;

  g_return_val_if_fail (GLASS_IS_PANEL (self), NAN);

  i = glass_param_find (key);
  return i < 0 ? NAN : PRIV (self)->param_value[i];
}

/**
 * glass_panel_is_param_set:
 * @self: a panel
 * @key: a parameter
 *
 * Gets whether @key was set with [method@Panel.set_param].
 *
 * Returns: whether @key was set with [method@Panel.set_param]
 */
gboolean
glass_panel_is_param_set (GlassPanel *self,
                          const char *key)
{
  int i;

  g_return_val_if_fail (GLASS_IS_PANEL (self), FALSE);

  i = glass_param_find (key);
  return i >= 0 && !isnan (PRIV (self)->param_value[i]);
}

/**
 * glass_panel_reset_param:
 * @self: a panel
 * @key: a parameter
 *
 * Returns @key to the context's or the material's value.
 */
void
glass_panel_reset_param (GlassPanel *self,
                         const char *key)
{
  GlassPanelPrivate *priv;
  int i;

  g_return_if_fail (GLASS_IS_PANEL (self));

  i = glass_param_find (key);
  priv = PRIV (self);
  if (i < 0 || isnan (priv->param_value[i]))
    return;
  priv->param_value[i] = NAN;
  gtk_widget_queue_draw (GTK_WIDGET (self));
}

/**
 * glass_panel_get_effective_param:
 * @self: a panel
 * @key: a parameter
 *
 * Gets the value this panel's glass uses: its own, else the one set on the
 * [class@Context], else the material's, else the default; NaN if @key is not
 * a parameter.
 *
 * Returns: the value this panel's glass uses: its own, else the one set on
 *   the [class@Context], else the material's, else the default; NaN if @key
 *   is not a parameter
 */
double
glass_panel_get_effective_param (GlassPanel *self,
                                 const char *key)
{
  double values[GLASS_N_PARAMS];
  int i;

  g_return_val_if_fail (GLASS_IS_PANEL (self), NAN);

  i = glass_param_find (key);
  if (i < 0)
    return NAN;
  glass_panel_resolve_params (self, glass_context_get_default (), values);
  return values[i];
}

/**
 * glass_panel_get_has_shadow:
 * @self: a panel
 *
 * Gets whether the glass casts a shadow.
 *
 * Returns: whether the glass casts a shadow
 */
gboolean
glass_panel_get_has_shadow (GlassPanel *self)
{
  g_return_val_if_fail (GLASS_IS_PANEL (self), TRUE);

  return PRIV (self)->has_shadow;
}

/**
 * glass_panel_set_has_shadow:
 * @self: a panel
 * @has_shadow: whether the glass casts a shadow
 *
 * Sets whether the glass casts a shadow.
 */
void
glass_panel_set_has_shadow (GlassPanel *self,
                            gboolean    has_shadow)
{
  GlassPanelPrivate *priv;

  g_return_if_fail (GLASS_IS_PANEL (self));

  priv = PRIV (self);
  has_shadow = !!has_shadow;
  if (priv->has_shadow == has_shadow)
    return;
  priv->has_shadow = has_shadow;
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_HAS_SHADOW]);
  gtk_widget_queue_draw (GTK_WIDGET (self));
}

/**
 * glass_panel_get_adaptive:
 * @self: a panel
 *
 * Gets how the foreground colour adapts.
 *
 * Returns: how the foreground colour adapts
 */
GlassAdaptiveMode
glass_panel_get_adaptive (GlassPanel *self)
{
  g_return_val_if_fail (GLASS_IS_PANEL (self), GLASS_ADAPTIVE_MODE_AUTO);

  return PRIV (self)->adaptive;
}

/**
 * glass_panel_set_adaptive:
 * @self: a panel
 * @mode: the mode
 *
 * Sets how the colour of what is on the glass follows what is under it.
 */
void
glass_panel_set_adaptive (GlassPanel        *self,
                          GlassAdaptiveMode  mode)
{
  GlassPanelPrivate *priv;

  g_return_if_fail (GLASS_IS_PANEL (self));

  priv = PRIV (self);
  if (priv->adaptive == mode)
    return;
  priv->adaptive = mode;
  if (!glass_panel_uses_adaptive (self))
    set_appearance (self, GLASS_APPEARANCE_UNKNOWN);
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_ADAPTIVE]);
  gtk_widget_queue_draw (GTK_WIDGET (self));
}

/**
 * glass_panel_get_appearance:
 * @self: a panel
 *
 * Gets how light the glass looks.
 *
 * Returns: how light the glass looks
 */
GlassAppearance
glass_panel_get_appearance (GlassPanel *self)
{
  g_return_val_if_fail (GLASS_IS_PANEL (self), GLASS_APPEARANCE_UNKNOWN);

  return PRIV (self)->appearance;
}
