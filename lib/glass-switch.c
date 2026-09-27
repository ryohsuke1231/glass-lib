/* glass-switch.c — an on/off switch whose knob turns to glass when touched
 * (design.md §6.7).
 *
 * The track is plain (CSS); the knob is a GlassPanel over it, in a small
 * GlassView of the switch's own, so the switch works anywhere, not only in
 * the overlays of an app's view. At rest the knob is nearly opaque white;
 * pressed or dragged it swells and clears into a lens over the track.
 *
 * GtkActionable goes through a hidden GtkSwitch (the proxy), whose :active
 * mirrors ours: GTK has no public API to watch an action's state, and the
 * proxy's action helper already knows how a switch follows a boolean state
 * (and how not to activate the action again while reporting it).
 *
 * SPDX-License-Identifier: MIT
 */
#include "glass-private.h"

#include <adwaita.h>

/**
 * GlassSwitch:
 *
 * The glass counterpart of `GtkSwitch`. Its knob is a small pane of glass:
 * opaque at rest, a lens over the track while it is pressed or dragged.
 *
 * ## CSS nodes
 *
 * `GlassSwitch` has a CSS node with name `glassswitch`, with the style class
 * `.active` when it is on. The track is a node `track` inside a `glassview`;
 * the knob is a [class@Panel] with the style class `.switch-knob`.
 *
 * ## Actions
 *
 * `GlassSwitch` is a `GtkActionable`, like `GtkSwitch`: with a boolean
 * stateful action, the switch follows the action's state and turning it
 * activates the action. It is insensitive while the action is disabled.
 *
 * ## Accessibility
 *
 * `GlassSwitch` uses the %GTK_ACCESSIBLE_ROLE_SWITCH role.
 */

#define KNOB 26
#define PAD  2
/* Room around the switch for the knob to swell into when touched: the
 * view is this much bigger than the switch on every side. */
#define BLEED 10

struct _GlassSwitch {
  GtkWidget     parent_instance;

  GtkWidget    *view;
  GtkWidget    *track;
  GtkWidget    *knob;
  GtkWidget    *proxy;            /* hidden GtkSwitch: the GtkActionable */
  gboolean      active;

  double        pos;              /* 0 off .. 1 on, as drawn */
  AdwAnimation *slide;
  double        lens;             /* 0 opaque knob .. 1 lens */
  AdwAnimation *lens_anim;

  gboolean      dragging;
  double        drag_start_pos;
};

enum {
  PROP_0,
  PROP_ACTIVE,
  N_PROPS,
  /* GtkActionable */
  PROP_ACTION_NAME = N_PROPS,
  PROP_ACTION_TARGET,
};

static GParamSpec *props[N_PROPS];

static void glass_switch_actionable_init (GtkActionableInterface *iface);

G_DEFINE_FINAL_TYPE_WITH_CODE (GlassSwitch, glass_switch, GTK_TYPE_WIDGET,
                               G_IMPLEMENT_INTERFACE (GTK_TYPE_ACTIONABLE, glass_switch_actionable_init))

static int
travel (GlassSwitch *self)
{
  return MAX (0, gtk_widget_get_width (GTK_WIDGET (self)) - KNOB - 2 * PAD);
}

static void
place_knob (GlassSwitch *self)
{
  glass_view_set_overlay_offset (GLASS_VIEW (self->view), self->knob, self->pos * travel (self), 0);
}

static void
update_knob_tint (GlassSwitch *self)
{
  /* Opaque white at rest, a clear lens when touched. */
  GdkRGBA tint = { 1, 1, 1, (float) (0.94 - 0.84 * self->lens) };

  glass_panel_set_tint (GLASS_PANEL (self->knob), &tint);
}

static void
slide_value (double value, gpointer data)
{
  GlassSwitch *self = data;

  self->pos = value;
  place_knob (self);
}

static void
lens_value (double value, gpointer data)
{
  GlassSwitch *self = data;

  self->lens = CLAMP (value, 0.0, 1.0);
  update_knob_tint (self);
}

static void
animate (AdwAnimation *animation, double from, double to)
{
  adw_spring_animation_set_value_from (ADW_SPRING_ANIMATION (animation), from);
  adw_spring_animation_set_value_to (ADW_SPRING_ANIMATION (animation), to);
  adw_animation_play (animation);
}

