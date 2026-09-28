/* glass-menu-button.c — a button that opens a glass popover or menu
 * (design.md §6.7).
 *
 * GtkMenuButton cannot be subclassed and opens a GtkPopoverMenu, which
 * cannot be glass; this is the same thing with a GlassPopover.
 *
 * SPDX-License-Identifier: MIT
 */
#include "glass-private.h"

#include <adwaita.h>
#include <math.h>

/**
 * GlassMenuButton:
 *
 * A button that opens a [class@Popover]: one given with
 * [method@MenuButton.set_popover], or a menu built from
 * [property@MenuButton:menu-model]. Pack it in a [class@HeaderBar] or a
 * [class@ButtonGroup] like any button.
 *
 * ## CSS nodes
 *
 * `GlassMenuButton` has a CSS node with name `glassmenubutton`, holding a
 * `button`.
 *
 * ## Actions
 *
 * `GlassMenuButton` is a `GtkActionable`: with [property@Gtk.Actionable:action-name]
 * set, the button is insensitive while the action is disabled, and a click
 * activates the action as well as opening the popover.
 */

struct _GlassMenuButton {
  GtkWidget   parent_instance;

  GtkWidget  *button;
  GtkPopover *popover;
  GMenuModel *model;

  /* The menu comes out of the button (design.md §6.8). In a capsule the
   * button leaves it while the menu is open (collapse 0 -> 1: its width,
   * so the capsule's glass closes up behind it) and comes back after. The
   * popover stays where the button was meanwhile (pinned). */
  AdwAnimation   *collapse_anim;
  double          collapse;
  gboolean        pinned;
  graphene_rect_t slot;            /* the button when the menu opened, root coordinates */
};

enum {
  PROP_0,
  PROP_ICON_NAME,
  PROP_LABEL,
  PROP_MENU_MODEL,
  PROP_POPOVER,
  N_PROPS,
  /* GtkActionable */
  PROP_ACTION_NAME = N_PROPS,
  PROP_ACTION_TARGET,
};

static GParamSpec *props[N_PROPS];

static void glass_menu_button_actionable_init (GtkActionableInterface *iface);

G_DEFINE_FINAL_TYPE_WITH_CODE (GlassMenuButton, glass_menu_button, GTK_TYPE_WIDGET,
                               G_IMPLEMENT_INTERFACE (GTK_TYPE_ACTIONABLE, glass_menu_button_actionable_init))

static void
collapse_value (double value, gpointer data)
{
  GlassMenuButton *self = data;

  self->collapse = CLAMP (value, 0.0, 1.0);
  gtk_widget_set_opacity (self->button, 1.0 - self->collapse);
  /* Narrowing out of the capsule cuts the button off rather than squeezing
   * it; at rest nothing is cut (its focus ring reaches outside). */
  gtk_widget_set_overflow (GTK_WIDGET (self), self->collapse > 0.0 ? GTK_OVERFLOW_HIDDEN : GTK_OVERFLOW_VISIBLE);
  gtk_widget_queue_resize (GTK_WIDGET (self));
}

static void
collapse_to (GlassMenuButton *self,
             double           to)
{
  if (self->collapse_anim == NULL)
    {
      AdwAnimationTarget *target = adw_callback_animation_target_new (collapse_value, self, NULL);

      /* Critically damped: the capsule's width must not bounce. */
      self->collapse_anim = adw_spring_animation_new (GTK_WIDGET (self), 0.0, 1.0,
                                                      adw_spring_params_new (1.0, 1.0, 260.0), target);
    }
  adw_spring_animation_set_value_from (ADW_SPRING_ANIMATION (self->collapse_anim), self->collapse);
  adw_spring_animation_set_value_to (ADW_SPRING_ANIMATION (self->collapse_anim), to);
  adw_animation_play (self->collapse_anim);
}

/* In a capsule (a GlassButtonGroup, the header bar's too): the button's
 * glass is part of the capsule's, and leaves it with the menu. */
static gboolean
in_capsule (GlassMenuButton *self)
{
  GtkWidget *panel = gtk_widget_get_ancestor (GTK_WIDGET (self), GLASS_TYPE_PANEL);

  return panel != NULL && GLASS_IS_BUTTON_GROUP (panel);
}

