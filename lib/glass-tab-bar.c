/* glass-tab-bar.c — a tab bar on glass (design.md §6.8).
 *
 * The pages of an AdwViewStack as one capsule of glass: an item per page,
 * its icon over its title, and under the page shown a plate of glass on the
 * glass that slides between them. It is a GlassToggleGroup inside (the
 * plate, the pills, frameless buttons), filled from the stack's pages and
 * kept in step with their selection both ways.
 *
 * SPDX-License-Identifier: MIT
 */
#include "glass-private.h"

/**
 * GlassTabBar:
 *
 * The glass counterpart of `AdwViewSwitcherBar` (a tab bar in Apple's
 * terms): one item per page of an `AdwViewStack`, its icon over its title,
 * on a capsule of glass. A plate of glass slides under the page shown.
 *
 * Items follow the pages' `visible`, `title`, `icon-name` and
 * `needs-attention` (a dot on the icon).
 *
 * ## CSS nodes
 *
 * `GlassTabBar` has a CSS node with name `glasstabbar`. The capsule inside
 * is a [class@ToggleGroup] with the style class `.tab-bar`; its items are
 * `button.tab`, the dot `.tab-dot`.
 */

struct _GlassTabBar {
  GtkWidget          parent_instance;

  GtkWidget         *group;        /* GlassToggleGroup */
  AdwViewStack      *stack;
  GtkSelectionModel *pages;
  GArray            *positions;    /* guint: the page of each item */
  GPtrArray         *watched;      /* AdwViewStackPage*, refs */
  gboolean           syncing;
};

enum {
  PROP_0,
  PROP_STACK,
  N_PROPS
};

static GParamSpec *props[N_PROPS];

G_DEFINE_FINAL_TYPE (GlassTabBar, glass_tab_bar, GTK_TYPE_WIDGET)

static void rebuild (GlassTabBar *self);

static void
page_notify (AdwViewStackPage *page,
             GParamSpec       *pspec,
             GlassTabBar      *self)
{
  const char *name = g_param_spec_get_name (pspec);

  if (g_str_equal (name, "title") || g_str_equal (name, "icon-name") ||
      g_str_equal (name, "visible") || g_str_equal (name, "needs-attention") ||
      g_str_equal (name, "use-underline"))
    rebuild (self);
}

static void
unwatch (GlassTabBar *self)
{
  for (guint i = 0; i < self->watched->len; i++)
    g_signal_handlers_disconnect_by_func (g_ptr_array_index (self->watched, i), page_notify, self);
  g_ptr_array_set_size (self->watched, 0);
}

/* The page shown, as an item. */
static void
sync_selection (GlassTabBar *self)
{
  g_autoptr (GtkBitset) selected = NULL;
  guint position;

  if (self->pages == NULL)
    return;
  selected = gtk_selection_model_get_selection (self->pages);
  if (gtk_bitset_is_empty (selected))
    return;
  position = gtk_bitset_get_minimum (selected);
  for (guint k = 0; k < self->positions->len; k++)
    if (g_array_index (self->positions, guint, k) == position)
      {
        self->syncing = TRUE;
        glass_toggle_group_set_active (GLASS_TOGGLE_GROUP (self->group), k);
        self->syncing = FALSE;
        return;
      }
}

/* An item: the page's icon (with a dot when it needs attention) over its
 * title. */
static GtkWidget *
make_item (AdwViewStackPage *page)
{
  GtkWidget *box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 2);
  GtkWidget *icon_area = gtk_overlay_new ();
  GtkWidget *icon = gtk_image_new_from_icon_name (adw_view_stack_page_get_icon_name (page));
  GtkWidget *dot = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
  GtkWidget *title = gtk_label_new (adw_view_stack_page_get_title (page));

  gtk_widget_set_halign (icon_area, GTK_ALIGN_CENTER);
  gtk_overlay_set_child (GTK_OVERLAY (icon_area), icon);
  gtk_widget_add_css_class (dot, "tab-dot");
  gtk_widget_set_halign (dot, GTK_ALIGN_END);
  gtk_widget_set_valign (dot, GTK_ALIGN_START);
  gtk_widget_set_can_target (dot, FALSE);
  gtk_widget_set_visible (dot, adw_view_stack_page_get_needs_attention (page));
  gtk_overlay_add_overlay (GTK_OVERLAY (icon_area), dot);

  gtk_label_set_use_underline (GTK_LABEL (title), adw_view_stack_page_get_use_underline (page));
  gtk_label_set_ellipsize (GTK_LABEL (title), PANGO_ELLIPSIZE_END);

  gtk_box_append (GTK_BOX (box), icon_area);
  gtk_box_append (GTK_BOX (box), title);
  gtk_widget_set_valign (box, GTK_ALIGN_CENTER);
  return box;
}

