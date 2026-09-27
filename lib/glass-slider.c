/* glass-slider.c — a slider whose knob turns to glass when touched
 * (design.md §6.7).
 *
 * The rail and its filled part are plain (drawn from CSS colours); the knob
 * is a GlassPanel over them, in a small GlassView of the slider's own, so
 * the slider works anywhere. At rest the knob is nearly opaque white; while
 * it is dragged it swells and clears into a lens that magnifies the rail.
 *
 * SPDX-License-Identifier: MIT
 */
#include "glass-private.h"

#include <adwaita.h>
#include <math.h>

/**
 * GlassSlider:
 *
 * The glass counterpart of a horizontal `GtkScale`. Its knob is a small pane
 * of glass: opaque at rest, a lens over the rail while it is dragged.
 *
 * ## CSS nodes
 *
 * `GlassSlider` has a CSS node with name `glassslider`. Inside its
 * `glassview`, the rail is a node `rail` whose `color` is the unfilled part,
 * with a child `fill` whose `color` is the filled part. The knob is a
 * [class@Panel] with the style class `.slider-knob`.
 *
 * ## Accessibility
 *
 * `GlassSlider` uses the %GTK_ACCESSIBLE_ROLE_SLIDER role.
 */

#define KNOB_W 36
#define KNOB_H 24
#define RAIL_H 6
/* Room around the slider for the knob to swell into when touched. */
#define BLEED 12

/* ── The rail: two rounded bars ── */

#define GLASS_TYPE_SLIDER_RAIL (glass_slider_rail_get_type ())
G_DECLARE_FINAL_TYPE (GlassSliderRail, glass_slider_rail, GLASS, SLIDER_RAIL, GtkWidget)

struct _GlassSliderRail {
  GtkWidget  parent_instance;

  GtkWidget *fill_node;          /* never drawn: the fill colour */
  double     fraction;
};

G_DEFINE_FINAL_TYPE (GlassSliderRail, glass_slider_rail, GTK_TYPE_WIDGET)

static void
glass_slider_rail_snapshot (GtkWidget *widget, GtkSnapshot *snapshot)
{
  GlassSliderRail *self = GLASS_SLIDER_RAIL (widget);
  float w = gtk_widget_get_width (widget);
  float h = gtk_widget_get_height (widget);
  /* The rail runs between the knob's centres at the two ends. */
  float x0 = KNOB_W / 2.0f, x1 = MAX (x0, w - KNOB_W / 2.0f);
  float y = (h - RAIL_H) / 2.0f;
  float xf = x0 + (float) self->fraction * (x1 - x0);
  GskRoundedRect bar;
  GdkRGBA rest, fill;

  gtk_widget_get_color (widget, &rest);
  gtk_widget_get_color (self->fill_node, &fill);

  gsk_rounded_rect_init_from_rect (&bar, &GRAPHENE_RECT_INIT (x0 - RAIL_H / 2.0f, y, x1 - x0 + RAIL_H, RAIL_H), RAIL_H / 2.0f);
  gtk_snapshot_push_rounded_clip (snapshot, &bar);
  gtk_snapshot_append_color (snapshot, &rest, &bar.bounds);
  gtk_snapshot_append_color (snapshot, &fill,
                             &GRAPHENE_RECT_INIT (bar.bounds.origin.x, y, xf - bar.bounds.origin.x, RAIL_H));
  gtk_snapshot_pop (snapshot);
}

static void
glass_slider_rail_size_allocate (GtkWidget *widget, int width, int height, int baseline)
{
  gtk_widget_allocate (GLASS_SLIDER_RAIL (widget)->fill_node, 0, 0, -1, NULL);
}

static void
glass_slider_rail_dispose (GObject *object)
{
  g_clear_pointer (&GLASS_SLIDER_RAIL (object)->fill_node, gtk_widget_unparent);
  G_OBJECT_CLASS (glass_slider_rail_parent_class)->dispose (object);
}

static void
glass_slider_rail_class_init (GlassSliderRailClass *klass)
{
  G_OBJECT_CLASS (klass)->dispose = glass_slider_rail_dispose;
  GTK_WIDGET_CLASS (klass)->snapshot = glass_slider_rail_snapshot;
  GTK_WIDGET_CLASS (klass)->size_allocate = glass_slider_rail_size_allocate;
  gtk_widget_class_set_css_name (GTK_WIDGET_CLASS (klass), "rail");
}

