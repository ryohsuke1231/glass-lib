/* glass-button.c — a button that is itself glass (design.md §6.6).
 *
 * A capsule of glass holding a flat GtkButton. While it is pressed the glass
 * swells and lights up (GlassPanel:interactive).
 *
 * SPDX-License-Identifier: MIT
 */
#include "glass-private.h"

#include <adwaita.h>

/**
 * GlassButton:
 *
 * A button that is a small pane of glass: round with an icon, a capsule
 * with a label. While it is pressed the glass lights up.
 *
 * ## CSS nodes
 *
 * `GlassButton` is a [class@Panel] (CSS name `glasspanel`) with the style
 * class `.glass-button`, holding a `button`.
 */

struct _GlassButton {
  GlassPanel    parent_instance;

  GtkWidget    *button;
};

enum {
  PROP_0,
  PROP_ICON_NAME,
  PROP_LABEL,
  N_PROPS,
  /* GtkActionable */
  PROP_ACTION_NAME = N_PROPS,
  PROP_ACTION_TARGET,
};

enum {
  CLICKED,
  N_SIGNALS
};

static GParamSpec *props[N_PROPS];
static guint signals[N_SIGNALS];

static void glass_button_actionable_init (GtkActionableInterface *iface);

G_DEFINE_FINAL_TYPE_WITH_CODE (GlassButton, glass_button, GLASS_TYPE_PANEL,
                               G_IMPLEMENT_INTERFACE (GTK_TYPE_ACTIONABLE, glass_button_actionable_init))

static void
clicked (GtkButton *button, GlassButton *self)
{
  g_signal_emit (self, signals[CLICKED], 0);
}

static void
update_shape (GlassButton *self)
{
  gboolean text = gtk_button_get_label (GTK_BUTTON (self->button)) != NULL;

  if (text)
    {
      gtk_widget_add_css_class (self->button, "text-button");
      gtk_widget_remove_css_class (self->button, "image-button");
    }
  else
    {
      gtk_widget_add_css_class (self->button, "image-button");
      gtk_widget_remove_css_class (self->button, "text-button");
    }
}

/* ── GtkActionable, forwarded to the inner button ── */

static const char *
get_action_name (GtkActionable *actionable)
{
  return gtk_actionable_get_action_name (GTK_ACTIONABLE (GLASS_BUTTON (actionable)->button));
}

static void
set_action_name (GtkActionable *actionable, const char *name)
{
  gtk_actionable_set_action_name (GTK_ACTIONABLE (GLASS_BUTTON (actionable)->button), name);
  g_object_notify (G_OBJECT (actionable), "action-name");
}

static GVariant *
get_action_target_value (GtkActionable *actionable)
{
  return gtk_actionable_get_action_target_value (GTK_ACTIONABLE (GLASS_BUTTON (actionable)->button));
}

static void
set_action_target_value (GtkActionable *actionable, GVariant *target)
{
  gtk_actionable_set_action_target_value (GTK_ACTIONABLE (GLASS_BUTTON (actionable)->button), target);
  g_object_notify (G_OBJECT (actionable), "action-target");
}

static void
glass_button_actionable_init (GtkActionableInterface *iface)
{
  iface->get_action_name = get_action_name;
  iface->set_action_name = set_action_name;
  iface->get_action_target_value = get_action_target_value;
  iface->set_action_target_value = set_action_target_value;
}