/* While pinned the popover points at where the button was, whatever the
 * capsule does to us meanwhile. */
static void
pin_popover (GlassMenuButton *self)
{
  GtkWidget *root = GTK_WIDGET (gtk_widget_get_root (GTK_WIDGET (self)));
  graphene_point_t at;

  if (root == NULL ||
      !gtk_widget_compute_point (GTK_WIDGET (self), root, graphene_point_zero (), &at))
    return;
  gtk_popover_set_pointing_to (self->popover,
                               &(GdkRectangle) { (int) lround (self->slot.origin.x - at.x),
                                                 (int) lround (self->slot.origin.y - at.y),
                                                 (int) lround (self->slot.size.width),
                                                 (int) lround (self->slot.size.height) });
}

static void
popover_closed (GtkPopover *popover, GlassMenuButton *self)
{
  gtk_widget_unset_state_flags (self->button, GTK_STATE_FLAG_CHECKED);
  if (self->pinned)
    {
      self->pinned = FALSE;
      gtk_popover_set_pointing_to (popover, NULL);
    }
  /* Back into the capsule. */
  if (self->collapse > 0.0)
    collapse_to (self, 0.0);
}

static void
clicked (GtkButton *button, GlassMenuButton *self)
{
  glass_menu_button_popup (self);
}

/* ── GtkActionable, forwarded to the inner button ── */

static const char *
get_action_name (GtkActionable *actionable)
{
  return gtk_actionable_get_action_name (GTK_ACTIONABLE (GLASS_MENU_BUTTON (actionable)->button));
}

static void
set_action_name (GtkActionable *actionable, const char *name)
{
  gtk_actionable_set_action_name (GTK_ACTIONABLE (GLASS_MENU_BUTTON (actionable)->button), name);
  g_object_notify (G_OBJECT (actionable), "action-name");
}

static GVariant *
get_action_target_value (GtkActionable *actionable)
{
  return gtk_actionable_get_action_target_value (GTK_ACTIONABLE (GLASS_MENU_BUTTON (actionable)->button));
}

static void
set_action_target_value (GtkActionable *actionable, GVariant *target)
{
  gtk_actionable_set_action_target_value (GTK_ACTIONABLE (GLASS_MENU_BUTTON (actionable)->button), target);
  g_object_notify (G_OBJECT (actionable), "action-target");
}

static void
glass_menu_button_actionable_init (GtkActionableInterface *iface)
{
  iface->get_action_name = get_action_name;
  iface->set_action_name = set_action_name;
  iface->get_action_target_value = get_action_target_value;
  iface->set_action_target_value = set_action_target_value;
}

static void
glass_menu_button_size_allocate (GtkWidget *widget, int width, int height, int baseline)
{
  GlassMenuButton *self = GLASS_MENU_BUTTON (widget);
  int natural = width;

  /* Leaving the capsule the button keeps its size, centred and cut off by
   * our narrowing width (overflow: hidden). */
  if (self->collapse > 0.0)
    {
      gtk_widget_measure (self->button, GTK_ORIENTATION_HORIZONTAL, height, NULL, &natural, NULL, NULL);
      natural = MAX (natural, width);
    }
  gtk_widget_allocate (self->button, natural, height, baseline,
                       natural > width ? gsk_transform_translate (NULL, &GRAPHENE_POINT_INIT ((width - natural) / 2.0f, 0))
                                       : NULL);
  if (self->popover)
    {
      if (self->pinned)
        pin_popover (self);
      gtk_popover_present (self->popover);
    }
}

static void
glass_menu_button_measure (GtkWidget *widget, GtkOrientation orientation, int for_size,
                           int *minimum, int *natural, int *minimum_baseline, int *natural_baseline)
{
  GlassMenuButton *self = GLASS_MENU_BUTTON (widget);

  gtk_widget_measure (self->button, orientation, for_size,
                      minimum, natural, minimum_baseline, natural_baseline);
  if (orientation == GTK_ORIENTATION_HORIZONTAL && self->collapse > 0.0)
    {
      *minimum = (int) floor (*minimum * (1.0 - self->collapse));
      *natural = (int) floor (*natural * (1.0 - self->collapse));
    }
}

