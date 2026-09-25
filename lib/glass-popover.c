/* glass-popover.c — a popover of glass, and menus on it (design.md §6.7).
 *
 * A popover is a surface of its own (xdg_popup on Wayland): no GlassView
 * can draw under it. So it draws its glass itself (GlassStandalone) from
 * the parent window's render node, placed where the popup is. The window's
 * node is the one it last drew; whenever the window renders again the
 * popover redraws, a frame later (it cannot be the same frame: they are
 * two surfaces).
 *
 * The contents' own CSS background is not drawn at all: the popover draws
 * the glass, then the contents' children. So a theme that paints popovers
 * at USER priority (docs/memo.md 地雷12) still gets glass.
 *
 * Menus: GtkPopoverMenu cannot be subclassed, so glass_popover_new_from_model()
 * builds its own from a GMenuModel: items, sections and submenus (a page each).
 *
 * SPDX-License-Identifier: MIT
 */
#include "glass-private.h"

/**
 * GlassPopover:
 *
 * A popover whose background is a pane of glass over the window it opens
 * from. It has no arrow.
 *
 * [ctor@Popover.new_from_model] fills it with a menu built from a
 * `GMenuModel`: items (with their actions and targets), sections and
 * submenus.
 *
 * ## CSS nodes
 *
 * `GlassPopover` is a `popover` with the style class `.glass`; menus are a
 * `box.glass-menu` of `button.glass-menu-item`s. The glass is tinted with
 * the `color` of the never-drawn child node `backdrop`
 * (`var(--popover-bg-color)`).
 */

#define RADIUS 15.0

struct _GlassPopover {
  GtkPopover       parent_instance;

  GlassStandalone *glass;
  GtkWidget       *bg_node;
  GdkSurface      *watched;          /* the window's surface, while mapped */
  gulong           render_handler;
  guint            submenus;
};

G_DEFINE_FINAL_TYPE (GlassPopover, glass_popover, GTK_TYPE_POPOVER)

static GtkWidget *
find_contents (GtkWidget *popover)
{
  for (GtkWidget *child = gtk_widget_get_first_child (popover); child; child = gtk_widget_get_next_sibling (child))
    if (g_strcmp0 (gtk_widget_get_css_name (child), "contents") == 0)
      return child;
  return NULL;
}

/* The window the popover opens from, and where its origin is in the
 * popover's coordinates. */
static GtkWidget *
parent_window (GlassPopover *self, graphene_point_t *offset)
{
  GtkNative *native = GTK_NATIVE (self);
  GdkSurface *surface = gtk_native_get_surface (native);
  GtkWidget *parent = gtk_widget_get_parent (GTK_WIDGET (self));
  GtkRoot *root = parent ? gtk_widget_get_root (parent) : NULL;
  double px, py, wx, wy;

  if (surface == NULL || !GDK_IS_POPUP (surface) || !GTK_IS_WINDOW (root))
    return NULL;

  gtk_native_get_surface_transform (native, &px, &py);
  gtk_native_get_surface_transform (GTK_NATIVE (root), &wx, &wy);
  /* Popup position: its surface in the parent surface's coordinates. */
  offset->x = (float) -(gdk_popup_get_position_x (GDK_POPUP (surface)) + px - wx);
  offset->y = (float) -(gdk_popup_get_position_y (GDK_POPUP (surface)) + py - wy);
  return GTK_WIDGET (root);
}

