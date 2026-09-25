/* glass-button-group.c — a row of buttons on one capsule of glass
 * (design.md §6.6).
 *
 * SPDX-License-Identifier: MIT
 */
#include "glass-private.h"

/**
 * GlassButtonGroup:
 *
 * A row of buttons (or other small widgets) sharing one capsule of glass,
 * like the item groups of a toolbar. [class@HeaderBar] packs its buttons in
 * these.
 *
 * Each item is drawn inside a pill (a circle for a square item), so its
 * hover and pressed backgrounds are round whatever the theme's corner
 * radius. Buttons and menu buttons lose their frame while in the group
 * (`has-frame` is %FALSE, as in a header bar), so the only background at
 * rest is the glass, whatever the theme; suggested, destructive and
 * `.opaque` buttons keep theirs. Prefer it over a [class@Panel] holding a
 * plain `GtkBox` of buttons.
 *
 * ## CSS nodes
 *
 * `GlassButtonGroup` is a [class@Panel] (CSS name `glasspanel`) with the
 * style class `.button-group`, holding a `box.pills`.
 */

struct _GlassButtonGroup {
  GlassPanel  parent_instance;

  GtkWidget  *row;
};

G_DEFINE_FINAL_TYPE (GlassButtonGroup, glass_button_group, GLASS_TYPE_PANEL)

/* A theme can paint every framed button, from USER priority, above the
 * library's `background: none` (docs/memo.md 地雷27): a faint pill under
 * each item at rest. Flat buttons it leaves alone. */
#define UNFRAMED_KEY "glass-button-group-unframed"

static gboolean
keeps_frame (GtkWidget *child)
{
  return gtk_widget_has_css_class (child, "suggested-action") ||
         gtk_widget_has_css_class (child, "destructive-action") ||
         gtk_widget_has_css_class (child, "opaque");
}

static void
unframe (GtkWidget *child)
{
  if (keeps_frame (child))
    return;
  if (GTK_IS_BUTTON (child) && gtk_button_get_has_frame (GTK_BUTTON (child)))
    gtk_button_set_has_frame (GTK_BUTTON (child), FALSE);
  else if (GTK_IS_MENU_BUTTON (child) && gtk_menu_button_get_has_frame (GTK_MENU_BUTTON (child)))
    gtk_menu_button_set_has_frame (GTK_MENU_BUTTON (child), FALSE);
  else
    return;
  g_object_set_data (G_OBJECT (child), UNFRAMED_KEY, GINT_TO_POINTER (TRUE));
}

static void
reframe (GtkWidget *child)
{
  if (!g_object_get_data (G_OBJECT (child), UNFRAMED_KEY))
    return;
  g_object_set_data (G_OBJECT (child), UNFRAMED_KEY, NULL);
  if (GTK_IS_BUTTON (child))
    gtk_button_set_has_frame (GTK_BUTTON (child), TRUE);
  else if (GTK_IS_MENU_BUTTON (child))
    gtk_menu_button_set_has_frame (GTK_MENU_BUTTON (child), TRUE);
}

static void
glass_button_group_class_init (GlassButtonGroupClass *klass)
{
}

static void
glass_button_group_init (GlassButtonGroup *self)
{
  self->row = glass_pill_box_new ();
  glass_panel_set_child (GLASS_PANEL (self), self->row);
  gtk_widget_add_css_class (GTK_WIDGET (self), "button-group");
  glass_panel_set_interactive (GLASS_PANEL (self), TRUE);
}

/**
 * glass_button_group_new:
 *
 * Returns: a new, empty button group
 */
GtkWidget *
glass_button_group_new (void)
{
  return g_object_new (GLASS_TYPE_BUTTON_GROUP, NULL);
}

/**
 * glass_button_group_append:
 * @self: a button group
 * @child: a widget, usually a button
 *
 * Adds @child at the end.
 */
void
glass_button_group_append (GlassButtonGroup *self,
                           GtkWidget        *child)
{
  g_return_if_fail (GLASS_IS_BUTTON_GROUP (self));
  g_return_if_fail (GTK_IS_WIDGET (child));

  unframe (child);
  glass_pill_box_append (GLASS_PILL_BOX (self->row), child);
}

/**
 * glass_button_group_prepend:
 * @self: a button group
 * @child: a widget, usually a button
 *
 * Adds @child at the start.
 */
void
glass_button_group_prepend (GlassButtonGroup *self,
                            GtkWidget        *child)
{
  g_return_if_fail (GLASS_IS_BUTTON_GROUP (self));
  g_return_if_fail (GTK_IS_WIDGET (child));

  unframe (child);
  glass_pill_box_prepend (GLASS_PILL_BOX (self->row), child);
}

/**
 * glass_button_group_remove:
 * @self: a button group
 * @child: an item
 *
 * Removes @child.
 */
void
glass_button_group_remove (GlassButtonGroup *self,
                           GtkWidget        *child)
{
  g_return_if_fail (GLASS_IS_BUTTON_GROUP (self));

  reframe (child);
  glass_pill_box_remove (GLASS_PILL_BOX (self->row), child);
}

/**
 * glass_button_group_is_empty:
 * @self: a button group
 *
 * Returns: whether the group has no items
 */
gboolean
glass_button_group_is_empty (GlassButtonGroup *self)
{
  g_return_val_if_fail (GLASS_IS_BUTTON_GROUP (self), TRUE);

  return gtk_widget_get_first_child (self->row) == NULL;
}

/* Private (glass-private.h): whether any of the widgets in it is visible. */
gboolean
glass_button_group_has_visible_child (GlassButtonGroup *self)
{
  for (GtkWidget *child = gtk_widget_get_first_child (self->row); child; child = gtk_widget_get_next_sibling (child))
    if (gtk_widget_get_visible (child))
      return TRUE;
  return FALSE;
}