static void
glass_menu_button_dispose (GObject *object)
{
  GlassMenuButton *self = GLASS_MENU_BUTTON (object);

  glass_menu_button_set_popover (self, NULL);
  g_clear_object (&self->model);
  g_clear_object (&self->collapse_anim);
  g_clear_pointer (&self->button, gtk_widget_unparent);

  G_OBJECT_CLASS (glass_menu_button_parent_class)->dispose (object);
}

static void
glass_menu_button_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  GlassMenuButton *self = GLASS_MENU_BUTTON (object);

  switch (prop_id)
    {
    case PROP_ICON_NAME:
      g_value_set_string (value, glass_menu_button_get_icon_name (self));
      break;
    case PROP_LABEL:
      g_value_set_string (value, glass_menu_button_get_label (self));
      break;
    case PROP_MENU_MODEL:
      g_value_set_object (value, self->model);
      break;
    case PROP_POPOVER:
      g_value_set_object (value, self->popover);
      break;
    case PROP_ACTION_NAME:
      g_value_set_string (value, get_action_name (GTK_ACTIONABLE (self)));
      break;
    case PROP_ACTION_TARGET:
      g_value_set_variant (value, get_action_target_value (GTK_ACTIONABLE (self)));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
glass_menu_button_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
  GlassMenuButton *self = GLASS_MENU_BUTTON (object);

  switch (prop_id)
    {
    case PROP_ICON_NAME:
      glass_menu_button_set_icon_name (self, g_value_get_string (value));
      break;
    case PROP_LABEL:
      glass_menu_button_set_label (self, g_value_get_string (value));
      break;
    case PROP_MENU_MODEL:
      glass_menu_button_set_menu_model (self, g_value_get_object (value));
      break;
    case PROP_POPOVER:
      glass_menu_button_set_popover (self, g_value_get_object (value));
      break;
    case PROP_ACTION_NAME:
      set_action_name (GTK_ACTIONABLE (self), g_value_get_string (value));
      break;
    case PROP_ACTION_TARGET:
      set_action_target_value (GTK_ACTIONABLE (self), g_value_get_variant (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
glass_menu_button_class_init (GlassMenuButtonClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = glass_menu_button_dispose;
  object_class->get_property = glass_menu_button_get_property;
  object_class->set_property = glass_menu_button_set_property;

  widget_class->measure = glass_menu_button_measure;
  widget_class->size_allocate = glass_menu_button_size_allocate;

  /**
   * GlassMenuButton:icon-name:
   *
   * The button's icon.
   */
  props[PROP_ICON_NAME] =
    g_param_spec_string ("icon-name", NULL, NULL, NULL,
                         G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

  /**
   * GlassMenuButton:label:
   *
   * The button's text, instead of the icon.
   */
  props[PROP_LABEL] =
    g_param_spec_string ("label", NULL, NULL, NULL,
                         G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

  /**
   * GlassMenuButton:menu-model:
   *
   * A menu to open, built with [ctor@Popover.new_from_model].
   */
  props[PROP_MENU_MODEL] =
    g_param_spec_object ("menu-model", NULL, NULL, G_TYPE_MENU_MODEL,
                         G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

  /**
   * GlassMenuButton:popover:
   *
   * The popover to open.
   */
  props[PROP_POPOVER] =
    g_param_spec_object ("popover", NULL, NULL, GTK_TYPE_POPOVER,
                         G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, N_PROPS, props);
  g_object_class_override_property (object_class, PROP_ACTION_NAME, "action-name");
  g_object_class_override_property (object_class, PROP_ACTION_TARGET, "action-target");

  gtk_widget_class_set_css_name (widget_class, "glassmenubutton");
}

static void
glass_menu_button_init (GlassMenuButton *self)
{
  glass_ensure_action_muxer (GTK_WIDGET (self));
  self->button = gtk_button_new_from_icon_name ("open-menu-symbolic");
  /* On glass: no background of its own at rest (docs/memo.md 地雷27). */
  gtk_button_set_has_frame (GTK_BUTTON (self->button), FALSE);
  gtk_widget_set_parent (self->button, GTK_WIDGET (self));
  g_signal_connect (self->button, "clicked", G_CALLBACK (clicked), self);
}

/**
 * glass_menu_button_new:
 *
 * Creates a new menu button.
 *
 * Returns: a new menu button
 */
GtkWidget *
glass_menu_button_new (void)
{
  return g_object_new (GLASS_TYPE_MENU_BUTTON, NULL);
}

/**
 * glass_menu_button_get_icon_name:
 * @self: a menu button
 *
 * Gets the icon.
 *
 * Returns: (nullable): the icon
 */
const char *
glass_menu_button_get_icon_name (GlassMenuButton *self)
{
  g_return_val_if_fail (GLASS_IS_MENU_BUTTON (self), NULL);

  return gtk_button_get_icon_name (GTK_BUTTON (self->button));
}

/**
 * glass_menu_button_set_icon_name:
 * @self: a menu button
 * @icon_name: (nullable): the icon
 *
 * Sets the icon.
 */
void
glass_menu_button_set_icon_name (GlassMenuButton *self,
                                 const char      *icon_name)
{
  g_return_if_fail (GLASS_IS_MENU_BUTTON (self));

  gtk_button_set_icon_name (GTK_BUTTON (self->button), icon_name);
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_ICON_NAME]);
}

/**
 * glass_menu_button_get_label:
 * @self: a menu button
 *
 * Gets the text.
 *
 * Returns: (nullable): the text
 */
const char *
glass_menu_button_get_label (GlassMenuButton *self)
{
  g_return_val_if_fail (GLASS_IS_MENU_BUTTON (self), NULL);

  return gtk_button_get_label (GTK_BUTTON (self->button));
}

/**
 * glass_menu_button_set_label:
 * @self: a menu button
 * @label: (nullable): the text
 *
 * Sets the text, instead of the icon.
 */
void
glass_menu_button_set_label (GlassMenuButton *self,
                             const char      *label)
{
  g_return_if_fail (GLASS_IS_MENU_BUTTON (self));

  gtk_button_set_label (GTK_BUTTON (self->button), label);
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_LABEL]);
}

/**
 * glass_menu_button_get_menu_model:
 * @self: a menu button
 *
 * Gets the menu.
 *
 * Returns: (transfer none) (nullable): the menu
 */
GMenuModel *
glass_menu_button_get_menu_model (GlassMenuButton *self)
{
  g_return_val_if_fail (GLASS_IS_MENU_BUTTON (self), NULL);

  return self->model;
}

/**
 * glass_menu_button_set_menu_model:
 * @self: a menu button
 * @model: (nullable): the menu to open
 *
 * Opens a menu built from @model, replacing the popover.
 */
void
glass_menu_button_set_menu_model (GlassMenuButton *self,
                                  GMenuModel      *model)
{
  g_return_if_fail (GLASS_IS_MENU_BUTTON (self));

  if (!g_set_object (&self->model, model))
    return;
  glass_menu_button_set_popover (self, model ? GTK_POPOVER (glass_popover_new_from_model (model)) : NULL);
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_MENU_MODEL]);
}

/**
 * glass_menu_button_get_popover:
 * @self: a menu button
 *
 * Gets the popover.
 *
 * Returns: (transfer none) (nullable): the popover
 */
GtkPopover *
glass_menu_button_get_popover (GlassMenuButton *self)
{
  g_return_val_if_fail (GLASS_IS_MENU_BUTTON (self), NULL);

  return self->popover;
}

/**
 * glass_menu_button_set_popover:
 * @self: a menu button
 * @popover: (nullable): the popover to open, preferably a [class@Popover]
 *
 * Sets the popover.
 */
void
glass_menu_button_set_popover (GlassMenuButton *self,
                               GtkPopover      *popover)
{
  g_return_if_fail (GLASS_IS_MENU_BUTTON (self));

  if (self->popover == popover)
    return;
  if (self->popover)
    {
      g_signal_handlers_disconnect_by_func (self->popover, popover_closed, self);
      gtk_widget_unparent (GTK_WIDGET (self->popover));
    }
  self->popover = popover;
  if (popover)
    {
      gtk_widget_set_parent (GTK_WIDGET (popover), GTK_WIDGET (self));
      g_signal_connect (popover, "closed", G_CALLBACK (popover_closed), self);
    }
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_POPOVER]);
}