static void
touch (GlassSwitch *self, gboolean touched)
{
  animate (self->lens_anim, self->lens, touched ? 1.0 : 0.0);
  glass_panel_set_pressed (GLASS_PANEL (self->knob), touched);
}

static void
sync_state (GlassSwitch *self, gboolean animate_it)
{
  if (self->active)
    gtk_widget_add_css_class (GTK_WIDGET (self), "active");
  else
    gtk_widget_remove_css_class (GTK_WIDGET (self), "active");
  gtk_accessible_update_state (GTK_ACCESSIBLE (self), GTK_ACCESSIBLE_STATE_CHECKED,
                               self->active ? GTK_ACCESSIBLE_TRISTATE_TRUE : GTK_ACCESSIBLE_TRISTATE_FALSE,
                               -1);
  if (animate_it && gtk_widget_get_mapped (GTK_WIDGET (self)))
    animate (self->slide, self->pos, self->active ? 1.0 : 0.0);
  else
    {
      adw_animation_skip (self->slide);
      self->pos = self->active ? 1.0 : 0.0;
      place_knob (self);
    }
}

static void
drag_begin (GtkGestureDrag *gesture, double x, double y, GlassSwitch *self)
{
  self->dragging = FALSE;
  self->drag_start_pos = self->pos;
  touch (self, TRUE);
  gtk_widget_grab_focus (GTK_WIDGET (self));
}

static void
drag_update (GtkGestureDrag *gesture, double dx, double dy, GlassSwitch *self)
{
  int t = travel (self);

  if (!self->dragging && ABS (dx) < 3)
    return;
  self->dragging = TRUE;
  adw_animation_pause (self->slide);
  self->pos = t > 0 ? CLAMP (self->drag_start_pos + dx / t, 0.0, 1.0) : self->pos;
  place_knob (self);
}

static void
drag_end (GtkGestureDrag *gesture, double dx, double dy, GlassSwitch *self)
{
  touch (self, FALSE);
  glass_switch_set_active (self, self->dragging ? self->pos > 0.5 : !self->active);
  /* Snap the knob home even when the state did not change. */
  animate (self->slide, self->pos, self->active ? 1.0 : 0.0);
}

static gboolean
key_pressed (GtkEventControllerKey *controller, guint keyval, guint keycode,
             GdkModifierType state, GlassSwitch *self)
{
  if (keyval == GDK_KEY_space || keyval == GDK_KEY_Return || keyval == GDK_KEY_KP_Enter)
    {
      glass_switch_set_active (self, !self->active);
      return GDK_EVENT_STOP;
    }
  return GDK_EVENT_PROPAGATE;
}

/* ── GtkActionable, through the proxy ── */

static const char *
get_action_name (GtkActionable *actionable)
{
  return gtk_actionable_get_action_name (GTK_ACTIONABLE (GLASS_SWITCH (actionable)->proxy));
}

static void
set_action_name (GtkActionable *actionable, const char *name)
{
  gtk_actionable_set_action_name (GTK_ACTIONABLE (GLASS_SWITCH (actionable)->proxy), name);
  g_object_notify (G_OBJECT (actionable), "action-name");
}

static GVariant *
get_action_target_value (GtkActionable *actionable)
{
  return gtk_actionable_get_action_target_value (GTK_ACTIONABLE (GLASS_SWITCH (actionable)->proxy));
}

static void
set_action_target_value (GtkActionable *actionable, GVariant *target)
{
  gtk_actionable_set_action_target_value (GTK_ACTIONABLE (GLASS_SWITCH (actionable)->proxy), target);
  g_object_notify (G_OBJECT (actionable), "action-target");
}

static void
glass_switch_actionable_init (GtkActionableInterface *iface)
{
  iface->get_action_name = get_action_name;
  iface->set_action_name = set_action_name;
  iface->get_action_target_value = get_action_target_value;
  iface->set_action_target_value = set_action_target_value;
}

/* The action's state changed the proxy (or we did, through it). */
static void
proxy_active_changed (GtkSwitch *proxy, GParamSpec *pspec, GlassSwitch *self)
{
  glass_switch_set_active (self, gtk_switch_get_active (proxy));
}