static void
glass_button_get_property (GObject    *object,
                           guint       prop_id,
                           GValue     *value,
                           GParamSpec *pspec)
{
  GlassButton *self = GLASS_BUTTON (object);

  switch (prop_id)
    {
    case PROP_ICON_NAME:
      g_value_set_string (value, glass_button_get_icon_name (self));
      break;
    case PROP_LABEL:
      g_value_set_string (value, glass_button_get_label (self));
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
glass_button_set_property (GObject      *object,
                           guint         prop_id,
                           const GValue *value,
                           GParamSpec   *pspec)
{
  GlassButton *self = GLASS_BUTTON (object);

  switch (prop_id)
    {
    case PROP_ICON_NAME:
      glass_button_set_icon_name (self, g_value_get_string (value));
      break;
    case PROP_LABEL:
      glass_button_set_label (self, g_value_get_string (value));
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
glass_button_class_init (GlassButtonClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = glass_button_get_property;
  object_class->set_property = glass_button_set_property;

  /**
   * GlassButton:icon-name:
   *
   * The icon.
   */
  props[PROP_ICON_NAME] =
    g_param_spec_string ("icon-name", NULL, NULL, NULL,
                         G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

  /**
   * GlassButton:label:
   *
   * The text; it takes the place of the icon.
   */
  props[PROP_LABEL] =
    g_param_spec_string ("label", NULL, NULL, NULL,
                         G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, N_PROPS, props);
  g_object_class_override_property (object_class, PROP_ACTION_NAME, "action-name");
  g_object_class_override_property (object_class, PROP_ACTION_TARGET, "action-target");

  /**
   * GlassButton::clicked:
   *
   * Emitted when the button is clicked.
   */
  signals[CLICKED] = g_signal_new ("clicked", G_TYPE_FROM_CLASS (klass),
                                   G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
                                   G_TYPE_NONE, 0);

  gtk_widget_class_set_accessible_role (GTK_WIDGET_CLASS (klass), GTK_ACCESSIBLE_ROLE_GENERIC);
}

static void
glass_button_init (GlassButton *self)
{
  glass_ensure_action_muxer (GTK_WIDGET (self));
  self->button = gtk_button_new ();
  gtk_widget_add_css_class (self->button, "flat");
  glass_panel_set_child (GLASS_PANEL (self), self->button);
  glass_panel_add_own_class (GLASS_PANEL (self), "glass-button");
  update_shape (self);

  g_signal_connect_object (self->button, "clicked", G_CALLBACK (clicked), self, 0);

  /* Pressed, the glass swells and lights up (GlassPanel:interactive). */
  glass_panel_set_interactive (GLASS_PANEL (self), TRUE);
}

/**
 * glass_button_new_from_icon_name:
 * @icon_name: the icon
 *
 * Creates a new round glass button.
 *
 * Returns: a new round glass button
 */
GtkWidget *
glass_button_new_from_icon_name (const char *icon_name)
{
  return g_object_new (GLASS_TYPE_BUTTON, "icon-name", icon_name, NULL);
}

/**
 * glass_button_new_with_label:
 * @label: the text
 *
 * Creates a new glass capsule button.
 *
 * Returns: a new glass capsule button
 */
GtkWidget *
glass_button_new_with_label (const char *label)
{
  return g_object_new (GLASS_TYPE_BUTTON, "label", label, NULL);
}

/**
 * glass_button_get_icon_name:
 * @self: a button
 *
 * Gets the icon.
 *
 * Returns: (nullable): the icon
 */
const char *
glass_button_get_icon_name (GlassButton *self)
{
  g_return_val_if_fail (GLASS_IS_BUTTON (self), NULL);

  return gtk_button_get_icon_name (GTK_BUTTON (self->button));
}

/**
 * glass_button_set_icon_name:
 * @self: a button
 * @icon_name: (nullable): the icon
 *
 * Sets the icon.
 */
void
glass_button_set_icon_name (GlassButton *self,
                            const char  *icon_name)
{
  g_return_if_fail (GLASS_IS_BUTTON (self));

  if (g_strcmp0 (icon_name, glass_button_get_icon_name (self)) == 0)
    return;
  gtk_button_set_icon_name (GTK_BUTTON (self->button), icon_name);
  update_shape (self);
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_ICON_NAME]);
}

/**
 * glass_button_get_label:
 * @self: a button
 *
 * Gets the text.
 *
 * Returns: (nullable): the text
 */
const char *
glass_button_get_label (GlassButton *self)
{
  g_return_val_if_fail (GLASS_IS_BUTTON (self), NULL);

  return gtk_button_get_label (GTK_BUTTON (self->button));
}

/**
 * glass_button_set_label:
 * @self: a button
 * @label: (nullable): the text
 *
 * Sets the text.
 */
void
glass_button_set_label (GlassButton *self,
                        const char  *label)
{
  g_return_if_fail (GLASS_IS_BUTTON (self));

  if (g_strcmp0 (label, glass_button_get_label (self)) == 0)
    return;
  gtk_button_set_label (GTK_BUTTON (self->button), label);
  update_shape (self);
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_LABEL]);
}
