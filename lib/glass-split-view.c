/* glass-split-view.c — content with a sidebar floating over it
 * (design.md §6.6).
 *
 * A GlassView whose content is the main page and whose one overlay is the
 * sidebar: a THICK glass panel inset from the window edges. It slides in and
 * out by moving the overlay (glass_view_set_overlay_offset()), so the glass
 * follows it frame by frame.
 *
 * SPDX-License-Identifier: MIT
 */
#include "glass-private.h"

#include <adwaita.h>
#include <math.h>

/**
 * GlassSplitView:
 *
 * The glass counterpart of `AdwOverlaySplitView`: a sidebar that floats
 * over the content as a pane of thick glass.
 *
 * The content extends under the sidebar;
 * [property@SplitView:content-inset] is how much of it the sidebar covers,
 * for the content's own start padding (or the start margin of its header
 * bar).
 *
 * Glass is not drawn on glass: [class@Panel]s inside the sidebar (for
 * example a [class@HeaderBar]'s capsules) are drawn as plain shapes.
 *
 * ## CSS nodes
 *
 * `GlassSplitView` has a CSS node with name `glasssplitview`. The sidebar is
 * a [class@Panel] with the style class `.glass-sidebar` (not `.sidebar`,
 * which themes give an opaque background of their own).
 */

#define SIDEBAR_MARGIN 8
#define SIDEBAR_RADIUS 14.0

struct _GlassSplitView {
  GtkWidget     parent_instance;

  GlassView    *view;
  GtkWidget    *panel;
  GtkWidget    *clamp;
  GtkWidget    *sidebar;
  gboolean      show_sidebar;
  GtkPackType   position;
  int           width;
  double        progress;          /* 0 hidden .. 1 shown */
  int           content_inset;
  AdwAnimation *animation;
};

enum {
  PROP_0,
  PROP_SIDEBAR,
  PROP_CONTENT,
  PROP_SHOW_SIDEBAR,
  PROP_SIDEBAR_POSITION,
  PROP_SIDEBAR_WIDTH,
  PROP_CONTENT_INSET,
  N_PROPS
};

static GParamSpec *props[N_PROPS];

static void glass_split_view_buildable_init (GtkBuildableIface *iface);

G_DEFINE_FINAL_TYPE_WITH_CODE (GlassSplitView, glass_split_view, GTK_TYPE_WIDGET,
                               G_IMPLEMENT_INTERFACE (GTK_TYPE_BUILDABLE, glass_split_view_buildable_init))

static GtkBuildableIface *parent_buildable_iface;

static void
apply_progress (GlassSplitView *self)
{
  double travel = self->width + 2 * SIDEBAR_MARGIN;
  double dx = travel * (1.0 - self->progress);
  int inset = (int) lround (travel * self->progress);

  glass_view_set_overlay_offset (self->view, self->panel,
                                 self->position == GTK_PACK_START ? -dx : dx, 0);
  gtk_widget_set_visible (self->panel, self->progress > 0.0);

  if (inset != self->content_inset)
    {
      self->content_inset = inset;
      g_object_notify_by_pspec (G_OBJECT (self), props[PROP_CONTENT_INSET]);
    }
}

static void
animation_value (double value, gpointer data)
{
  GlassSplitView *self = data;

  self->progress = value;
  apply_progress (self);
}

static void
update_geometry (GlassSplitView *self)
{
  gtk_widget_set_halign (self->panel, self->position == GTK_PACK_START ? GTK_ALIGN_START : GTK_ALIGN_END);
  gtk_widget_set_size_request (self->panel, self->width, -1);
  adw_clamp_set_maximum_size (ADW_CLAMP (self->clamp), self->width);
  adw_clamp_set_tightening_threshold (ADW_CLAMP (self->clamp), self->width);
  apply_progress (self);
}

static void
glass_split_view_measure (GtkWidget      *widget,
                          GtkOrientation  orientation,
                          int             for_size,
                          int            *minimum,
                          int            *natural,
                          int            *minimum_baseline,
                          int            *natural_baseline)
{
  gtk_widget_measure (GTK_WIDGET (GLASS_SPLIT_VIEW (widget)->view), orientation, for_size,
                      minimum, natural, NULL, NULL);
}