static void
glass_popover_snapshot (GtkWidget   *widget,
                        GtkSnapshot *snapshot)
{
  GlassPopover *self = GLASS_POPOVER (widget);
  GtkWidget *contents = find_contents (widget);
  GtkWidget *window;
  graphene_point_t offset;
  graphene_rect_t box;
  GskRenderNode *backdrop = NULL;
  GdkRGBA bg;
  GskRoundedRect shape;

  if (contents == NULL || !gtk_widget_compute_bounds (contents, widget, &box) ||
      (window = parent_window (self, &offset)) == NULL ||
      (backdrop = glass_standalone_backdrop (window, &offset)) == NULL)
    goto plain;

  /* A soft shadow within the popover's own margins. */
  gsk_rounded_rect_init_from_rect (&shape, &box, RADIUS);
  gtk_snapshot_append_outset_shadow (snapshot, &shape, &(GdkRGBA) { 0, 0, 0.02f, 0.22f }, 0, 2, 0, 8);

  gtk_widget_get_color (self->bg_node, &bg);
  if (!glass_standalone_draw (self->glass, widget, snapshot, backdrop, &box, RADIUS,
                              GLASS_MATERIAL_THICK, &bg, FALSE))
    goto plain;
  gsk_render_node_unref (backdrop);

  /* The contents' children, without the contents' own background. */
  gtk_snapshot_save (snapshot);
  gtk_snapshot_translate (snapshot, &box.origin);
  for (GtkWidget *child = gtk_widget_get_first_child (contents); child; child = gtk_widget_get_next_sibling (child))
    gtk_widget_snapshot_child (contents, child, snapshot);
  gtk_snapshot_restore (snapshot);
  return;

plain:
  g_clear_pointer (&backdrop, gsk_render_node_unref);
  GTK_WIDGET_CLASS (glass_popover_parent_class)->snapshot (widget, snapshot);
}

static gboolean
window_rendered (GdkSurface     *surface,
                 cairo_region_t *region,
                 GlassPopover   *self)
{
  /* What is behind the popover may have changed. */
  gtk_widget_queue_draw (GTK_WIDGET (self));
  return FALSE;
}

static void
glass_popover_map (GtkWidget *widget)
{
  GlassPopover *self = GLASS_POPOVER (widget);
  GtkWidget *parent = gtk_widget_get_parent (widget);
  GtkNative *native = parent ? gtk_widget_get_native (parent) : NULL;

  GTK_WIDGET_CLASS (glass_popover_parent_class)->map (widget);

  if (native)
    {
      self->watched = g_object_ref (gtk_native_get_surface (native));
      self->render_handler = g_signal_connect_after (self->watched, "render",
                                                     G_CALLBACK (window_rendered), self);
    }
}

static void
glass_popover_unmap (GtkWidget *widget)
{
  GlassPopover *self = GLASS_POPOVER (widget);

  if (self->watched)
    {
      g_clear_signal_handler (&self->render_handler, self->watched);
      g_clear_object (&self->watched);
    }
  GTK_WIDGET_CLASS (glass_popover_parent_class)->unmap (widget);
}

static void
glass_popover_unrealize (GtkWidget *widget)
{
  glass_standalone_release (GLASS_POPOVER (widget)->glass);
  GTK_WIDGET_CLASS (glass_popover_parent_class)->unrealize (widget);
}

static void
glass_popover_size_allocate (GtkWidget *widget, int width, int height, int baseline)
{
  GTK_WIDGET_CLASS (glass_popover_parent_class)->size_allocate (widget, width, height, baseline);
  gtk_widget_allocate (GLASS_POPOVER (widget)->bg_node, 0, 0, -1, NULL);
}

static void
glass_popover_root (GtkWidget *widget)
{
  GTK_WIDGET_CLASS (glass_popover_parent_class)->root (widget);
  glass_style_ensure (gtk_widget_get_display (widget));
}

static void
glass_popover_dispose (GObject *object)
{
  GlassPopover *self = GLASS_POPOVER (object);

  g_clear_pointer (&self->bg_node, gtk_widget_unparent);
  g_clear_pointer (&self->glass, glass_standalone_free);

  G_OBJECT_CLASS (glass_popover_parent_class)->dispose (object);
}