static void
glass_slider_rail_init (GlassSliderRail *self)
{
  self->fill_node = glass_style_node_new ("fill");
  gtk_widget_set_parent (self->fill_node, GTK_WIDGET (self));
}

/* ── The slider ── */

struct _GlassSlider {
  GtkWidget      parent_instance;

  GtkWidget     *view;
  GtkWidget     *rail;
  GtkWidget     *knob;
  GtkAdjustment *adjustment;

  double         lens;
  AdwAnimation  *lens_anim;
  double         drag_start_x;
};

enum {
  PROP_0,
  PROP_ADJUSTMENT,
  N_PROPS
};

static GParamSpec *props[N_PROPS];

G_DEFINE_FINAL_TYPE (GlassSlider, glass_slider, GTK_TYPE_WIDGET)

static double
fraction (GlassSlider *self)
{
  double lower = gtk_adjustment_get_lower (self->adjustment);
  double upper = gtk_adjustment_get_upper (self->adjustment) - gtk_adjustment_get_page_size (self->adjustment);

  return upper > lower ? CLAMP ((gtk_adjustment_get_value (self->adjustment) - lower) / (upper - lower), 0.0, 1.0) : 0.0;
}

static void
place_knob (GlassSlider *self)
{
  int travel = MAX (0, gtk_widget_get_width (GTK_WIDGET (self)) - KNOB_W);
  double f = fraction (self);

  GLASS_SLIDER_RAIL (self->rail)->fraction = f;
  gtk_widget_queue_draw (self->rail);
  glass_view_set_overlay_offset (GLASS_VIEW (self->view), self->knob, f * travel, 0);
  gtk_accessible_update_property (GTK_ACCESSIBLE (self),
                                  GTK_ACCESSIBLE_PROPERTY_VALUE_NOW, gtk_adjustment_get_value (self->adjustment),
                                  GTK_ACCESSIBLE_PROPERTY_VALUE_MIN, gtk_adjustment_get_lower (self->adjustment),
                                  GTK_ACCESSIBLE_PROPERTY_VALUE_MAX, gtk_adjustment_get_upper (self->adjustment),
                                  -1);
}

static void
lens_value (double value, gpointer data)
{
  GlassSlider *self = data;
  GdkRGBA tint;

  self->lens = CLAMP (value, 0.0, 1.0);
  tint = (GdkRGBA) { 1, 1, 1, (float) (0.94 - 0.86 * self->lens) };
  glass_panel_set_tint (GLASS_PANEL (self->knob), &tint);
}

static void
touch (GlassSlider *self, gboolean touched)
{
  adw_spring_animation_set_value_from (ADW_SPRING_ANIMATION (self->lens_anim), self->lens);
  adw_spring_animation_set_value_to (ADW_SPRING_ANIMATION (self->lens_anim), touched ? 1.0 : 0.0);
  adw_animation_play (self->lens_anim);
  glass_panel_set_pressed (GLASS_PANEL (self->knob), touched);
}

static void
set_from_x (GlassSlider *self, double x)
{
  int travel = MAX (1, gtk_widget_get_width (GTK_WIDGET (self)) - KNOB_W);
  double f = CLAMP ((x - KNOB_W / 2.0) / travel, 0.0, 1.0);
  double lower = gtk_adjustment_get_lower (self->adjustment);
  double upper = gtk_adjustment_get_upper (self->adjustment) - gtk_adjustment_get_page_size (self->adjustment);

  gtk_adjustment_set_value (self->adjustment, lower + f * (upper - lower));
}

static void
drag_begin (GtkGestureDrag *gesture, double x, double y, GlassSlider *self)
{
  self->drag_start_x = x;
  set_from_x (self, x);
  touch (self, TRUE);
  gtk_widget_grab_focus (GTK_WIDGET (self));
}

static void
drag_update (GtkGestureDrag *gesture, double dx, double dy, GlassSlider *self)
{
  set_from_x (self, self->drag_start_x + dx);
}

static void
drag_end (GtkGestureDrag *gesture, double dx, double dy, GlassSlider *self)
{
  touch (self, FALSE);
}

