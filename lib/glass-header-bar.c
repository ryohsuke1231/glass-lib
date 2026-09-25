/* glass-header-bar.c — a title bar whose buttons float on glass
 * (design.md §6.6).
 *
 * The bar itself has no background: it floats over the content of a
 * GlassToolbarView, over the scroll edge effect. The buttons packed at each
 * end share one glass capsule; the window controls get a small capsule of
 * their own so they read over any content. The title is plain text.
 *
 * SPDX-License-Identifier: MIT
 */
#include "glass-private.h"

/**
 * GlassHeaderBar:
 *
 * The glass counterpart of `AdwHeaderBar`, for the top of a
 * [class@ToolbarView].
 *
 * Widgets packed at the start share one glass capsule, as do the ones
 * packed at the end. The window's title (or the title widget) sits between
 * them without glass. The empty parts of the bar move the window when
 * dragged.
 *
 * ## CSS nodes
 *
 * `GlassHeaderBar` has a CSS node with name `glassheaderbar`. The capsules
 * are [class@Panel]s with the style class `.header-capsule`; the window
 * controls' capsules also have `.window-controls`.
 */

struct _GlassHeaderBar {
  GtkWidget  parent_instance;

  GtkWidget *handle;
  GtkWidget *center_box;
  GtkWidget *start_capsule;         /* GlassButtonGroup */
  GtkWidget *end_capsule;
  GtkWidget *start_controls_capsule, *start_controls;
  GtkWidget *end_controls_capsule, *end_controls;
  GtkWidget *title_label;
  GtkWidget *title_widget;
  gboolean   show_title;
  gboolean   show_start_title_buttons;
  gboolean   show_end_title_buttons;
  GtkWindow *window;                 /* weak: whose title we show */
};

enum {
  PROP_0,
  PROP_TITLE_WIDGET,
  PROP_SHOW_TITLE,
  PROP_SHOW_START_TITLE_BUTTONS,
  PROP_SHOW_END_TITLE_BUTTONS,
  PROP_DECORATION_LAYOUT,
  N_PROPS
};

static GParamSpec *props[N_PROPS];

static void glass_header_bar_buildable_init (GtkBuildableIface *iface);

G_DEFINE_FINAL_TYPE_WITH_CODE (GlassHeaderBar, glass_header_bar, GTK_TYPE_WIDGET,
                               G_IMPLEMENT_INTERFACE (GTK_TYPE_BUILDABLE, glass_header_bar_buildable_init))

static GtkBuildableIface *parent_buildable_iface;

/* A capsule shows while something in it does: a lone button that is hidden
 * (a "show sidebar" button while the sidebar shows) would otherwise leave
 * an empty dot of glass. */
static void
update_visibility (GlassHeaderBar *self)
{
  gtk_widget_set_visible (self->start_capsule,
                          glass_button_group_has_visible_child (GLASS_BUTTON_GROUP (self->start_capsule)));
  gtk_widget_set_visible (self->end_capsule,
                          glass_button_group_has_visible_child (GLASS_BUTTON_GROUP (self->end_capsule)));
  gtk_widget_set_visible (self->start_controls_capsule,
                          self->show_start_title_buttons &&
                          !gtk_window_controls_get_empty (GTK_WINDOW_CONTROLS (self->start_controls)));
  gtk_widget_set_visible (self->end_controls_capsule,
                          self->show_end_title_buttons &&
                          !gtk_window_controls_get_empty (GTK_WINDOW_CONTROLS (self->end_controls)));
  gtk_widget_set_visible (self->title_label, self->show_title && self->title_widget == NULL);
  if (self->title_widget)
    gtk_widget_set_visible (self->title_widget, self->show_title);
}

static void
child_visible_changed (GtkWidget      *child,
                       GParamSpec     *pspec,
                       GlassHeaderBar *self)
{
  update_visibility (self);
}

static void
controls_empty_changed (GObject        *controls,
                        GParamSpec     *pspec,
                        GlassHeaderBar *self)
{
  update_visibility (self);
}

static void
window_title_changed (GtkWindow      *window,
                      GParamSpec     *pspec,
                      GlassHeaderBar *self)
{
  const char *title = gtk_window_get_title (window);

  gtk_label_set_label (GTK_LABEL (self->title_label), title ? title : "");
}