static void
glass_popover_class_init (GlassPopoverClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = glass_popover_dispose;

  widget_class->snapshot = glass_popover_snapshot;
  widget_class->map = glass_popover_map;
  widget_class->unmap = glass_popover_unmap;
  widget_class->unrealize = glass_popover_unrealize;
  widget_class->size_allocate = glass_popover_size_allocate;
  widget_class->root = glass_popover_root;
}

static void
glass_popover_init (GlassPopover *self)
{
  self->glass = glass_standalone_new ();
  gtk_popover_set_has_arrow (GTK_POPOVER (self), FALSE);
  gtk_widget_add_css_class (GTK_WIDGET (self), "glass");

  /* Never drawn: the glass's tint, var(--popover-bg-color) (glass.css). */
  self->bg_node = glass_style_node_new ("backdrop");
  gtk_widget_set_parent (self->bg_node, GTK_WIDGET (self));
}

/**
 * glass_popover_new:
 *
 * Returns: a new popover of glass
 */
GtkWidget *
glass_popover_new (void)
{
  return g_object_new (GLASS_TYPE_POPOVER, NULL);
}

/* ── Menus from a GMenuModel ─────────────────────────────────────────────── */

static void
item_clicked (GtkButton    *button,
              GlassPopover *self)
{
  gtk_popover_popdown (GTK_POPOVER (self));
}

static GtkWidget *
menu_row (const char *label, const char *icon_name, const char *trailing)
{
  GtkWidget *row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 10);
  GtkWidget *text = gtk_label_new_with_mnemonic (label ? label : "");

  if (icon_name)
    gtk_box_append (GTK_BOX (row), gtk_image_new_from_icon_name (icon_name));
  gtk_label_set_xalign (GTK_LABEL (text), 0.0f);
  gtk_widget_set_hexpand (text, TRUE);
  gtk_box_append (GTK_BOX (row), text);
  if (trailing)
    gtk_box_append (GTK_BOX (row), gtk_image_new_from_icon_name (trailing));
  return row;
}

static void show_page (GtkButton *button, GtkStack *stack);

static void
append_items (GlassPopover *self,
              GtkStack     *stack,
              GtkWidget    *box,
              GMenuModel   *model,
              gboolean     *first_section)
{
  int n = g_menu_model_get_n_items (model);

  for (int i = 0; i < n; i++)
    {
      g_autoptr (GMenuModel) section = g_menu_model_get_item_link (model, i, G_MENU_LINK_SECTION);
      g_autoptr (GMenuModel) submenu = g_menu_model_get_item_link (model, i, G_MENU_LINK_SUBMENU);
      g_autofree char *label = NULL;
      g_autofree char *action = NULL;
      g_autoptr (GVariant) icon = g_menu_model_get_item_attribute_value (model, i, G_MENU_ATTRIBUTE_ICON, NULL);
      g_autofree char *icon_name = NULL;
      GtkWidget *button;

      g_menu_model_get_item_attribute (model, i, G_MENU_ATTRIBUTE_LABEL, "s", &label);
      if (icon && g_variant_is_of_type (icon, G_VARIANT_TYPE_STRING))
        icon_name = g_variant_dup_string (icon, NULL);

      if (section)
        {
          if (!*first_section)
            gtk_box_append (GTK_BOX (box), gtk_separator_new (GTK_ORIENTATION_HORIZONTAL));
          *first_section = FALSE;
          append_items (self, stack, box, section, first_section);
          *first_section = FALSE;
          continue;
        }

      if (submenu)
        {
          g_autofree char *page_name = g_strdup_printf ("submenu-%u", self->submenus++);
          GtkWidget *page = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
          GtkWidget *back = gtk_button_new ();
          gboolean first = TRUE;

          gtk_widget_add_css_class (page, "glass-menu");
          gtk_button_set_child (GTK_BUTTON (back), menu_row (label, "go-previous-symbolic", NULL));
          gtk_widget_add_css_class (back, "flat");
          gtk_widget_add_css_class (back, "glass-menu-item");
          gtk_widget_add_css_class (back, "back");
          g_object_set_data_full (G_OBJECT (back), "page", g_strdup ("main"), g_free);
          g_signal_connect (back, "clicked", G_CALLBACK (show_page), stack);
          gtk_box_append (GTK_BOX (page), back);
          gtk_box_append (GTK_BOX (page), gtk_separator_new (GTK_ORIENTATION_HORIZONTAL));
          append_items (self, stack, page, submenu, &first);
          gtk_stack_add_named (stack, page, page_name);

          button = gtk_button_new ();
          gtk_button_set_child (GTK_BUTTON (button), menu_row (label, icon_name, "go-next-symbolic"));
          g_object_set_data_full (G_OBJECT (button), "page", g_steal_pointer (&page_name), g_free);
          g_signal_connect (button, "clicked", G_CALLBACK (show_page), stack);
        }
      else
        {
          g_autoptr (GVariant) target = g_menu_model_get_item_attribute_value (model, i, G_MENU_ATTRIBUTE_TARGET, NULL);

          button = gtk_button_new ();
          gtk_button_set_child (GTK_BUTTON (button), menu_row (label, icon_name, NULL));
          gtk_button_set_use_underline (GTK_BUTTON (button), TRUE);
          if (g_menu_model_get_item_attribute (model, i, G_MENU_ATTRIBUTE_ACTION, "s", &action))
            gtk_actionable_set_action_name (GTK_ACTIONABLE (button), action);
          if (target)
            gtk_actionable_set_action_target_value (GTK_ACTIONABLE (button), target);
          g_signal_connect_after (button, "clicked", G_CALLBACK (item_clicked), self);
        }

      gtk_widget_add_css_class (button, "flat");
      gtk_widget_add_css_class (button, "glass-menu-item");
      gtk_box_append (GTK_BOX (box), button);
    }
}