static gboolean
key_pressed (GtkEventControllerKey *controller, guint keyval, guint keycode,
             GdkModifierType state, GlassSlider *self)
{
  double step = gtk_adjustment_get_step_increment (self->adjustment);
  double value = gtk_adjustment_get_value (self->adjustment);

  if (step <= 0.0)
    step = (gtk_adjustment_get_upper (self->adjustment) - gtk_adjustment_get_lower (self->adjustment)) / 20.0;
  switch (keyval)
    {
    case GDK_KEY_Left:
    case GDK_KEY_Down:
      gtk_adjustment_set_value (self->adjustment, value - step);
      return GDK_EVENT_STOP;
    case GDK_KEY_Right:
    case GDK_KEY_Up:
      gtk_adjustment_set_value (self->adjustment, value + step);
      return GDK_EVENT_STOP;
    case GDK_KEY_Home:
      gtk_adjustment_set_value (self->adjustment, gtk_adjustment_get_lower (self->adjustment));
      return GDK_EVENT_STOP;
    case GDK_KEY_End:
      gtk_adjustment_set_value (self->adjustment, gtk_adjustment_get_upper (self->adjustment));
      return GDK_EVENT_STOP;
    default:
      return GDK_EVENT_PROPAGATE;
    }
}

static void
glass_slider_measure (GtkWidget *widget, GtkOrientation orientation, int for_size,
                      int *minimum, int *natural, int *minimum_baseline, int *natural_baseline)
{
  if (orientation == GTK_ORIENTATION_HORIZONTAL)
    {
      *minimum = KNOB_W * 2;
      *natural = 200;
    }
  else
    *minimum = *natural = KNOB_H + 8;
}

static void
glass_slider_size_allocate (GtkWidget *widget, int width, int height, int baseline)
{
  GlassSlider *self = GLASS_SLIDER (widget);

  gtk_widget_allocate (self->view, width + 2 * BLEED, height + 2 * BLEED, -1,
                       gsk_transform_translate (NULL, &GRAPHENE_POINT_INIT (-BLEED, -BLEED)));
  place_knob (self);
}

static void
glass_slider_dispose (GObject *object)
{
  GlassSlider *self = GLASS_SLIDER (object);

  if (self->adjustment)
    g_signal_handlers_disconnect_by_data (self->adjustment, self);
  g_clear_object (&self->adjustment);
  g_clear_object (&self->lens_anim);
  g_clear_pointer (&self->view, gtk_widget_unparent);

  G_OBJECT_CLASS (glass_slider_parent_class)->dispose (object);
}