static void
glass_header_bar_root (GtkWidget *widget)
{
  GlassHeaderBar *self = GLASS_HEADER_BAR (widget);
  GtkRoot *root;

  GTK_WIDGET_CLASS (glass_header_bar_parent_class)->root (widget);

  root = gtk_widget_get_root (widget);
  if (GTK_IS_WINDOW (root))
    {
      self->window = GTK_WINDOW (root);
      g_signal_connect_object (root, "notify::title", G_CALLBACK (window_title_changed), self, 0);
      window_title_changed (self->window, NULL, self);
    }
}

static void
glass_header_bar_unroot (GtkWidget *widget)
{
  GlassHeaderBar *self = GLASS_HEADER_BAR (widget);

  if (self->window)
    g_signal_handlers_disconnect_by_func (self->window, window_title_changed, self);
  self->window = NULL;

  GTK_WIDGET_CLASS (glass_header_bar_parent_class)->unroot (widget);
}

static GtkWidget *
capsule_new (void)
{
  GtkWidget *capsule = glass_button_group_new ();

  gtk_widget_add_css_class (capsule, "header-capsule");
  gtk_widget_set_valign (capsule, GTK_ALIGN_CENTER);
  return capsule;
}

static GtkWidget *
controls_capsule_new (GtkWidget *controls)
{
  GtkWidget *capsule = glass_panel_new ();

  glass_panel_set_child (GLASS_PANEL (capsule), controls);
  gtk_widget_add_css_class (capsule, "header-capsule");
  gtk_widget_add_css_class (capsule, "window-controls");
  gtk_widget_set_valign (capsule, GTK_ALIGN_CENTER);
  return capsule;
}

static void
glass_header_bar_dispose (GObject *object)
{
  GlassHeaderBar *self = GLASS_HEADER_BAR (object);

  g_clear_pointer (&self->handle, gtk_widget_unparent);
  g_clear_object (&self->title_label);

  G_OBJECT_CLASS (glass_header_bar_parent_class)->dispose (object);
}

