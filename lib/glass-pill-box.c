/* glass-pill-box.c — a row whose children are drawn inside pill shapes.
 *
 * On a capsule of glass, a button's own background (hover, pressed) is a
 * rectangle with the theme's corner radius, cut by the capsule only on its
 * outer side: half a capsule. The border radius cannot be fixed from CSS
 * reliably: a theme in ~/.config/gtk-4.0/gtk.css sets it at USER priority,
 * above any application or library (docs/memo.md 地雷12, 17). So this row
 * clips each child to a pill (a circle for square children) when drawing
 * it: whatever the theme paints, the shape is round.
 *
 * SPDX-License-Identifier: MIT
 */
#include "glass-private.h"

struct _GlassPillBox {
  GtkWidget parent_instance;
};

G_DEFINE_FINAL_TYPE (GlassPillBox, glass_pill_box, GTK_TYPE_WIDGET)

static void
glass_pill_box_snapshot (GtkWidget   *widget,
                         GtkSnapshot *snapshot)
{
  for (GtkWidget *child = gtk_widget_get_first_child (widget);
       child != NULL;
       child = gtk_widget_get_next_sibling (child))
    {
      graphene_rect_t bounds;
      GskRoundedRect pill;

      if (!gtk_widget_get_visible (child))
        continue;
      if (!gtk_widget_compute_bounds (child, widget, &bounds))
        {
          gtk_widget_snapshot_child (widget, child, snapshot);
          continue;
        }

      gsk_rounded_rect_init_from_rect (&pill, &bounds,
                                       MIN (bounds.size.width, bounds.size.height) / 2.0f);
      gtk_snapshot_push_rounded_clip (snapshot, &pill);
      gtk_widget_snapshot_child (widget, child, snapshot);
      gtk_snapshot_pop (snapshot);
    }
}

static void
glass_pill_box_dispose (GObject *object)
{
  GtkWidget *child;

  while ((child = gtk_widget_get_first_child (GTK_WIDGET (object))) != NULL)
    gtk_widget_unparent (child);

  G_OBJECT_CLASS (glass_pill_box_parent_class)->dispose (object);
}

static void
glass_pill_box_class_init (GlassPillBoxClass *klass)
{
  GTK_WIDGET_CLASS (klass)->snapshot = glass_pill_box_snapshot;
  G_OBJECT_CLASS (klass)->dispose = glass_pill_box_dispose;
  gtk_widget_class_set_layout_manager_type (GTK_WIDGET_CLASS (klass), GTK_TYPE_BOX_LAYOUT);
  gtk_widget_class_set_css_name (GTK_WIDGET_CLASS (klass), "box");
}

static void
glass_pill_box_init (GlassPillBox *self)
{
  gtk_widget_add_css_class (GTK_WIDGET (self), "pills");
}

GtkWidget *
glass_pill_box_new (void)
{
  return g_object_new (GLASS_TYPE_PILL_BOX, NULL);
}

void
glass_pill_box_set_homogeneous (GlassPillBox *self,
                                gboolean      homogeneous)
{
  GtkLayoutManager *layout = gtk_widget_get_layout_manager (GTK_WIDGET (self));

  gtk_box_layout_set_homogeneous (GTK_BOX_LAYOUT (layout), homogeneous);
}

void
glass_pill_box_append (GlassPillBox *self,
                       GtkWidget    *child)
{
  gtk_widget_insert_before (child, GTK_WIDGET (self), NULL);
}

void
glass_pill_box_prepend (GlassPillBox *self,
                        GtkWidget    *child)
{
  gtk_widget_insert_after (child, GTK_WIDGET (self), NULL);
}

void
glass_pill_box_remove (GlassPillBox *self,
                       GtkWidget    *child)
{
  g_return_if_fail (gtk_widget_get_parent (child) == GTK_WIDGET (self));

  gtk_widget_unparent (child);
}