/* The action was enabled or disabled. */
static void
proxy_sensitive_changed (GtkWidget *proxy, GParamSpec *pspec, GlassSwitch *self)
{
  gtk_widget_set_sensitive (GTK_WIDGET (self), gtk_widget_get_sensitive (proxy));
}

static void
glass_switch_measure (GtkWidget *widget, GtkOrientation orientation, int for_size,
                      int *minimum, int *natural, int *minimum_baseline, int *natural_baseline)
{
  *minimum = *natural = orientation == GTK_ORIENTATION_HORIZONTAL ? 2 * KNOB : KNOB + 2 * PAD;
}

static void
glass_switch_size_allocate (GtkWidget *widget, int width, int height, int baseline)
{
  GlassSwitch *self = GLASS_SWITCH (widget);

  gtk_widget_allocate (self->view, width + 2 * BLEED, height + 2 * BLEED, -1,
                       gsk_transform_translate (NULL, &GRAPHENE_POINT_INIT (-BLEED, -BLEED)));
  place_knob (self);
}

static void
glass_switch_dispose (GObject *object)
{
  GlassSwitch *self = GLASS_SWITCH (object);

  /* First: losing the actions turns the proxy off, which must not reach
   * the (disposed) animations. */
  if (self->proxy)
    g_signal_handlers_disconnect_by_data (self->proxy, self);
  g_clear_pointer (&self->proxy, gtk_widget_unparent);
  g_clear_object (&self->slide);
  g_clear_object (&self->lens_anim);
  g_clear_pointer (&self->view, gtk_widget_unparent);

  G_OBJECT_CLASS (glass_switch_parent_class)->dispose (object);
}

