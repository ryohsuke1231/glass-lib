/* glass-menu-button.c — a button that opens a glass popover or menu
 * (design.md §6.7).
 *
 * GtkMenuButton cannot be subclassed and opens a GtkPopoverMenu, which
 * cannot be glass; this is the same thing with a GlassPopover.
 *
 * SPDX-License-Identifier: MIT
 */
#include "glass-private.h"

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
 */

struct _GlassMenuButton {
  GtkWidget   parent_instance;

  GtkWidget  *button;
  GtkPopover *popover;
  GMenuModel *model;
};

enum {
  PROP_0,
  PROP_ICON_NAME,
  PROP_LABEL,
  PROP_MENU_MODEL,
  PROP_POPOVER,
  N_PROPS
};

static GParamSpec *props[N_PROPS];

G_DEFINE_FINAL_TYPE (GlassMenuButton, glass_menu_button, GTK_TYPE_WIDGET)

static void
popover_closed (GtkPopover *popover, GlassMenuButton *self)
{
  gtk_widget_unset_state_flags (self->button, GTK_STATE_FLAG_CHECKED);
}

static void
clicked (GtkButton *button, GlassMenuButton *self)
{
  glass_menu_button_popup (self);
}

static void
glass_menu_button_size_allocate (GtkWidget *widget, int width, int height, int baseline)
{
  GlassMenuButton *self = GLASS_MENU_BUTTON (widget);

  gtk_widget_allocate (self->button, width, height, baseline, NULL);
  if (self->popover)
    gtk_popover_present (self->popover);
}

static void
glass_menu_button_measure (GtkWidget *widget, GtkOrientation orientation, int for_size,
                           int *minimum, int *natural, int *minimum_baseline, int *natural_baseline)
{
  gtk_widget_measure (GLASS_MENU_BUTTON (widget)->button, orientation, for_size,
                      minimum, natural, minimum_baseline, natural_baseline);
}

static void
glass_menu_button_dispose (GObject *object)
{
  GlassMenuButton *self = GLASS_MENU_BUTTON (object);

  glass_menu_button_set_popover (self, NULL);
  g_clear_object (&self->model);
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

  gtk_widget_class_set_css_name (widget_class, "glassmenubutton");
}

static void
glass_menu_button_init (GlassMenuButton *self)
{
  self->button = gtk_button_new_from_icon_name ("open-menu-symbolic");
  /* On glass: no background of its own at rest (docs/memo.md 地雷27). */
  gtk_button_set_has_frame (GTK_BUTTON (self->button), FALSE);
  gtk_widget_set_parent (self->button, GTK_WIDGET (self));
  g_signal_connect (self->button, "clicked", G_CALLBACK (clicked), self);
}

/**
 * glass_menu_button_new:
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

/**
 * glass_menu_button_popup:
 * @self: a menu button
 *
 * Opens the popover.
 */
void
glass_menu_button_popup (GlassMenuButton *self)
{
  g_return_if_fail (GLASS_IS_MENU_BUTTON (self));

  if (self->popover == NULL)
    return;
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

  if (self->popover)
    gtk_popover_popdown (self->popover);
}