static void
glass_split_view_size_allocate (GtkWidget *widget,
                                int        width,
                                int        height,
                                int        baseline)
{
  gtk_widget_allocate (GTK_WIDGET (GLASS_SPLIT_VIEW (widget)->view), width, height, baseline, NULL);
}

static void
glass_split_view_dispose (GObject *object)
{
  GlassSplitView *self = GLASS_SPLIT_VIEW (object);

  g_clear_object (&self->animation);
  if (self->view)
    {
      gtk_widget_unparent (GTK_WIDGET (self->view));
      self->view = NULL;
    }

  G_OBJECT_CLASS (glass_split_view_parent_class)->dispose (object);
}

static void
glass_split_view_get_property (GObject    *object,
                               guint       prop_id,
                               GValue     *value,
                               GParamSpec *pspec)
{
  GlassSplitView *self = GLASS_SPLIT_VIEW (object);

  switch (prop_id)
    {
    case PROP_SIDEBAR:
      g_value_set_object (value, self->sidebar);
      break;
    case PROP_CONTENT:
      g_value_set_object (value, glass_split_view_get_content (self));
      break;
    case PROP_SHOW_SIDEBAR:
      g_value_set_boolean (value, self->show_sidebar);
      break;
    case PROP_SIDEBAR_POSITION:
      g_value_set_enum (value, self->position);
      break;
    case PROP_SIDEBAR_WIDTH:
      g_value_set_int (value, self->width);
      break;
    case PROP_CONTENT_INSET:
      g_value_set_int (value, self->content_inset);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
glass_split_view_set_property (GObject      *object,
                               guint         prop_id,
                               const GValue *value,
                               GParamSpec   *pspec)
{
  GlassSplitView *self = GLASS_SPLIT_VIEW (object);

  switch (prop_id)
    {
    case PROP_SIDEBAR:
      glass_split_view_set_sidebar (self, g_value_get_object (value));
      break;
    case PROP_CONTENT:
      glass_split_view_set_content (self, g_value_get_object (value));
      break;
    case PROP_SHOW_SIDEBAR:
      glass_split_view_set_show_sidebar (self, g_value_get_boolean (value));
      break;
    case PROP_SIDEBAR_POSITION:
      glass_split_view_set_sidebar_position (self, g_value_get_enum (value));
      break;
    case PROP_SIDEBAR_WIDTH:
      glass_split_view_set_sidebar_width (self, g_value_get_int (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
glass_split_view_class_init (GlassSplitViewClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = glass_split_view_dispose;
  object_class->get_property = glass_split_view_get_property;
  object_class->set_property = glass_split_view_set_property;

  widget_class->measure = glass_split_view_measure;
  widget_class->size_allocate = glass_split_view_size_allocate;

  /**
   * GlassSplitView:sidebar:
   *
   * What is on the sidebar.
   */
  props[PROP_SIDEBAR] =
    g_param_spec_object ("sidebar", NULL, NULL, GTK_TYPE_WIDGET,
                         G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

  /**
   * GlassSplitView:content:
   *
   * The main page. It extends under the sidebar.
   */
  props[PROP_CONTENT] =
    g_param_spec_object ("content", NULL, NULL, GTK_TYPE_WIDGET,
                         G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

  /**
   * GlassSplitView:show-sidebar:
   *
   * Whether the sidebar is shown; it slides in and out.
   */
  props[PROP_SHOW_SIDEBAR] =
    g_param_spec_boolean ("show-sidebar", NULL, NULL, TRUE,
                          G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

  /**
   * GlassSplitView:sidebar-position:
   *
   * The side of the sidebar.
   */
  props[PROP_SIDEBAR_POSITION] =
    g_param_spec_enum ("sidebar-position", NULL, NULL, GTK_TYPE_PACK_TYPE, GTK_PACK_START,
                       G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

  /**
   * GlassSplitView:sidebar-width:
   *
   * The sidebar's width in px.
   */
  props[PROP_SIDEBAR_WIDTH] =
    g_param_spec_int ("sidebar-width", NULL, NULL, 120, G_MAXINT, 260,
                      G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

  /**
   * GlassSplitView:content-inset:
   *
   * How much of the content the sidebar covers, in px, from its side
   * (including the margins around the sidebar). It follows the sidebar as
   * it slides.
   */
  props[PROP_CONTENT_INSET] =
    g_param_spec_int ("content-inset", NULL, NULL, 0, G_MAXINT, 0,
                      G_PARAM_READABLE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, N_PROPS, props);

  gtk_widget_class_set_css_name (widget_class, "glasssplitview");
}

static void
glass_split_view_init (GlassSplitView *self)
{
  AdwAnimationTarget *target;

  self->show_sidebar = TRUE;
  self->position = GTK_PACK_START;
  self->width = 260;
  self->progress = 1.0;

  self->view = GLASS_VIEW (glass_view_new ());
  gtk_widget_set_parent (GTK_WIDGET (self->view), GTK_WIDGET (self));

  self->clamp = adw_clamp_new ();
  self->panel = glass_panel_new ();
  glass_panel_set_material (GLASS_PANEL (self->panel), GLASS_MATERIAL_THICK);
  glass_panel_set_corner_radius (GLASS_PANEL (self->panel), SIDEBAR_RADIUS);
  glass_panel_set_child (GLASS_PANEL (self->panel), self->clamp);
  gtk_widget_add_css_class (self->panel, "glass-sidebar");
  gtk_widget_set_valign (self->panel, GTK_ALIGN_FILL);
  gtk_widget_set_margin_start (self->panel, SIDEBAR_MARGIN);
  gtk_widget_set_margin_end (self->panel, SIDEBAR_MARGIN);
  gtk_widget_set_margin_top (self->panel, SIDEBAR_MARGIN);
  gtk_widget_set_margin_bottom (self->panel, SIDEBAR_MARGIN);
  glass_view_add_overlay (self->view, self->panel);

  target = adw_callback_animation_target_new (animation_value, self, NULL);
  self->animation = adw_timed_animation_new (GTK_WIDGET (self), 0.0, 1.0, 250, target);
  adw_timed_animation_set_easing (ADW_TIMED_ANIMATION (self->animation), ADW_EASE_OUT_CUBIC);

  update_geometry (self);
}

static void
glass_split_view_buildable_add_child (GtkBuildable *buildable,
                                      GtkBuilder   *builder,
                                      GObject      *child,
                                      const char   *type)
{
  GlassSplitView *self = GLASS_SPLIT_VIEW (buildable);

  if (!GTK_IS_WIDGET (child))
    parent_buildable_iface->add_child (buildable, builder, child, type);
  else if (g_strcmp0 (type, "sidebar") == 0)
    glass_split_view_set_sidebar (self, GTK_WIDGET (child));
  else if (type == NULL || g_strcmp0 (type, "content") == 0)
    glass_split_view_set_content (self, GTK_WIDGET (child));
  else
    parent_buildable_iface->add_child (buildable, builder, child, type);
}

static void
glass_split_view_buildable_init (GtkBuildableIface *iface)
{
  parent_buildable_iface = g_type_interface_peek_parent (iface);
  iface->add_child = glass_split_view_buildable_add_child;
}

/**
 * glass_split_view_new:
 *
 * Creates a new split view.
 *
 * Returns: a new split view
 */
GtkWidget *
glass_split_view_new (void)
{
  return g_object_new (GLASS_TYPE_SPLIT_VIEW, NULL);
}

/**
 * glass_split_view_get_sidebar:
 * @self: a split view
 *
 * Gets the sidebar.
 *
 * Returns: (transfer none) (nullable): the sidebar
 */
GtkWidget *
glass_split_view_get_sidebar (GlassSplitView *self)
{
  g_return_val_if_fail (GLASS_IS_SPLIT_VIEW (self), NULL);

  return self->sidebar;
}

/**
 * glass_split_view_set_sidebar:
 * @self: a split view
 * @sidebar: (nullable): what is on the sidebar
 *
 * Sets the sidebar.
 */
void
glass_split_view_set_sidebar (GlassSplitView *self,
                              GtkWidget      *sidebar)
{
  g_return_if_fail (GLASS_IS_SPLIT_VIEW (self));

  if (self->sidebar == sidebar)
    return;
  self->sidebar = sidebar;
  adw_clamp_set_child (ADW_CLAMP (self->clamp), sidebar);
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_SIDEBAR]);
}

/**
 * glass_split_view_get_content:
 * @self: a split view
 *
 * Gets the content.
 *
 * Returns: (transfer none) (nullable): the content
 */
GtkWidget *
glass_split_view_get_content (GlassSplitView *self)
{
  g_return_val_if_fail (GLASS_IS_SPLIT_VIEW (self), NULL);

  return glass_view_get_content (self->view);
}

/**
 * glass_split_view_set_content:
 * @self: a split view
 * @content: (nullable): the main page
 *
 * Sets the content.
 */
void
glass_split_view_set_content (GlassSplitView *self,
                              GtkWidget      *content)
{
  g_return_if_fail (GLASS_IS_SPLIT_VIEW (self));

  if (glass_view_get_content (self->view) == content)
    return;
  glass_view_set_content (self->view, content);
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_CONTENT]);
}

/**
 * glass_split_view_get_show_sidebar:
 * @self: a split view
 *
 * Gets whether the sidebar is shown.
 *
 * Returns: whether the sidebar is shown
 */
gboolean
glass_split_view_get_show_sidebar (GlassSplitView *self)
{
  g_return_val_if_fail (GLASS_IS_SPLIT_VIEW (self), TRUE);

  return self->show_sidebar;
}

/**
 * glass_split_view_set_show_sidebar:
 * @self: a split view
 * @show_sidebar: whether to show the sidebar
 *
 * Shows or hides the sidebar; it slides.
 */
void
glass_split_view_set_show_sidebar (GlassSplitView *self,
                                   gboolean        show_sidebar)
{
  g_return_if_fail (GLASS_IS_SPLIT_VIEW (self));

  show_sidebar = !!show_sidebar;
  if (self->show_sidebar == show_sidebar)
    return;
  self->show_sidebar = show_sidebar;

  adw_timed_animation_set_value_from (ADW_TIMED_ANIMATION (self->animation), self->progress);
  adw_timed_animation_set_value_to (ADW_TIMED_ANIMATION (self->animation), show_sidebar ? 1.0 : 0.0);
  adw_animation_play (self->animation);

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_SHOW_SIDEBAR]);
}

/**
 * glass_split_view_get_sidebar_position:
 * @self: a split view
 *
 * Gets the sidebar's side.
 *
 * Returns: the sidebar's side
 */
GtkPackType
glass_split_view_get_sidebar_position (GlassSplitView *self)
{
  g_return_val_if_fail (GLASS_IS_SPLIT_VIEW (self), GTK_PACK_START);

  return self->position;
}

/**
 * glass_split_view_set_sidebar_position:
 * @self: a split view
 * @position: the side
 *
 * Sets the side of the sidebar.
 */
void
glass_split_view_set_sidebar_position (GlassSplitView *self,
                                       GtkPackType     position)
{
  g_return_if_fail (GLASS_IS_SPLIT_VIEW (self));

  if (self->position == position)
    return;
  self->position = position;
  update_geometry (self);
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_SIDEBAR_POSITION]);
}

/**
 * glass_split_view_get_sidebar_width:
 * @self: a split view
 *
 * Gets the sidebar's width in px.
 *
 * Returns: the sidebar's width in px
 */
int
glass_split_view_get_sidebar_width (GlassSplitView *self)
{
  g_return_val_if_fail (GLASS_IS_SPLIT_VIEW (self), 260);

  return self->width;
}

/**
 * glass_split_view_set_sidebar_width:
 * @self: a split view
 * @width: the width in px
 *
 * Sets the sidebar's width.
 */
void
glass_split_view_set_sidebar_width (GlassSplitView *self,
                                    int             width)
{
  g_return_if_fail (GLASS_IS_SPLIT_VIEW (self));

  width = MAX (width, 120);
  if (self->width == width)
    return;
  self->width = width;
  update_geometry (self);
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_SIDEBAR_WIDTH]);
}

/**
 * glass_split_view_get_content_inset:
 * @self: a split view
 *
 * Gets how much of the content the sidebar covers, in px.
 *
 * Returns: how much of the content the sidebar covers, in px
 */
int
glass_split_view_get_content_inset (GlassSplitView *self)
{
  g_return_val_if_fail (GLASS_IS_SPLIT_VIEW (self), 0);

  return self->content_inset;
}
