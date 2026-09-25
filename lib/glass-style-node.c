/* glass-style-node.c — a widget that is only a CSS node.
 *
 * GlassView and GlassToggleGroup read colours from CSS (the window
 * background, the toggle plate) through a child that is styled but never
 * drawn. GtkWidget itself is abstract, hence this subclass.
 *
 * SPDX-License-Identifier: MIT
 */
#include "glass-private.h"

struct _GlassStyleNode {
  GtkWidget parent_instance;
};

G_DEFINE_FINAL_TYPE (GlassStyleNode, glass_style_node, GTK_TYPE_WIDGET)

static void
glass_style_node_class_init (GlassStyleNodeClass *klass)
{
}

static void
glass_style_node_init (GlassStyleNode *self)
{
  gtk_widget_set_can_target (GTK_WIDGET (self), FALSE);
  gtk_widget_set_can_focus (GTK_WIDGET (self), FALSE);
}

GtkWidget *
glass_style_node_new (const char *css_name)
{
  return g_object_new (GLASS_TYPE_STYLE_NODE, "css-name", css_name, NULL);
}
