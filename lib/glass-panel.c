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
  GlassMaterial       material;
  double              corner_radius;
  GdkRGBA             tint;
  gboolean            tint_set;
  gboolean            has_shadow;
  GlassAdaptiveMode   adaptive;
  GlassAppearance     appearance;

  GlassView          *view;             /* registered with, not owned */
  GlassPanelMode      mode;
  const char         *shape_class;      /* the CSS radius class in use */

  GlassAdaptiveState  astate;
  GlassLumaStats      last_luma;
  gboolean            have_luma;
  GlassAppearance     pending;
  guint               apply_tick;
  guint               settle_source;

  double              highlight;
} GlassPanelPrivate;

enum {
  PROP_0,
  PROP_CHILD,
  PROP_MATERIAL,
  PROP_CORNER_RADIUS,
  PROP_TINT,
  PROP_HAS_SHADOW,
  PROP_ADAPTIVE,
  PROP_APPEARANCE,
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
  if (priv->corner_radius >= 0.0)
    wanted = glass_style_radius_class (gtk_widget_get_display (widget), priv->corner_radius);

  if (wanted == priv->shape_class)
    return;
  if (priv->shape_class)
    gtk_widget_remove_css_class (widget, priv->shape_class);
  priv->shape_class = wanted;
  if (wanted)
    gtk_widget_add_css_class (widget, wanted);
}

static void set_appearance (GlassPanel *self, GlassAppearance appearance);

void
glass_panel_set_mode (GlassPanel     *self,
                      GlassPanelMode  mode)
{
  GlassPanelPrivate *priv = PRIV (self);
  GtkWidget *widget = GTK_WIDGET (self);
  gboolean hc = glass_context_get_high_contrast (glass_context_get_default ());

  if (mode == GLASS_PANEL_MODE_NONE)
    priv->view = NULL;

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
  if (hc && mode != GLASS_PANEL_MODE_NESTED)
    gtk_widget_add_css_class (widget, "glass-high-contrast");

  priv->mode = mode;
  update_shape_class (self);

  /* Only a panel the view draws is measured; the others keep the theme's
   * colours (the fallback's own sampling is design.md §12.1, not done yet). */
  if (mode != GLASS_PANEL_MODE_VIEW || hc)
    set_appearance (self, GLASS_APPEARANCE_UNKNOWN);
}

GlassPanelMode
glass_panel_get_mode (GlassPanel *self)
{
  return PRIV (self)->mode;
}

