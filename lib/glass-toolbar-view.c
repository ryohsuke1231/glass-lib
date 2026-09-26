/* glass-toolbar-view.c — content with bars floating over it (design.md §6.6).
 *
 * A GlassView whose content is the page and whose overlays are two boxes of
 * bars, one at each edge. The content always extends under the bars; the
 * view draws the scroll edge effect under them.
 *
 * SPDX-License-Identifier: MIT
 */
#include "glass-private.h"

/**
 * GlassToolbarView:
 *
 * The glass counterpart of `AdwToolbarView`: content with top and bottom
 * bars that float over it.
 *
 * The content always extends under the bars. Under each bar the content is
 * blurred and washed out ([property@ToolbarView:top-edge-style]) so the bar
 * stays readable over anything. [property@ToolbarView:top-bar-height] tells
 * how much of the content the bars cover, for the padding a scrolling
 * content should start with.
 *
 * ## CSS nodes
 *
 * `GlassToolbarView` has a CSS node with name `glasstoolbarview`; the bars
 * are in boxes with the style classes `.top-bars` and `.bottom-bars`.
 */

struct _GlassToolbarView {
  GtkWidget       parent_instance;

  GlassView      *view;
  GtkWidget      *top_box;
  GtkWidget      *bottom_box;
  GlassEdgeStyle  edge_style[2];
  int             bar_height[2];
  guint           notify_idle;
};

enum {
  PROP_0,
  PROP_CONTENT,
  PROP_TOP_EDGE_STYLE,
  PROP_BOTTOM_EDGE_STYLE,
  PROP_TOP_BAR_HEIGHT,
  PROP_BOTTOM_BAR_HEIGHT,
  N_PROPS
};

static GParamSpec *props[N_PROPS];

static void glass_toolbar_view_buildable_init (GtkBuildableIface *iface);

G_DEFINE_FINAL_TYPE_WITH_CODE (GlassToolbarView, glass_toolbar_view, GTK_TYPE_WIDGET,
                               G_IMPLEMENT_INTERFACE (GTK_TYPE_BUILDABLE, glass_toolbar_view_buildable_init))

static GtkBuildableIface *parent_buildable_iface;

static int
bar_height (GtkWidget *box, int width)
{
  int natural = 0;

  if (!gtk_widget_should_layout (box) || gtk_widget_get_first_child (box) == NULL)
    return 0;
  gtk_widget_measure (box, GTK_ORIENTATION_VERTICAL, width, NULL, &natural, NULL, NULL);
  return natural;
}

static gboolean
notify_heights (gpointer data)
{
  GlassToolbarView *self = data;

  self->notify_idle = 0;
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_TOP_BAR_HEIGHT]);
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_BOTTOM_BAR_HEIGHT]);
  return G_SOURCE_REMOVE;
}

static void
glass_toolbar_view_measure (GtkWidget      *widget,
                            GtkOrientation  orientation,
                            int             for_size,
                            int            *minimum,
                            int            *natural,
                            int            *minimum_baseline,
                            int            *natural_baseline)
{
  GlassToolbarView *self = GLASS_TOOLBAR_VIEW (widget);

  gtk_widget_measure (GTK_WIDGET (self->view), orientation, for_size, minimum, natural, NULL, NULL);
}

static void
glass_toolbar_view_size_allocate (GtkWidget *widget,
                                  int        width,
                                  int        height,
                                  int        baseline)
{
  GlassToolbarView *self = GLASS_TOOLBAR_VIEW (widget);
  int top = bar_height (self->top_box, width);
  int bottom = bar_height (self->bottom_box, width);

  /* The edge effects are drawn by the view, so it knows before it draws. */
  glass_view_set_edge (self->view, GTK_POS_TOP, self->edge_style[0], top);
  glass_view_set_edge (self->view, GTK_POS_BOTTOM, self->edge_style[1], bottom);
  gtk_widget_allocate (GTK_WIDGET (self->view), width, height, baseline, NULL);

  if (top != self->bar_height[0] || bottom != self->bar_height[1])
    {
      self->bar_height[0] = top;
      self->bar_height[1] = bottom;
      /* Not from inside the allocation: a handler would relayout. */
      if (self->notify_idle == 0)
        self->notify_idle = g_idle_add (notify_heights, self);
    }
}