static void
glass_switch_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  switch (prop_id)
    {
    case PROP_ACTIVE:
      g_value_set_boolean (value, GLASS_SWITCH (object)->active);
      break;
    case PROP_ACTION_NAME:
      g_value_set_string (value, get_action_name (GTK_ACTIONABLE (object)));
      break;
    case PROP_ACTION_TARGET:
      g_value_set_variant (value, get_action_target_value (GTK_ACTIONABLE (object)));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
glass_switch_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
  switch (prop_id)
    {
    case PROP_ACTIVE:
      glass_switch_set_active (GLASS_SWITCH (object), g_value_get_boolean (value));
      break;
    case PROP_ACTION_NAME:
      set_action_name (GTK_ACTIONABLE (object), g_value_get_string (value));
      break;
    case PROP_ACTION_TARGET:
      set_action_target_value (GTK_ACTIONABLE (object), g_value_get_variant (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
glass_switch_class_init (GlassSwitchClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = glass_switch_dispose;
  object_class->get_property = glass_switch_get_property;
  object_class->set_property = glass_switch_set_property;

  widget_class->measure = glass_switch_measure;
  widget_class->size_allocate = glass_switch_size_allocate;

  /**
   * GlassSwitch:active:
   *
   * Whether the switch is on.
   */
  props[PROP_ACTIVE] =
    g_param_spec_boolean ("active", NULL, NULL, FALSE,
                          G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, N_PROPS, props);
  g_object_class_override_property (object_class, PROP_ACTION_NAME, "action-name");
  g_object_class_override_property (object_class, PROP_ACTION_TARGET, "action-target");

  gtk_widget_class_set_css_name (widget_class, "glassswitch");
  gtk_widget_class_set_accessible_role (widget_class, GTK_ACCESSIBLE_ROLE_SWITCH);
}

static void
glass_switch_init (GlassSwitch *self)
{
  GtkGesture *drag;
  GtkEventController *keys;
  AdwSpringParams *spring;

  gtk_widget_set_focusable (GTK_WIDGET (self), TRUE);
  gtk_widget_set_valign (GTK_WIDGET (self), GTK_ALIGN_CENTER);

  self->view = glass_view_new ();
  glass_view_set_backdrop_capture_only (GLASS_VIEW (self->view), TRUE);
  gtk_widget_set_parent (self->view, GTK_WIDGET (self));

  self->track = glass_style_node_new ("track");
  gtk_widget_set_margin_start (self->track, BLEED);
  gtk_widget_set_margin_end (self->track, BLEED);
  gtk_widget_set_margin_top (self->track, BLEED);
  gtk_widget_set_margin_bottom (self->track, BLEED);
  glass_view_set_content (GLASS_VIEW (self->view), self->track);

  self->knob = glass_panel_new ();
  gtk_widget_add_css_class (self->knob, "switch-knob");
  gtk_widget_set_size_request (self->knob, KNOB, KNOB);
  gtk_widget_set_halign (self->knob, GTK_ALIGN_START);
  gtk_widget_set_valign (self->knob, GTK_ALIGN_CENTER);
  gtk_widget_set_margin_start (self->knob, PAD + BLEED);
  gtk_widget_set_can_target (self->knob, FALSE);
  glass_panel_set_adaptive (GLASS_PANEL (self->knob), GLASS_ADAPTIVE_MODE_OFF);
  glass_panel_set_press_grow (GLASS_PANEL (self->knob), 14.0, 0.5);
  /* A lens over the track: sharp, not frosted (unless the app sets a blur
   * for all glass; design.md §6.7). */
  glass_panel_set_param_default (GLASS_PANEL (self->knob), GLASS_PARAM_ID_BLUR_RADIUS, 0.0);
  glass_view_add_overlay (GLASS_VIEW (self->view), self->knob);
  update_knob_tint (self);

  /* adw_spring_animation_new() takes the params and the target (transfer
   * full): each animation gets its own (docs/memo.md 地雷18). */
  spring = adw_spring_params_new (0.75, 1.0, 400.0);
  self->slide = adw_spring_animation_new (GTK_WIDGET (self), 0.0, 1.0, adw_spring_params_ref (spring),
                                          adw_callback_animation_target_new (slide_value, self, NULL));
  adw_spring_animation_set_clamp (ADW_SPRING_ANIMATION (self->slide), FALSE);
  self->lens_anim = adw_spring_animation_new (GTK_WIDGET (self), 0.0, 1.0, spring,
                                              adw_callback_animation_target_new (lens_value, self, NULL));

  drag = gtk_gesture_drag_new ();
  g_signal_connect (drag, "drag-begin", G_CALLBACK (drag_begin), self);
  g_signal_connect (drag, "drag-update", G_CALLBACK (drag_update), self);
  g_signal_connect (drag, "drag-end", G_CALLBACK (drag_end), self);
  gtk_widget_add_controller (GTK_WIDGET (self), GTK_EVENT_CONTROLLER (drag));

  keys = gtk_event_controller_key_new ();
  g_signal_connect (keys, "key-pressed", G_CALLBACK (key_pressed), self);
  gtk_widget_add_controller (GTK_WIDGET (self), keys);

  /* Never shown, measured or drawn; in the tree only so that its action
   * helper finds the actions of our ancestors. */
  glass_ensure_action_muxer (GTK_WIDGET (self));
  self->proxy = gtk_switch_new ();
  gtk_widget_set_visible (self->proxy, FALSE);
  gtk_widget_set_parent (self->proxy, GTK_WIDGET (self));
  g_signal_connect (self->proxy, "notify::active", G_CALLBACK (proxy_active_changed), self);
  g_signal_connect (self->proxy, "notify::sensitive", G_CALLBACK (proxy_sensitive_changed), self);

  sync_state (self, FALSE);
}

/**
 * glass_switch_new:
 *
 * Creates a new switch, off.
 *
 * Returns: a new switch, off
 */
GtkWidget *
glass_switch_new (void)
{
  return g_object_new (GLASS_TYPE_SWITCH, NULL);
}

/**
 * glass_switch_get_active:
 * @self: a switch
 *
 * Gets whether the switch is on.
 *
 * Returns: whether the switch is on
 */
gboolean
glass_switch_get_active (GlassSwitch *self)
{
  g_return_val_if_fail (GLASS_IS_SWITCH (self), FALSE);

  return self->active;
}

/**
 * glass_switch_set_active:
 * @self: a switch
 * @active: whether to turn it on
 *
 * Turns the switch on or off.
 */
void
glass_switch_set_active (GlassSwitch *self,
                         gboolean     active)
{
  g_return_if_fail (GLASS_IS_SWITCH (self));

  active = !!active;
  if (self->active == active)
    return;
  self->active = active;
  sync_state (self, TRUE);
  /* With an action, the proxy activates it. */
  gtk_switch_set_active (GTK_SWITCH (self->proxy), active);
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_ACTIVE]);
}