static void
show_page (GtkButton *button,
           GtkStack  *stack)
{
  const char *page = g_object_get_data (G_OBJECT (button), "page");

  gtk_stack_set_transition_type (stack, g_strcmp0 (page, "main") == 0
                                        ? GTK_STACK_TRANSITION_TYPE_SLIDE_RIGHT
                                        : GTK_STACK_TRANSITION_TYPE_SLIDE_LEFT);
  gtk_stack_set_visible_child_name (stack, page);
}

static void
back_to_main (GtkPopover *popover,
              GtkStack   *stack)
{
  gtk_stack_set_transition_type (stack, GTK_STACK_TRANSITION_TYPE_NONE);
  gtk_stack_set_visible_child_name (stack, "main");
}

/**
 * glass_popover_new_from_model:
 * @model: a menu
 *
 * Returns: a new popover of glass holding a menu built from @model: its
 *   items activate their actions (with their targets) and close the
 *   popover; sections are separated by lines; submenus open as pages.
 */
GtkWidget *
glass_popover_new_from_model (GMenuModel *model)
{
  GlassPopover *self = g_object_new (GLASS_TYPE_POPOVER, NULL);
  GtkWidget *stack = gtk_stack_new ();
  GtkWidget *main_page = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
  gboolean first = TRUE;

  g_return_val_if_fail (G_IS_MENU_MODEL (model), GTK_WIDGET (self));

  gtk_widget_add_css_class (GTK_WIDGET (self), "menu");
  gtk_widget_add_css_class (main_page, "glass-menu");
  gtk_stack_set_vhomogeneous (GTK_STACK (stack), FALSE);
  gtk_stack_set_interpolate_size (GTK_STACK (stack), TRUE);
  gtk_stack_add_named (GTK_STACK (stack), main_page, "main");
  append_items (self, GTK_STACK (stack), main_page, model, &first);
  gtk_stack_set_visible_child_name (GTK_STACK (stack), "main");
  gtk_popover_set_child (GTK_POPOVER (self), stack);
  g_signal_connect_object (self, "closed", G_CALLBACK (back_to_main), stack, 0);

  return GTK_WIDGET (self);
}