/* Menus open below the button, a little apart, like a pull-down menu: one
 * of its edges lines up with the button's, on the side where the menu fits
 * in the window (preferring to grow away from the nearer window edge);
 * centred on the button if it fits neither way. GTK still flips and slides
 * it if the screen is too small. */
#define POPOVER_GAP 10

static void
place_popover (GlassMenuButton *self)
{
  GtkWidget *widget = GTK_WIDGET (self);
  GtkWidget *root = GTK_WIDGET (gtk_widget_get_root (widget));
  graphene_rect_t bounds;
  float room_right, room_left;
  int menu_width = 0;
  gboolean rtl = gtk_widget_get_direction (widget) == GTK_TEXT_DIR_RTL;
  GtkAlign align;

  if (root == NULL || !gtk_widget_compute_bounds (widget, root, &bounds))
    return;
  gtk_widget_measure (GTK_WIDGET (self->popover), GTK_ORIENTATION_HORIZONTAL, -1, NULL, &menu_width, NULL, NULL);

  /* Room for the menu if its left edge is the button's left edge, and if
   * its right edge is the button's right edge. */
  room_right = gtk_widget_get_width (root) - bounds.origin.x;
  room_left = bounds.origin.x + bounds.size.width;
  if (room_right >= menu_width && (room_left < menu_width || room_right >= room_left))
    align = rtl ? GTK_ALIGN_END : GTK_ALIGN_START;   /* grows to the right */
  else if (room_left >= menu_width)
    align = rtl ? GTK_ALIGN_START : GTK_ALIGN_END;   /* grows to the left */
  else
    align = GTK_ALIGN_CENTER;

  gtk_popover_set_position (self->popover, GTK_POS_BOTTOM);
  gtk_widget_set_halign (GTK_WIDGET (self->popover), align);
  gtk_popover_set_offset (self->popover, 0, POPOVER_GAP);
}