static void
glass_slider_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  switch (prop_id)
    {
    case PROP_ADJUSTMENT:
      g_value_set_object (value, GLASS_SLIDER (object)->adjustment);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
glass_slider_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
  switch (prop_id)
    {
    case PROP_ADJUSTMENT:
      glass_slider_set_adjustment (GLASS_SLIDER (object), g_value_get_object (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
glass_slider_class_init (GlassSliderClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = glass_slider_dispose;
  object_class->get_property = glass_slider_get_property;
  object_class->set_property = glass_slider_set_property;

  widget_class->measure = glass_slider_measure;
  widget_class->size_allocate = glass_slider_size_allocate;

  /**
   * GlassSlider:adjustment:
   *
   * The value and its range.
   */
  props[PROP_ADJUSTMENT] =
    g_param_spec_object ("adjustment", NULL, NULL, GTK_TYPE_ADJUSTMENT,
                         G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, N_PROPS, props);

  gtk_widget_class_set_css_name (widget_class, "glassslider");
  gtk_widget_class_set_accessible_role (widget_class, GTK_ACCESSIBLE_ROLE_SLIDER);
}

static void
glass_slider_init (GlassSlider *self)
{
  GtkGesture *drag;
  GtkEventController *keys;
  AdwSpringParams *spring;

  gtk_widget_set_focusable (GTK_WIDGET (self), TRUE);

  self->view = glass_view_new ();
  glass_view_set_backdrop_capture_only (GLASS_VIEW (self->view), TRUE);
  gtk_widget_set_parent (self->view, GTK_WIDGET (self));

  self->rail = g_object_new (GLASS_TYPE_SLIDER_RAIL, NULL);
  gtk_widget_set_margin_start (self->rail, BLEED);
  gtk_widget_set_margin_end (self->rail, BLEED);
  gtk_widget_set_margin_top (self->rail, BLEED);
  gtk_widget_set_margin_bottom (self->rail, BLEED);
  glass_view_set_content (GLASS_VIEW (self->view), self->rail);

  self->knob = glass_panel_new ();
  gtk_widget_add_css_class (self->knob, "slider-knob");
  gtk_widget_set_size_request (self->knob, KNOB_W, KNOB_H);
  gtk_widget_set_halign (self->knob, GTK_ALIGN_START);
  gtk_widget_set_valign (self->knob, GTK_ALIGN_CENTER);
  gtk_widget_set_margin_start (self->knob, BLEED);
  gtk_widget_set_can_target (self->knob, FALSE);
  glass_panel_set_adaptive (GLASS_PANEL (self->knob), GLASS_ADAPTIVE_MODE_OFF);
  glass_panel_set_press_grow (GLASS_PANEL (self->knob), 18.0, 0.5);
  /* A lens over the track: sharp, not frosted (unless the app sets a blur
   * for all glass; design.md §6.7). */
  glass_panel_set_param_default (GLASS_PANEL (self->knob), GLASS_PARAM_ID_BLUR_RADIUS, 0.0);
  glass_view_add_overlay (GLASS_VIEW (self->view), self->knob);

  /* The params and the target are the animation's (transfer full). */
  spring = adw_spring_params_new (0.75, 1.0, 400.0);
  self->lens_anim = adw_spring_animation_new (GTK_WIDGET (self), 0.0, 1.0, spring,
                                              adw_callback_animation_target_new (lens_value, self, NULL));
  lens_value (0.0, self);

  drag = gtk_gesture_drag_new ();
  g_signal_connect (drag, "drag-begin", G_CALLBACK (drag_begin), self);
  g_signal_connect (drag, "drag-update", G_CALLBACK (drag_update), self);
  g_signal_connect (drag, "drag-end", G_CALLBACK (drag_end), self);
  gtk_widget_add_controller (GTK_WIDGET (self), GTK_EVENT_CONTROLLER (drag));

  keys = gtk_event_controller_key_new ();
  g_signal_connect (keys, "key-pressed", G_CALLBACK (key_pressed), self);
  gtk_widget_add_controller (GTK_WIDGET (self), keys);

  glass_slider_set_adjustment (self, NULL);
}

/**
 * glass_slider_new:
 * @adjustment: (nullable): the value and its range
 *
 * Creates a new slider.
 *
 * Returns: a new slider
 */
GtkWidget *
glass_slider_new (GtkAdjustment *adjustment)
{
  return g_object_new (GLASS_TYPE_SLIDER, "adjustment", adjustment, NULL);
}

/**
 * glass_slider_new_with_range:
 * @min: the smallest value
 * @max: the largest value
 * @step: the step of the arrow keys
 *
 * Creates a new slider at @min.
 *
 * Returns: a new slider at @min
 */
GtkWidget *
glass_slider_new_with_range (double min,
                             double max,
                             double step)
{
  return glass_slider_new (gtk_adjustment_new (min, min, max, step, step * 10, 0));
}

/**
 * glass_slider_get_adjustment:
 * @self: a slider
 *
 * Gets the adjustment.
 *
 * Returns: (transfer none): the adjustment
 */
GtkAdjustment *
glass_slider_get_adjustment (GlassSlider *self)
{
  g_return_val_if_fail (GLASS_IS_SLIDER (self), NULL);

  return self->adjustment;
}

/**
 * glass_slider_set_adjustment:
 * @self: a slider
 * @adjustment: (nullable): the value and its range; %NULL for 0..1
 *
 * Sets the adjustment.
 */
void
glass_slider_set_adjustment (GlassSlider   *self,
                             GtkAdjustment *adjustment)
{
  g_return_if_fail (GLASS_IS_SLIDER (self));

  if (adjustment == NULL)
    adjustment = gtk_adjustment_new (0, 0, 1, 0.05, 0.1, 0);
  if (self->adjustment == adjustment)
    return;

  if (self->adjustment)
    g_signal_handlers_disconnect_by_data (self->adjustment, self);
  g_object_ref_sink (adjustment);
  g_clear_object (&self->adjustment);
  self->adjustment = adjustment;
  g_signal_connect_swapped (adjustment, "value-changed", G_CALLBACK (place_knob), self);
  g_signal_connect_swapped (adjustment, "changed", G_CALLBACK (place_knob), self);
  place_knob (self);
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_ADJUSTMENT]);
}

/**
 * glass_slider_get_value:
 * @self: a slider
 *
 * Gets the value.
 *
 * Returns: the value
 */
double
glass_slider_get_value (GlassSlider *self)
{
  g_return_val_if_fail (GLASS_IS_SLIDER (self), 0.0);

  return gtk_adjustment_get_value (self->adjustment);
}

/**
 * glass_slider_set_value:
 * @self: a slider
 * @value: the value
 *
 * Sets the value.
 */
void
glass_slider_set_value (GlassSlider *self,
                        double       value)
{
  g_return_if_fail (GLASS_IS_SLIDER (self));

  gtk_adjustment_set_value (self->adjustment, value);
}