static void
rebuild (GlassTabBar *self)
{
  guint n;

  unwatch (self);
  glass_toggle_group_remove_all (GLASS_TOGGLE_GROUP (self->group));
  g_array_set_size (self->positions, 0);
  if (self->pages == NULL)
    return;

  n = g_list_model_get_n_items (G_LIST_MODEL (self->pages));
  for (guint i = 0; i < n; i++)
    {
      g_autoptr (AdwViewStackPage) page = g_list_model_get_item (G_LIST_MODEL (self->pages), i);
      GtkWidget *button;

      g_signal_connect (page, "notify", G_CALLBACK (page_notify), self);
      g_ptr_array_add (self->watched, g_object_ref (page));
      if (!adw_view_stack_page_get_visible (page))
        continue;

      button = glass_toggle_group_append_item (GLASS_TOGGLE_GROUP (self->group),
                                               adw_view_stack_page_get_name (page), make_item (page));
      gtk_widget_add_css_class (button, "tab");
      gtk_widget_set_tooltip_text (button, adw_view_stack_page_get_title (page));
      g_array_append_val (self->positions, i);
    }
  sync_selection (self);
}

static void
items_changed (GListModel  *model,
               guint        position,
               guint        removed,
               guint        added,
               GlassTabBar *self)
{
  rebuild (self);
}

static void
selection_changed (GtkSelectionModel *model,
                   guint              position,
                   guint              n_items,
                   GlassTabBar       *self)
{
  sync_selection (self);
}

/* An item clicked: show its page. */
static void
active_changed (GlassToggleGroup *group,
                GParamSpec       *pspec,
                GlassTabBar      *self)
{
  guint k = glass_toggle_group_get_active (group);

  if (self->syncing || self->pages == NULL || k >= self->positions->len)
    return;
  gtk_selection_model_select_item (self->pages, g_array_index (self->positions, guint, k), TRUE);
}

static void
glass_tab_bar_dispose (GObject *object)
{
  GlassTabBar *self = GLASS_TAB_BAR (object);

  glass_tab_bar_set_stack (self, NULL);
  g_clear_pointer (&self->group, gtk_widget_unparent);

  G_OBJECT_CLASS (glass_tab_bar_parent_class)->dispose (object);
}

static void
glass_tab_bar_finalize (GObject *object)
{
  GlassTabBar *self = GLASS_TAB_BAR (object);

  g_array_unref (self->positions);
  g_ptr_array_unref (self->watched);

  G_OBJECT_CLASS (glass_tab_bar_parent_class)->finalize (object);
}

static void
glass_tab_bar_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  switch (prop_id)
    {
    case PROP_STACK:
      g_value_set_object (value, GLASS_TAB_BAR (object)->stack);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
glass_tab_bar_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
  switch (prop_id)
    {
    case PROP_STACK:
      glass_tab_bar_set_stack (GLASS_TAB_BAR (object), g_value_get_object (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
glass_tab_bar_class_init (GlassTabBarClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = glass_tab_bar_dispose;
  object_class->finalize = glass_tab_bar_finalize;
  object_class->get_property = glass_tab_bar_get_property;
  object_class->set_property = glass_tab_bar_set_property;

  /**
   * GlassTabBar:stack:
   *
   * The stack whose pages the bar shows.
   */
  props[PROP_STACK] =
    g_param_spec_object ("stack", NULL, NULL, ADW_TYPE_VIEW_STACK,
                         G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, N_PROPS, props);

  gtk_widget_class_set_layout_manager_type (widget_class, GTK_TYPE_BIN_LAYOUT);
  gtk_widget_class_set_css_name (widget_class, "glasstabbar");
}

static void
glass_tab_bar_init (GlassTabBar *self)
{
  self->positions = g_array_new (FALSE, FALSE, sizeof (guint));
  self->watched = g_ptr_array_new_with_free_func (g_object_unref);

  self->group = glass_toggle_group_new ();
  gtk_widget_add_css_class (self->group, "tab-bar");
  gtk_widget_set_parent (self->group, GTK_WIDGET (self));
  g_signal_connect (self->group, "notify::active", G_CALLBACK (active_changed), self);
}

/**
 * glass_tab_bar_new:
 *
 * Creates a new tab bar.
 *
 * Returns: a new tab bar
 */
GtkWidget *
glass_tab_bar_new (void)
{
  return g_object_new (GLASS_TYPE_TAB_BAR, NULL);
}

/**
 * glass_tab_bar_get_stack:
 * @self: a tab bar
 *
 * Gets the stack.
 *
 * Returns: (transfer none) (nullable): the stack
 */
AdwViewStack *
glass_tab_bar_get_stack (GlassTabBar *self)
{
  g_return_val_if_fail (GLASS_IS_TAB_BAR (self), NULL);

  return self->stack;
}

/**
 * glass_tab_bar_set_stack:
 * @self: a tab bar
 * @stack: (nullable): the stack whose pages to show
 *
 * Sets the stack.
 */
void
glass_tab_bar_set_stack (GlassTabBar  *self,
                         AdwViewStack *stack)
{
  g_return_if_fail (GLASS_IS_TAB_BAR (self));
  g_return_if_fail (stack == NULL || ADW_IS_VIEW_STACK (stack));

  if (self->stack == stack)
    return;

  if (self->pages)
    {
      g_signal_handlers_disconnect_by_data (self->pages, self);
      g_clear_object (&self->pages);
    }
  g_clear_object (&self->stack);

  if (stack)
    {
      self->stack = g_object_ref (stack);
      self->pages = adw_view_stack_get_pages (stack);
      g_signal_connect (self->pages, "items-changed", G_CALLBACK (items_changed), self);
      g_signal_connect (self->pages, "selection-changed", G_CALLBACK (selection_changed), self);
    }
  if (self->group)
    rebuild (self);

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_STACK]);
}