static void
glass_header_bar_get_property (GObject    *object,
                               guint       prop_id,
                               GValue     *value,
                               GParamSpec *pspec)
{
  GlassHeaderBar *self = GLASS_HEADER_BAR (object);

  switch (prop_id)
    {
    case PROP_TITLE_WIDGET:
      g_value_set_object (value, self->title_widget);
      break;
    case PROP_SHOW_TITLE:
      g_value_set_boolean (value, self->show_title);
      break;
    case PROP_SHOW_START_TITLE_BUTTONS:
      g_value_set_boolean (value, self->show_start_title_buttons);
      break;
    case PROP_SHOW_END_TITLE_BUTTONS:
      g_value_set_boolean (value, self->show_end_title_buttons);
      break;
    case PROP_DECORATION_LAYOUT:
      g_value_set_string (value, glass_header_bar_get_decoration_layout (self));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
glass_header_bar_set_property (GObject      *object,
                               guint         prop_id,
                               const GValue *value,
                               GParamSpec   *pspec)
{
  GlassHeaderBar *self = GLASS_HEADER_BAR (object);

  switch (prop_id)
    {
    case PROP_TITLE_WIDGET:
      glass_header_bar_set_title_widget (self, g_value_get_object (value));
      break;
    case PROP_SHOW_TITLE:
      glass_header_bar_set_show_title (self, g_value_get_boolean (value));
      break;
    case PROP_SHOW_START_TITLE_BUTTONS:
      glass_header_bar_set_show_start_title_buttons (self, g_value_get_boolean (value));
      break;
    case PROP_SHOW_END_TITLE_BUTTONS:
      glass_header_bar_set_show_end_title_buttons (self, g_value_get_boolean (value));
      break;
    case PROP_DECORATION_LAYOUT:
      glass_header_bar_set_decoration_layout (self, g_value_get_string (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
glass_header_bar_class_init (GlassHeaderBarClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = glass_header_bar_dispose;
  object_class->get_property = glass_header_bar_get_property;
  object_class->set_property = glass_header_bar_set_property;

  widget_class->root = glass_header_bar_root;
  widget_class->unroot = glass_header_bar_unroot;

  /**
   * GlassHeaderBar:title-widget:
   *
   * Shown in the middle instead of the window's title.
   */
  props[PROP_TITLE_WIDGET] =
    g_param_spec_object ("title-widget", NULL, NULL, GTK_TYPE_WIDGET,
                         G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

  /**
   * GlassHeaderBar:show-title:
   *
   * Whether the title (or the title widget) is shown.
   */
  props[PROP_SHOW_TITLE] =
    g_param_spec_boolean ("show-title", NULL, NULL, TRUE,
                          G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

  /**
   * GlassHeaderBar:show-start-title-buttons:
   *
   * Whether the window controls on the start side are shown.
   */
  props[PROP_SHOW_START_TITLE_BUTTONS] =
    g_param_spec_boolean ("show-start-title-buttons", NULL, NULL, TRUE,
                          G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

  /**
   * GlassHeaderBar:show-end-title-buttons:
   *
   * Whether the window controls on the end side are shown.
   */
  props[PROP_SHOW_END_TITLE_BUTTONS] =
    g_param_spec_boolean ("show-end-title-buttons", NULL, NULL, TRUE,
                          G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

  /**
   * GlassHeaderBar:decoration-layout:
   *
   * The window controls' layout (`GtkWindowControls:decoration-layout`);
   * %NULL follows the desktop setting.
   */
  props[PROP_DECORATION_LAYOUT] =
    g_param_spec_string ("decoration-layout", NULL, NULL, NULL,
                         G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, N_PROPS, props);

  gtk_widget_class_set_layout_manager_type (widget_class, GTK_TYPE_BIN_LAYOUT);
  gtk_widget_class_set_css_name (widget_class, "glassheaderbar");
  gtk_widget_class_set_accessible_role (widget_class, GTK_ACCESSIBLE_ROLE_GROUP);
}

static void
glass_header_bar_init (GlassHeaderBar *self)
{
  GtkWidget *start, *end;

  self->show_title = TRUE;
  self->show_start_title_buttons = TRUE;
  self->show_end_title_buttons = TRUE;

  self->handle = gtk_window_handle_new ();
  gtk_widget_set_parent (self->handle, GTK_WIDGET (self));

  self->center_box = gtk_center_box_new ();
  gtk_window_handle_set_child (GTK_WINDOW_HANDLE (self->handle), self->center_box);

  start = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
  end = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
  gtk_center_box_set_start_widget (GTK_CENTER_BOX (self->center_box), start);
  gtk_center_box_set_end_widget (GTK_CENTER_BOX (self->center_box), end);

  self->start_controls = gtk_window_controls_new (GTK_PACK_START);
  self->start_controls_capsule = controls_capsule_new (self->start_controls);
  self->start_capsule = capsule_new ();
  gtk_box_append (GTK_BOX (start), self->start_controls_capsule);
  gtk_box_append (GTK_BOX (start), self->start_capsule);

  self->end_capsule = capsule_new ();
  self->end_controls = gtk_window_controls_new (GTK_PACK_END);
  self->end_controls_capsule = controls_capsule_new (self->end_controls);
  gtk_box_append (GTK_BOX (end), self->end_capsule);
  gtk_box_append (GTK_BOX (end), self->end_controls_capsule);

  /* Kept alive while a title widget takes its place. */
  self->title_label = g_object_ref_sink (gtk_label_new (NULL));
  gtk_widget_add_css_class (self->title_label, "title");
  gtk_label_set_ellipsize (GTK_LABEL (self->title_label), PANGO_ELLIPSIZE_END);
  gtk_label_set_single_line_mode (GTK_LABEL (self->title_label), TRUE);
  gtk_label_set_width_chars (GTK_LABEL (self->title_label), 5);
  gtk_center_box_set_center_widget (GTK_CENTER_BOX (self->center_box), self->title_label);

  g_signal_connect_object (self->start_controls, "notify::empty",
                           G_CALLBACK (controls_empty_changed), self, 0);
  g_signal_connect_object (self->end_controls, "notify::empty",
                           G_CALLBACK (controls_empty_changed), self, 0);

  update_visibility (self);
}

static void
glass_header_bar_buildable_add_child (GtkBuildable *buildable,
                                      GtkBuilder   *builder,
                                      GObject      *child,
                                      const char   *type)
{
  GlassHeaderBar *self = GLASS_HEADER_BAR (buildable);

  if (!GTK_IS_WIDGET (child))
    parent_buildable_iface->add_child (buildable, builder, child, type);
  else if (g_strcmp0 (type, "title") == 0)
    glass_header_bar_set_title_widget (self, GTK_WIDGET (child));
  else if (g_strcmp0 (type, "end") == 0)
    glass_header_bar_pack_end (self, GTK_WIDGET (child));
  else if (type == NULL || g_strcmp0 (type, "start") == 0)
    glass_header_bar_pack_start (self, GTK_WIDGET (child));
  else
    parent_buildable_iface->add_child (buildable, builder, child, type);
}

static void
glass_header_bar_buildable_init (GtkBuildableIface *iface)
{
  parent_buildable_iface = g_type_interface_peek_parent (iface);
  iface->add_child = glass_header_bar_buildable_add_child;
}

/**
 * glass_header_bar_new:
 *
 * Returns: a new header bar
 */
GtkWidget *
glass_header_bar_new (void)
{
  return g_object_new (GLASS_TYPE_HEADER_BAR, NULL);
}

/**
 * glass_header_bar_pack_start:
 * @self: a header bar
 * @child: a widget, usually a button
 *
 * Adds @child to the capsule at the start, after the ones added before.
 */
void
glass_header_bar_pack_start (GlassHeaderBar *self,
                             GtkWidget      *child)
{
  g_return_if_fail (GLASS_IS_HEADER_BAR (self));
  g_return_if_fail (GTK_IS_WIDGET (child));

  glass_button_group_append (GLASS_BUTTON_GROUP (self->start_capsule), child);
  g_signal_connect_object (child, "notify::visible", G_CALLBACK (child_visible_changed), self, 0);
  update_visibility (self);
}

/**
 * glass_header_bar_pack_end:
 * @self: a header bar
 * @child: a widget, usually a button
 *
 * Adds @child to the capsule at the end, before (to the left of) the ones
 * added before, like `gtk_header_bar_pack_end()`.
 */
void
glass_header_bar_pack_end (GlassHeaderBar *self,
                           GtkWidget      *child)
{
  g_return_if_fail (GLASS_IS_HEADER_BAR (self));
  g_return_if_fail (GTK_IS_WIDGET (child));

  glass_button_group_prepend (GLASS_BUTTON_GROUP (self->end_capsule), child);
  g_signal_connect_object (child, "notify::visible", G_CALLBACK (child_visible_changed), self, 0);
  update_visibility (self);
}

/**
 * glass_header_bar_remove:
 * @self: a header bar
 * @child: a packed widget
 *
 * Removes @child.
 */
void
glass_header_bar_remove (GlassHeaderBar *self,
                         GtkWidget      *child)
{
  GtkWidget *parent;

  g_return_if_fail (GLASS_IS_HEADER_BAR (self));

  parent = gtk_widget_get_parent (child);
  parent = parent ? gtk_widget_get_parent (parent) : NULL;   /* the group, above its row */
  if (parent == self->start_capsule || parent == self->end_capsule)
    {
      g_signal_handlers_disconnect_by_func (child, child_visible_changed, self);
      glass_button_group_remove (GLASS_BUTTON_GROUP (parent), child);
    }
  else if (child == self->title_widget)
    glass_header_bar_set_title_widget (self, NULL);
  else
    g_critical ("glass_header_bar_remove: not packed in this header bar");
  update_visibility (self);
}

/**
 * glass_header_bar_get_title_widget:
 * @self: a header bar
 *
 * Returns: (transfer none) (nullable): the title widget
 */
GtkWidget *
glass_header_bar_get_title_widget (GlassHeaderBar *self)
{
  g_return_val_if_fail (GLASS_IS_HEADER_BAR (self), NULL);

  return self->title_widget;
}

/**
 * glass_header_bar_set_title_widget:
 * @self: a header bar
 * @title_widget: (nullable): shown instead of the window's title
 *
 * Sets the title widget.
 */
void
glass_header_bar_set_title_widget (GlassHeaderBar *self,
                                   GtkWidget      *title_widget)
{
  g_return_if_fail (GLASS_IS_HEADER_BAR (self));

  if (self->title_widget == title_widget)
    return;
  self->title_widget = title_widget;
  gtk_center_box_set_center_widget (GTK_CENTER_BOX (self->center_box),
                                    title_widget ? title_widget : self->title_label);
  update_visibility (self);
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_TITLE_WIDGET]);
}

/**
 * glass_header_bar_get_show_title:
 * @self: a header bar
 *
 * Returns: whether the title is shown
 */
gboolean
glass_header_bar_get_show_title (GlassHeaderBar *self)
{
  g_return_val_if_fail (GLASS_IS_HEADER_BAR (self), TRUE);

  return self->show_title;
}

/**
 * glass_header_bar_set_show_title:
 * @self: a header bar
 * @show_title: whether to show the title
 *
 * Sets whether the title is shown.
 */
void
glass_header_bar_set_show_title (GlassHeaderBar *self,
                                 gboolean        show_title)
{
  g_return_if_fail (GLASS_IS_HEADER_BAR (self));

  show_title = !!show_title;
  if (self->show_title == show_title)
    return;
  self->show_title = show_title;
  update_visibility (self);
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_SHOW_TITLE]);
}

/**
 * glass_header_bar_get_show_start_title_buttons:
 * @self: a header bar
 *
 * Returns: whether the start window controls are shown
 */
gboolean
glass_header_bar_get_show_start_title_buttons (GlassHeaderBar *self)
{
  g_return_val_if_fail (GLASS_IS_HEADER_BAR (self), TRUE);

  return self->show_start_title_buttons;
}

/**
 * glass_header_bar_set_show_start_title_buttons:
 * @self: a header bar
 * @setting: whether to show them
 *
 * Sets whether the window controls on the start side are shown.
 */
void
glass_header_bar_set_show_start_title_buttons (GlassHeaderBar *self,
                                               gboolean        setting)
{
  g_return_if_fail (GLASS_IS_HEADER_BAR (self));

  setting = !!setting;
  if (self->show_start_title_buttons == setting)
    return;
  self->show_start_title_buttons = setting;
  update_visibility (self);
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_SHOW_START_TITLE_BUTTONS]);
}

/**
 * glass_header_bar_get_show_end_title_buttons:
 * @self: a header bar
 *
 * Returns: whether the end window controls are shown
 */
gboolean
glass_header_bar_get_show_end_title_buttons (GlassHeaderBar *self)
{
  g_return_val_if_fail (GLASS_IS_HEADER_BAR (self), TRUE);

  return self->show_end_title_buttons;
}

/**
 * glass_header_bar_set_show_end_title_buttons:
 * @self: a header bar
 * @setting: whether to show them
 *
 * Sets whether the window controls on the end side are shown.
 */
void
glass_header_bar_set_show_end_title_buttons (GlassHeaderBar *self,
                                             gboolean        setting)
{
  g_return_if_fail (GLASS_IS_HEADER_BAR (self));

  setting = !!setting;
  if (self->show_end_title_buttons == setting)
    return;
  self->show_end_title_buttons = setting;
  update_visibility (self);
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_SHOW_END_TITLE_BUTTONS]);
}

/**
 * glass_header_bar_get_decoration_layout:
 * @self: a header bar
 *
 * Returns: (nullable): the window controls' layout
 */
const char *
glass_header_bar_get_decoration_layout (GlassHeaderBar *self)
{
  g_return_val_if_fail (GLASS_IS_HEADER_BAR (self), NULL);

  return gtk_window_controls_get_decoration_layout (GTK_WINDOW_CONTROLS (self->start_controls));
}

/**
 * glass_header_bar_set_decoration_layout:
 * @self: a header bar
 * @layout: (nullable): the layout, as in `GtkWindowControls`
 *
 * Sets the window controls' layout.
 */
void
glass_header_bar_set_decoration_layout (GlassHeaderBar *self,
                                        const char     *layout)
{
  g_return_if_fail (GLASS_IS_HEADER_BAR (self));

  gtk_window_controls_set_decoration_layout (GTK_WINDOW_CONTROLS (self->start_controls), layout);
  gtk_window_controls_set_decoration_layout (GTK_WINDOW_CONTROLS (self->end_controls), layout);
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_DECORATION_LAYOUT]);
}