static void
glass_toolbar_view_dispose (GObject *object)
{
  GlassToolbarView *self = GLASS_TOOLBAR_VIEW (object);

  g_clear_handle_id (&self->notify_idle, g_source_remove);
  if (self->view)
    {
      gtk_widget_unparent (GTK_WIDGET (self->view));
      self->view = NULL;
    }

  G_OBJECT_CLASS (glass_toolbar_view_parent_class)->dispose (object);
}

static void
glass_toolbar_view_get_property (GObject    *object,
                                 guint       prop_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
  GlassToolbarView *self = GLASS_TOOLBAR_VIEW (object);

  switch (prop_id)
    {
    case PROP_CONTENT:
      g_value_set_object (value, glass_toolbar_view_get_content (self));
      break;
    case PROP_TOP_EDGE_STYLE:
      g_value_set_enum (value, self->edge_style[0]);
      break;
    case PROP_BOTTOM_EDGE_STYLE:
      g_value_set_enum (value, self->edge_style[1]);
      break;
    case PROP_TOP_BAR_HEIGHT:
      g_value_set_int (value, self->bar_height[0]);
      break;
    case PROP_BOTTOM_BAR_HEIGHT:
      g_value_set_int (value, self->bar_height[1]);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
glass_toolbar_view_set_property (GObject      *object,
                                 guint         prop_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
  GlassToolbarView *self = GLASS_TOOLBAR_VIEW (object);

  switch (prop_id)
    {
    case PROP_CONTENT:
      glass_toolbar_view_set_content (self, g_value_get_object (value));
      break;
    case PROP_TOP_EDGE_STYLE:
      glass_toolbar_view_set_top_edge_style (self, g_value_get_enum (value));
      break;
    case PROP_BOTTOM_EDGE_STYLE:
      glass_toolbar_view_set_bottom_edge_style (self, g_value_get_enum (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
glass_toolbar_view_class_init (GlassToolbarViewClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = glass_toolbar_view_dispose;
  object_class->get_property = glass_toolbar_view_get_property;
  object_class->set_property = glass_toolbar_view_set_property;

  widget_class->measure = glass_toolbar_view_measure;
  widget_class->size_allocate = glass_toolbar_view_size_allocate;

  /**
   * GlassToolbarView:content:
   *
   * The page. It extends under the bars.
   */
  props[PROP_CONTENT] =
    g_param_spec_object ("content", NULL, NULL, GTK_TYPE_WIDGET,
                         G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

  /**
   * GlassToolbarView:top-edge-style:
   *
   * The scroll edge effect under the top bars.
   */
  props[PROP_TOP_EDGE_STYLE] =
    g_param_spec_enum ("top-edge-style", NULL, NULL, GLASS_TYPE_EDGE_STYLE, GLASS_EDGE_STYLE_SOFT,
                       G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

  /**
   * GlassToolbarView:bottom-edge-style:
   *
   * The scroll edge effect under the bottom bars.
   */
  props[PROP_BOTTOM_EDGE_STYLE] =
    g_param_spec_enum ("bottom-edge-style", NULL, NULL, GLASS_TYPE_EDGE_STYLE, GLASS_EDGE_STYLE_SOFT,
                       G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

  /**
   * GlassToolbarView:top-bar-height:
   *
   * How much of the top of the content the bars cover, in px.
   */
  props[PROP_TOP_BAR_HEIGHT] =
    g_param_spec_int ("top-bar-height", NULL, NULL, 0, G_MAXINT, 0,
                      G_PARAM_READABLE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

  /**
   * GlassToolbarView:bottom-bar-height:
   *
   * How much of the bottom of the content the bars cover, in px.
   */
  props[PROP_BOTTOM_BAR_HEIGHT] =
    g_param_spec_int ("bottom-bar-height", NULL, NULL, 0, G_MAXINT, 0,
                      G_PARAM_READABLE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, N_PROPS, props);

  gtk_widget_class_set_css_name (widget_class, "glasstoolbarview");
}

static void
glass_toolbar_view_init (GlassToolbarView *self)
{
  self->edge_style[0] = self->edge_style[1] = GLASS_EDGE_STYLE_SOFT;

  self->view = GLASS_VIEW (glass_view_new ());
  gtk_widget_set_parent (GTK_WIDGET (self->view), GTK_WIDGET (self));

  self->top_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
  gtk_widget_add_css_class (self->top_box, "top-bars");
  gtk_widget_set_valign (self->top_box, GTK_ALIGN_START);
  glass_view_add_overlay (self->view, self->top_box);

  self->bottom_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
  gtk_widget_add_css_class (self->bottom_box, "bottom-bars");
  gtk_widget_set_valign (self->bottom_box, GTK_ALIGN_END);
  glass_view_add_overlay (self->view, self->bottom_box);
}

static void
glass_toolbar_view_buildable_add_child (GtkBuildable *buildable,
                                        GtkBuilder   *builder,
                                        GObject      *child,
                                        const char   *type)
{
  GlassToolbarView *self = GLASS_TOOLBAR_VIEW (buildable);

  if (!GTK_IS_WIDGET (child))
    parent_buildable_iface->add_child (buildable, builder, child, type);
  else if (g_strcmp0 (type, "top") == 0)
    glass_toolbar_view_add_top_bar (self, GTK_WIDGET (child));
  else if (g_strcmp0 (type, "bottom") == 0)
    glass_toolbar_view_add_bottom_bar (self, GTK_WIDGET (child));
  else if (type == NULL)
    glass_toolbar_view_set_content (self, GTK_WIDGET (child));
  else
    parent_buildable_iface->add_child (buildable, builder, child, type);
}

static void
glass_toolbar_view_buildable_init (GtkBuildableIface *iface)
{
  parent_buildable_iface = g_type_interface_peek_parent (iface);
  iface->add_child = glass_toolbar_view_buildable_add_child;
}

/**
 * glass_toolbar_view_new:
 *
 * Creates a new toolbar view.
 *
 * Returns: a new toolbar view
 */
GtkWidget *
glass_toolbar_view_new (void)
{
  return g_object_new (GLASS_TYPE_TOOLBAR_VIEW, NULL);
}

/**
 * glass_toolbar_view_get_content:
 * @self: a toolbar view
 *
 * Gets the content.
 *
 * Returns: (transfer none) (nullable): the content
 */
GtkWidget *
glass_toolbar_view_get_content (GlassToolbarView *self)
{
  g_return_val_if_fail (GLASS_IS_TOOLBAR_VIEW (self), NULL);

  return glass_view_get_content (self->view);
}

/**
 * glass_toolbar_view_set_content:
 * @self: a toolbar view
 * @content: (nullable): the page
 *
 * Sets the content.
 */
void
glass_toolbar_view_set_content (GlassToolbarView *self,
                                GtkWidget        *content)
{
  g_return_if_fail (GLASS_IS_TOOLBAR_VIEW (self));

  if (glass_view_get_content (self->view) == content)
    return;
  glass_view_set_content (self->view, content);
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_CONTENT]);
}

/**
 * glass_toolbar_view_add_top_bar:
 * @self: a toolbar view
 * @widget: a bar, such as a [class@HeaderBar]
 *
 * Adds a bar at the top, below the ones added before.
 */
void
glass_toolbar_view_add_top_bar (GlassToolbarView *self,
                                GtkWidget        *widget)
{
  g_return_if_fail (GLASS_IS_TOOLBAR_VIEW (self));
  g_return_if_fail (GTK_IS_WIDGET (widget));

  gtk_box_append (GTK_BOX (self->top_box), widget);
}

/**
 * glass_toolbar_view_add_bottom_bar:
 * @self: a toolbar view
 * @widget: a bar
 *
 * Adds a bar at the bottom, below the ones added before.
 */
void
glass_toolbar_view_add_bottom_bar (GlassToolbarView *self,
                                   GtkWidget        *widget)
{
  g_return_if_fail (GLASS_IS_TOOLBAR_VIEW (self));
  g_return_if_fail (GTK_IS_WIDGET (widget));

  gtk_box_append (GTK_BOX (self->bottom_box), widget);
}

/**
 * glass_toolbar_view_remove:
 * @self: a toolbar view
 * @widget: a bar
 *
 * Removes a bar.
 */
void
glass_toolbar_view_remove (GlassToolbarView *self,
                           GtkWidget        *widget)
{
  GtkWidget *parent;

  g_return_if_fail (GLASS_IS_TOOLBAR_VIEW (self));

  parent = gtk_widget_get_parent (widget);
  if (parent == self->top_box || parent == self->bottom_box)
    gtk_box_remove (GTK_BOX (parent), widget);
  else
    g_critical ("glass_toolbar_view_remove: not a bar of this view");
}

/**
 * glass_toolbar_view_get_top_edge_style:
 * @self: a toolbar view
 *
 * Gets the scroll edge effect under the top bars.
 *
 * Returns: the scroll edge effect under the top bars
 */
GlassEdgeStyle
glass_toolbar_view_get_top_edge_style (GlassToolbarView *self)
{
  g_return_val_if_fail (GLASS_IS_TOOLBAR_VIEW (self), GLASS_EDGE_STYLE_SOFT);

  return self->edge_style[0];
}

/**
 * glass_toolbar_view_set_top_edge_style:
 * @self: a toolbar view
 * @style: the style
 *
 * Sets the scroll edge effect under the top bars.
 */
void
glass_toolbar_view_set_top_edge_style (GlassToolbarView *self,
                                       GlassEdgeStyle    style)
{
  g_return_if_fail (GLASS_IS_TOOLBAR_VIEW (self));

  if (self->edge_style[0] == style)
    return;
  self->edge_style[0] = style;
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_TOP_EDGE_STYLE]);
  gtk_widget_queue_allocate (GTK_WIDGET (self));
}

/**
 * glass_toolbar_view_get_bottom_edge_style:
 * @self: a toolbar view
 *
 * Gets the scroll edge effect under the bottom bars.
 *
 * Returns: the scroll edge effect under the bottom bars
 */
GlassEdgeStyle
glass_toolbar_view_get_bottom_edge_style (GlassToolbarView *self)
{
  g_return_val_if_fail (GLASS_IS_TOOLBAR_VIEW (self), GLASS_EDGE_STYLE_SOFT);

  return self->edge_style[1];
}

/**
 * glass_toolbar_view_set_bottom_edge_style:
 * @self: a toolbar view
 * @style: the style
 *
 * Sets the scroll edge effect under the bottom bars.
 */
void
glass_toolbar_view_set_bottom_edge_style (GlassToolbarView *self,
                                          GlassEdgeStyle    style)
{
  g_return_if_fail (GLASS_IS_TOOLBAR_VIEW (self));

  if (self->edge_style[1] == style)
    return;
  self->edge_style[1] = style;
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_BOTTOM_EDGE_STYLE]);
  gtk_widget_queue_allocate (GTK_WIDGET (self));
}

/**
 * glass_toolbar_view_get_top_bar_height:
 * @self: a toolbar view
 *
 * Gets how much of the top of the content the bars cover, in px.
 *
 * Returns: how much of the top of the content the bars cover, in px
 */
int
glass_toolbar_view_get_top_bar_height (GlassToolbarView *self)
{
  g_return_val_if_fail (GLASS_IS_TOOLBAR_VIEW (self), 0);

  return self->bar_height[0];
}

/**
 * glass_toolbar_view_get_bottom_bar_height:
 * @self: a toolbar view
 *
 * Gets how much of the bottom of the content the bars cover, in px.
 *
 * Returns: how much of the bottom of the content the bars cover, in px
 */
int
glass_toolbar_view_get_bottom_bar_height (GlassToolbarView *self)
{
  g_return_val_if_fail (GLASS_IS_TOOLBAR_VIEW (self), 0);

  return self->bar_height[1];
}
