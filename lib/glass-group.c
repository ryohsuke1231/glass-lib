/* glass-group.c — panels whose glass flows together (design.md §6.7).
 *
 * Only a marker for the view: every GlassPanel inside a group (at the same
 * layer) is drawn as one body of glass, the smooth union of their shapes
 * (shaders/core/glass_shape.glsl, fusedSD()). Panels closer than `spacing`
 * flow into each other like drops; moved apart, they come off as separate
 * drops again, frame by frame.
 *
 * SPDX-License-Identifier: MIT
 */
#include "glass-private.h"

/**
 * GlassGroup:
 *
 * A container whose [class@Panel]s are drawn as one body of glass: closer
 * than [property@Group:spacing] they flow into each other like drops of
 * water, and pull apart again when they move away (the counterpart of
 * SwiftUI's `GlassEffectContainer`).
 *
 * The panels share the first one's material and tint. Put the group among a
 * [class@View]'s overlay children; its child can be any layout (a box of
 * panels, panels moved by margins...).
 *
 * ## CSS nodes
 *
 * `GlassGroup` has a single CSS node with name `glassgroup`.
 */

struct _GlassGroup {
  GtkWidget  parent_instance;

  GtkWidget *child;
  double     spacing;
};

enum {
  PROP_0,
  PROP_CHILD,
  PROP_SPACING,
  N_PROPS
};

static GParamSpec *props[N_PROPS];

G_DEFINE_FINAL_TYPE (GlassGroup, glass_group, GTK_TYPE_WIDGET)

static void
glass_group_dispose (GObject *object)
{
  g_clear_pointer (&GLASS_GROUP (object)->child, gtk_widget_unparent);
  G_OBJECT_CLASS (glass_group_parent_class)->dispose (object);
}

static void
glass_group_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  GlassGroup *self = GLASS_GROUP (object);

  switch (prop_id)
    {
    case PROP_CHILD:
      g_value_set_object (value, self->child);
      break;
    case PROP_SPACING:
      g_value_set_double (value, self->spacing);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
glass_group_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
  GlassGroup *self = GLASS_GROUP (object);

  switch (prop_id)
    {
    case PROP_CHILD:
      glass_group_set_child (self, g_value_get_object (value));
      break;
    case PROP_SPACING:
      glass_group_set_spacing (self, g_value_get_double (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
glass_group_class_init (GlassGroupClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = glass_group_dispose;
  object_class->get_property = glass_group_get_property;
  object_class->set_property = glass_group_set_property;

  /**
   * GlassGroup:child:
   *
   * The layout holding the panels.
   */
  props[PROP_CHILD] =
    g_param_spec_object ("child", NULL, NULL, GTK_TYPE_WIDGET,
                         G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

  /**
   * GlassGroup:spacing:
   *
   * How close (in px) two panels must come before their glass starts to
   * flow together.
   */
  props[PROP_SPACING] =
    g_param_spec_double ("spacing", NULL, NULL, 0.0, 200.0, 16.0,
                         G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, N_PROPS, props);

  gtk_widget_class_set_layout_manager_type (GTK_WIDGET_CLASS (klass), GTK_TYPE_BIN_LAYOUT);
  gtk_widget_class_set_css_name (GTK_WIDGET_CLASS (klass), "glassgroup");
}

static void
glass_group_init (GlassGroup *self)
{
  self->spacing = 16.0;
}

/**
 * glass_group_new:
 *
 * Creates a new group.
 *
 * Returns: a new group
 */
GtkWidget *
glass_group_new (void)
{
  return g_object_new (GLASS_TYPE_GROUP, NULL);
}

/**
 * glass_group_get_child:
 * @self: a group
 *
 * Gets the child.
 *
 * Returns: (transfer none) (nullable): the child
 */
GtkWidget *
glass_group_get_child (GlassGroup *self)
{
  g_return_val_if_fail (GLASS_IS_GROUP (self), NULL);

  return self->child;
}

/**
 * glass_group_set_child:
 * @self: a group
 * @child: (nullable): the layout holding the panels
 *
 * Sets the child.
 */
void
glass_group_set_child (GlassGroup *self,
                       GtkWidget  *child)
{
  g_return_if_fail (GLASS_IS_GROUP (self));

  if (self->child == child)
    return;
  g_clear_pointer (&self->child, gtk_widget_unparent);
  if (child)
    {
      self->child = child;
      gtk_widget_set_parent (child, GTK_WIDGET (self));
    }
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_CHILD]);
}

/**
 * glass_group_get_spacing:
 * @self: a group
 *
 * Gets the merge distance in px.
 *
 * Returns: the merge distance in px
 */
double
glass_group_get_spacing (GlassGroup *self)
{
  g_return_val_if_fail (GLASS_IS_GROUP (self), 16.0);

  return self->spacing;
}

/**
 * glass_group_set_spacing:
 * @self: a group
 * @spacing: the merge distance in px
 *
 * Sets how close panels must come before their glass flows together.
 */
void
glass_group_set_spacing (GlassGroup *self,
                         double      spacing)
{
  g_return_if_fail (GLASS_IS_GROUP (self));

  spacing = MAX (spacing, 0.0);
  if (self->spacing == spacing)
    return;
  self->spacing = spacing;
  gtk_widget_queue_draw (GTK_WIDGET (self));
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_SPACING]);
}