static void
find_view (GlassPanel *self)
{
  GlassPanelPrivate *priv = PRIV (self);
  GtkWidget *widget = GTK_WIDGET (self);
  GtkWidget *parent = gtk_widget_get_parent (widget);
  GtkWidget *view = parent ? gtk_widget_get_ancestor (parent, GLASS_TYPE_VIEW) : NULL;
  GtkWidget *outer = parent ? gtk_widget_get_ancestor (parent, GLASS_TYPE_PANEL) : NULL;

  /* Glass is not drawn on glass (design.md §6.6): a panel inside another
   * panel is only a faint shape. */
  if (outer)
    {
      glass_panel_set_mode (self, GLASS_PANEL_MODE_NESTED);
      return;
    }

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

  if (priv->view)
    glass_view_unregister_panel (priv->view, self);
  priv->view = NULL;
  priv->mode = GLASS_PANEL_MODE_NONE;
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

  gtk_widget_remove_css_class (widget, "glass-light");
  gtk_widget_remove_css_class (widget, "glass-dark");
  if (appearance == GLASS_APPEARANCE_LIGHT)
    gtk_widget_add_css_class (widget, "glass-light");
  else if (appearance == GLASS_APPEARANCE_DARK)
    gtk_widget_add_css_class (widget, "glass-dark");

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
  if (!priv->have_luma || !glass_panel_uses_adaptive (self) || priv->mode != GLASS_PANEL_MODE_VIEW)
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
  if (glass_panel_uses_adaptive (self) && priv->mode == GLASS_PANEL_MODE_VIEW)
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

void
glass_panel_get_tint_rgba (GlassPanel    *self,
                           const GdkRGBA *theme_bg,
                           float          out[4])
{
  GlassPanelPrivate *priv = PRIV (self);
  const GlassMaterialSpec *m = &glass_material_specs[priv->material];

  if (priv->tint_set)
    {
      out[0] = priv->tint.red;
      out[1] = priv->tint.green;
      out[2] = priv->tint.blue;
      out[3] = priv->tint.alpha;
    }
  else
    {
      memcpy (out, m->tint, sizeof m->tint);
      if (m->tint_from_theme && theme_bg)
        {
          out[0] = theme_bg->red;
          out[1] = theme_bg->green;
          out[2] = theme_bg->blue;
        }
    }

  /* Pressed (GlassButton): a little more of a lighter tint. */
  if (priv->highlight > 0.0)
    {
      double h = priv->highlight;

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
  g_clear_pointer (&priv->child, gtk_widget_unparent);

  G_OBJECT_CLASS (glass_panel_parent_class)->dispose (object);
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
    case PROP_TINT:
      glass_panel_set_tint (self, g_value_get_boxed (value));
      break;
    case PROP_HAS_SHADOW:
      glass_panel_set_has_shadow (self, g_value_get_boolean (value));
      break;
    case PROP_ADAPTIVE:
      glass_panel_set_adaptive (self, g_value_get_enum (value));
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
  object_class->get_property = glass_panel_get_property;
  object_class->set_property = glass_panel_set_property;

  widget_class->measure = glass_panel_measure;
  widget_class->size_allocate = glass_panel_size_allocate;
  widget_class->get_request_mode = glass_panel_get_request_mode;
  widget_class->compute_expand = glass_panel_compute_expand;
  widget_class->root = glass_panel_root;
  widget_class->unroot = glass_panel_unroot;

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
   * [enum@Material.THICK] never adapts.
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
  priv->has_shadow = TRUE;
  priv->adaptive = GLASS_ADAPTIVE_MODE_AUTO;
  priv->appearance = GLASS_APPEARANCE_UNKNOWN;
  glass_adaptive_state_init (&priv->astate);

  gtk_widget_set_overflow (GTK_WIDGET (self), GTK_OVERFLOW_HIDDEN);
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
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_MATERIAL]);
  gtk_widget_queue_draw (GTK_WIDGET (self));
}

/**
 * glass_panel_get_corner_radius:
 * @self: a panel
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
 * glass_panel_get_tint:
 * @self: a panel
 * @tint: (out): the tint in use
 *
 * Returns: %TRUE if the tint was set, %FALSE if it is the material's
 */
gboolean
glass_panel_get_tint (GlassPanel *self,
                      GdkRGBA    *tint)
{
  GlassPanelPrivate *priv;
  float rgba[4];

  g_return_val_if_fail (GLASS_IS_PANEL (self), FALSE);

  priv = PRIV (self);
  if (priv->tint_set)
    *tint = priv->tint;
  else
    {
      memcpy (rgba, glass_material_specs[priv->material].tint, sizeof rgba);
      *tint = (GdkRGBA) { rgba[0], rgba[1], rgba[2], rgba[3] };
    }
  return priv->tint_set;
}

/**
 * glass_panel_set_tint:
 * @self: a panel
 * @tint: (nullable): the tint, its alpha being how much; %NULL for the
 *   material's
 *
 * Sets the colour mixed into the glass.
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
 * glass_panel_get_has_shadow:
 * @self: a panel
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
 * Returns: how light the glass looks
 */
GlassAppearance
glass_panel_get_appearance (GlassPanel *self)
{
  g_return_val_if_fail (GLASS_IS_PANEL (self), GLASS_APPEARANCE_UNKNOWN);

  return PRIV (self)->appearance;
}