/**
 * glass_menu_button_popup:
 * @self: a menu button
 *
 * Opens the popover, below the button. One of its edges lines up with the
 * button's, on the side where the popover fits in the window (towards the
 * middle when it fits both ways); it is centred on the button if it fits
 * neither way.
 */
void
glass_menu_button_popup (GlassMenuButton *self)
{
  g_return_if_fail (GLASS_IS_MENU_BUTTON (self));

  if (self->popover == NULL)
    return;
  place_popover (self);

  /* Out of the button (design.md §6.8): the popover's surface reaches back
   * over the button, where its glass starts, and stays there while the
   * button leaves its capsule. The contents are where they always were,
   * POPOVER_GAP below. With less motion, just the menu. */
  if (GLASS_IS_POPOVER (self->popover))
    {
      GtkWidget *root = GTK_WIDGET (gtk_widget_get_root (GTK_WIDGET (self)));
      GlassPopover *glass = GLASS_POPOVER (self->popover);

      if (root && !glass_motion_reduced (GTK_WIDGET (self)) &&
          gtk_widget_compute_bounds (self->button, root, &self->slot))
        {
          int reach = (int) ceil (self->slot.size.height);

          glass_popover_set_origin (glass, &self->slot, MIN (self->slot.size.width, self->slot.size.height) / 2.0);
          glass_popover_set_reach (glass, reach + POPOVER_GAP);
          gtk_popover_set_offset (self->popover, 0, -reach);
          self->pinned = TRUE;
          pin_popover (self);
          if (in_capsule (self))
            collapse_to (self, 1.0);
        }
      else
        {
          glass_popover_set_origin (glass, NULL, 0.0);
          glass_popover_set_reach (glass, 0);
        }
    }

  gtk_widget_set_state_flags (self->button, GTK_STATE_FLAG_CHECKED, FALSE);
  gtk_popover_popup (self->popover);
}

/**
 * glass_menu_button_popdown:
 * @self: a menu button
 *
 * Closes the popover.
 */
void
glass_menu_button_popdown (GlassMenuButton *self)
{
  g_return_if_fail (GLASS_IS_MENU_BUTTON (self));

  if (self->popover == NULL)
    return;
  /* Back into the button first. */
  if (GLASS_IS_POPOVER (self->popover))
    glass_popover_dismiss (GLASS_POPOVER (self->popover));
  else
    gtk_popover_popdown (self->popover);
}
